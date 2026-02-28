#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "lwip/apps/http_client.h"

// --- 1. CONFIGURATION ---
#define SENSOR_PIN 22          // Reed Switch (GND to Pin)
#define RESET_PIN 15           // Factory Reset Button (GND to Pin)
#define DEBOUNCE_MS 250
#define FLASH_TARGET_OFFSET (2044 * 1024) // Last 4KB sector of 2MB Flash
#define SERVER_IP "129.80.142.41"        // <--- SET YOUR VM IP HERE
#define SERVER_PORT 8080 
#define WATCHDOG_TIMEOUT_MS 8000          
#define DACHI_MAGIC 0xDACC1               // Valid Hex Magic

char global_path[128];
typedef struct {
	char ssid[32];
	char pass[64];
	uint32_t magic;
} settings_t;

// --- 2. GLOBAL STATE ---
static volatile bool event_pending = false;
static volatile bool door_state = false;
static volatile uint32_t last_interrupt_time = 0;

extern char __flash_binary_end;
const settings_t *stored_settings = (const settings_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

// --- 3. CORE UTILITIES ---

void led_signal(int blinks, int speed_ms) {
	for(int i = 0; i < blinks; i++) {
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		sleep_ms(speed_ms);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
		sleep_ms(speed_ms);
		watchdog_update(); 
	}
}

void save_settings(settings_t *s) {
	s->magic = DACHI_MAGIC;
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(FLASH_TARGET_OFFSET, (uint8_t *)s, FLASH_PAGE_SIZE);
	restore_interrupts(ints);
}

void verify_flash_safety() {
	uintptr_t binary_end = (uintptr_t)&__flash_binary_end - XIP_BASE;
	if (binary_end >= FLASH_TARGET_OFFSET) {
		printf("bin too large?????? \n");
		while(1) { led_signal(10, 30); } // Panic: Binary is too large!
	}
}

void check_factory_reset() {
	gpio_init(RESET_PIN);
	gpio_set_dir(RESET_PIN, GPIO_IN);
	gpio_pull_up(RESET_PIN);
	if (gpio_get(RESET_PIN) == 0) {
		printf("factory reset??????? \n");
		led_signal(10, 50); // Warning: Rapid blinks
		sleep_ms(3000);     // Wait 3 seconds
		if (gpio_get(RESET_PIN) == 0) {
			settings_t wipe = {0};
			save_settings(&wipe);
			watchdog_reboot(0, 0, 0);
		}
	}
}

// --- 4. NETWORK CALLBACKS & PROVISIONING ---

err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	if (p != NULL) {
		tcp_recved(tpcb, p->tot_len);
		char *req = (char *)p->payload;

		if (strstr(req, "GET /save")) {
			settings_t n = {0};
			char *s_ptr = strstr(req, "s=") + 2;
			char *p_ptr = strstr(req, "p=") + 2;
			if (s_ptr && p_ptr) {
				sscanf(s_ptr, "%[^&]", n.ssid);
				sscanf(p_ptr, "%[^ ]", n.pass);
				save_settings(&n);
				const char *res = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
					"<html><body style='font-family:sans-serif;background-color:#111;color:#fff;text-align:center;padding-top:50px;'>"
					"<h1>INITIALIZING</h1><p style='color:#888;'>Sentry joining network...</p></body></html>";
				tcp_write(tpcb, res, strlen(res), TCP_WRITE_FLAG_COPY);
				tcp_output(tpcb);
				sleep_ms(2000);
				watchdog_reboot(0, 0, 0); 
			}
		} else {
			const char *form = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
				"<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
				"<style>"
				"body{font-family:sans-serif; background-color:#111; color:#fff; margin:0; padding:20px; text-align:center;}"
				".container{max-width:400px; margin:50px auto; padding:20px; border-radius:12px; border:1px solid #333; background-color:#080808;}"
				"h2{font-weight:200; letter-spacing:2px; text-transform:uppercase;}"
				"label{color:#888; font-size:12px; text-transform:uppercase; display:block; text-align:left; margin:10px 0 5px 5px;}"
				"input{width:100%; padding:14px; border-radius:6px; background-color:#1c1c1c; border:1px solid #333; color:#fff; box-sizing:border-box; margin-bottom:15px;}"
				"input:focus{outline:none; border-color:#d4001c;}"
				"input[type=submit]{background-color:#d4001c; color:#fff; cursor:pointer; border:none; letter-spacing:1px; font-weight:bold;}"
				"</style></head>"
				"<body><div class='container'>"
				"<h2>🛡️ DACHI</h2>"
				"<p style='color:#666; font-size:13px;'>SENTRY CONFIGURATION</p>"
				"<form action='/save' method='get'>"
				"<label>Network SSID</label><input type='text' name='s' required placeholder='SSID'>"
				"<label>Password</label><input type='password' name='p' required placeholder='••••••••'>"
				"<input type='submit' value='INITIALIZE SENTRY'>"
				"</form>"
				"</div></body></html>";
			tcp_write(tpcb, form, strlen(form), TCP_WRITE_FLAG_COPY);
			tcp_output(tpcb);
		}
		pbuf_free(p);
	}
	return ERR_OK;
}

err_t setup_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
	if (err != ERR_OK || newpcb == NULL) {
		return ERR_VAL;
	}

	// Assign the receiver callback to the new connection
	tcp_recv(newpcb, http_recv_callback);

	// Optional: Set an argument if you need to track state
	tcp_arg(newpcb, NULL);

	return ERR_OK;
}

void gpio_callback(uint gpio, uint32_t events) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (now - last_interrupt_time > DEBOUNCE_MS) {
		door_state = gpio_get(SENSOR_PIN);
		event_pending = true;
		last_interrupt_time = now;
	}
}

void send_update(bool is_open, const char* event_type) {
	/*
	   printf("Dachi : %s \n", is_open ? "OPENED": "CLOSED"); 
	   printf("update sent\n");
	   char path[128];
	   snprintf(path, 128, "/update?node=DACHI_01&state=%s&event=%s", is_open ? "OPEN" : "CLOSED", event_type);
	   httpc_connection_t settings = { .result_fn = NULL };
	   memset(&settings, 0, sizeof(httpc_connection_t));
	   cyw43_arch_lwip_begin();
	//err_t err=httpc_get_file_dns(SERVER_IP, SERVER_PORT, path, &settings, NULL, NULL, NULL);
	err_t err = httpc_get_file_dns("129.80.142.41", 8000, "/update?test=1", &settings, NULL, NULL, NULL);
	cyw43_arch_lwip_end();
	*/
	/*
	   snprintf(global_path, sizeof(global_path), "/update?node=FRONT_DOOR&state=OPEN&event=TRIGGER");

	   cyw43_arch_lwip_begin();
	   httpc_connection_t settings;
	   memset(&settings, 0, sizeof(settings));
	   settings.use_proxy = 0;
	// result_fn is optional, but setting it to NULL explicitly is good practice
	settings.result_fn = NULL; 
	ip_addr_t target_addr;
	// Convert string to LwIP IP structure
	ip4addr_aton("129.80.142.41", ip_2_ip4(&target_addr));
	err_t err = httpc_get_file_dns(
	&target_addr,
	8080, 
	global_path, 
	&settings, 
	NULL, 
	NULL, 
	NULL
	);
	cyw43_arch_lwip_end();
	*/

	// Use a static settings struct to ensure it stays in memory
	printf("send update starthere?? \n");
	static httpc_connection_t settings;
	memset(&settings, 0, sizeof(settings));
	settings.use_proxy = 0;
	settings.result_fn = NULL; // Keep it simple first

	cyw43_arch_lwip_begin();
	// Use the IP-specific function directly
	err_t err = httpc_get_file_dns(
			"129.80.142.41", 
			8080, 
			"/update?node=TEST&state=OPEN", 
			&settings, 
			NULL, 
			NULL, 
			NULL
			);

	cyw43_arch_lwip_end();
	printf("send updatehere?? \n");

	if (err != ERR_OK) {
		printf("Dachi Error: Packet failed to initiate. Code: %d\n", err);
	} else {
		printf("Dachi Success: Packet handed to WiFi chip!\n");
	}
}

// --- 5. MAIN EXECUTION ---

int main() {
	stdio_init_all();

	sleep_ms(3000);
	// 1. Initialize WiFi Architecture
	printf("here?? \n");
	if (cyw43_arch_init()) {
		printf("WiFi Init Failed\n");
		return -1;
	}

	printf("wifi init went okhere?? \n");
	// 2. Hardware Checks (Flash/Reset)
	verify_flash_safety();
	check_factory_reset();
	//watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);

	// --- START PROVISIONING (AP MODE) ---
	if (stored_settings->magic != DACHI_MAGIC) {
		printf("Dachi: Entering Setup Mode (AP)...\n");

		// Turn on Access Point
		cyw43_arch_enable_ap_mode("Dachi_Setup", "dachi123", CYW43_AUTH_WPA2_AES_PSK);

		// CRITICAL: Lock the LWIP stack before creating a new server
		cyw43_arch_lwip_begin();

		struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
		if (!pcb) {
			printf("LwIP Error: Could not create PCB\n");
			cyw43_arch_lwip_end();
		} else {
			// Bind to Port 80
			err_t err = tcp_bind(pcb, IP_ADDR_ANY, 80);
			if (err != ERR_OK) {
				printf("LwIP Error: Bind failed (%d)\n", err);
				tcp_abort(pcb);
			} else {
				// IMPORTANT: You must re-assign 'pcb' here!
				pcb = tcp_listen_with_backlog(pcb, 1);

				// Attach the accept callback
				tcp_accept(pcb, setup_accept_callback);
				printf("Dachi: [PORT 80 OPEN] - Listening on 192.168.4.1\n");
			}
			cyw43_arch_lwip_end(); // Unlock to allow background processing
		}

		// --- AP SERVICE LOOP ---
		while(stored_settings->magic != DACHI_MAGIC) {
			watchdog_update();

			// This is the "Magic Poke": It forces the CYW43 chip to 
			// check for incoming packets even if interrupts are missed.
			cyw43_arch_poll(); 

			// Visual pulse: Double-blink means "I am waiting for you"
			led_signal(2, 50); 
			sleep_ms(500); 
		}
		tcp_close(pcb);  
		cyw43_arch_disable_ap_mode();   
	}
	cyw43_arch_enable_sta_mode();
	if (cyw43_arch_wifi_connect_blocking(stored_settings->ssid, stored_settings->pass, CYW43_AUTH_WPA2_AES_PSK)) {
		watchdog_reboot(0, 0, 0); 
	}

	printf("wift sta connected fine here?? \n");
	// SETUP DOOR SENSOR
	gpio_init(SENSOR_PIN);
	gpio_set_dir(SENSOR_PIN, GPIO_IN);
	gpio_pull_up(SENSOR_PIN);
	gpio_set_irq_enabled_with_callback(SENSOR_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

	uint32_t last_hb = 0;
	while(1) {
		printf("while for state change here?? \n");

		watchdog_update(); 
		if (event_pending) {
			event_pending = false;
			led_signal(5, 50); 
			send_update(door_state, "TRIGGER");
		}
		uint32_t now = to_ms_since_boot(get_absolute_time());
		if (now - last_hb > 60000) { // Heartbeat every 60s
			last_hb = now;
			led_signal(1, 200); 
			send_update(gpio_get(SENSOR_PIN), "HEARTBEAT");
			printf("hearbeat \n");
		}
		cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
	}
}
