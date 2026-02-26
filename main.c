#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "lwip/tcp.h"

// --- Config ---
#define FLASH_TARGET_OFFSET (1536 * 1024) // 1.5MB mark
#define RESET_BUTTON_PIN 15               // Hold to GND to reset
#define SERVER_IP "192.168.18.14"          // YOUR PC IP
#define SERVER_PORT 4242

typedef struct {
	char ssid[32];
	char pass[64];
	uint32_t magic;
} wifi_creds_t;

const wifi_creds_t *flash_creds = (const wifi_creds_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
char g_ssid[32], g_pass[64];
volatile bool creds_received = false;
void debug_print_flash() {
	printf("\n--- Checking Flash Memory ---\n");
	printf("Magic Number: 0x%08X\n", flash_creds->magic);

	if (flash_creds->magic == 0xDEADBEEF) {
		printf("SSID stored: [%s]\n", flash_creds->ssid);
		// Be careful printing passwords in production!
		printf("PASS stored: [%s]\n", flash_creds->pass);
	} else {
		printf("Flash is empty or corrupted (0xFFFFFFFF).\n");
	}
	printf("-----------------------------\n");
}
// --- Flash Storage ---

void save_creds(const char* s, const char* p) {
	// 1. Create a 256-byte buffer in RAM
	uint8_t buffer[FLASH_PAGE_SIZE]; 
	memset(buffer, 0, FLASH_PAGE_SIZE);

	// 2. Map our struct onto that RAM buffer
	wifi_creds_t *temp = (wifi_creds_t *)buffer;
	strncpy(temp->ssid, s, 31);
	strncpy(temp->pass, p, 63);
	temp->magic = 0xDEADBEEF;

	// 3. Perform the Flash operations
	uint32_t ints = save_and_disable_interrupts();

	// Erase the 4KB sector
	flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

	// Program the 256-byte page from our RAM buffer
	flash_range_program(FLASH_TARGET_OFFSET, buffer, FLASH_PAGE_SIZE);

	restore_interrupts(ints);
	printf("Flash Write Complete. Magic: 0x%08X\n", temp->magic);
	debug_print_flash();
}
//--- TCP Client ---
static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	tcp_close(tpcb);
	return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
	const char *msg = "Pico W online and talking to server!";
	tcp_write(tpcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);
	tcp_output(tpcb);
	tcp_sent(tpcb, tcp_client_sent);
	return ERR_OK;
}

void talk_to_server() {
	struct tcp_pcb *pcb = tcp_new();
	ip_addr_t addr;
	ipaddr_aton(SERVER_IP, &addr);
	tcp_connect(pcb, &addr, SERVER_PORT, tcp_client_connected);
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
	if (!p) { tcp_close(pcb); return ERR_OK; }
	char *data = (char *)p->payload;

	// 1. Check if this is the form submission
	if (strstr(data, "GET /c?")) {
		char *s_ptr = strstr(data, "s=");
		char *p_ptr = strstr(data, "p=");

		if (s_ptr && p_ptr) {
			// Move pointer past "s="
			s_ptr += 2; 
			// Copy SSID until '&'
			int i = 0;
			while (*s_ptr != '&' && i < 31) g_ssid[i++] = *s_ptr++;
			g_ssid[i] = '\0';

			// Move pointer past "p="
			p_ptr += 2;
			// Copy PASS until ' ' (space at end of GET request)
			i = 0;
			while (*p_ptr != ' ' && i < 63) g_pass[i++] = *p_ptr++;
			g_pass[i] = '\0';

			printf("\n--- Captured! ---\nSSID: %s\n", g_ssid);
			creds_received = true; 
		}
	}

	// 2. Send Response
	const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
		"<html><body><h2>Pico Setup</h2><form action='/c' method='get'>"
		"SSID: <input name='s'><br>Pass: <input name='p'><br>"
		"<input type='submit' value='Connect'></form></body></html>";
	tcp_write(pcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
	tcp_output(pcb);
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
	tcp_recv(pcb, http_recv);
	return ERR_OK;
}

// --- Main ---
int main() {
	stdio_init_all();
	sleep_ms(3000); // Give Ubuntu time to recognize the USB
	printf("Flash Pointer Address: %p\n", (void*)flash_creds);
	printf("Flash Raw Magic: 0x%08X\n", flash_creds->magic);
	printf("\n\n--- PICO SYSTEM ONLINE ---\n");
	if (cyw43_arch_init()) return 1;

	bool connected = false;

	// 1. Try Auto-Connect from Flash
	if (flash_creds->magic == 0xDEADBEEF) {
		printf("Saved creds found. Connecting to %s...\n", flash_creds->ssid);
		cyw43_arch_enable_sta_mode();

		int err = cyw43_arch_wifi_connect_timeout_ms(flash_creds->ssid, flash_creds->pass, CYW43_AUTH_WPA2_AES_PSK, 15000);
		if (err == 0) {
			printf("Auto-connected! IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
			connected = true;
		} else {
			printf("Auto-connect failed (Error %d). Falling back to AP mode.\n", err);
		}
	}

	// 2. Only enter AP Mode if NOT connected
	if (!connected) {
		while (1) {
			creds_received = false;
			cyw43_arch_enable_ap_mode("PicoW_Setup", "password", CYW43_AUTH_WPA2_AES_PSK);

			// Start Web Server
			struct tcp_pcb *pcb = tcp_new();
			tcp_bind(pcb, IP_ADDR_ANY, 80);
			pcb = tcp_listen(pcb);
			tcp_accept(pcb, http_accept);

			printf("AP Active at 192.168.4.1\n");

			// Wait for Web Form
			while (!creds_received) {
				//cyw43_arch_poll();
				sleep_ms(1);
			}

			// Cleanup AP
			tcp_close(pcb);
			cyw43_arch_disable_ap_mode();
			cyw43_arch_enable_sta_mode();

			// Try New Credentials
			if (cyw43_arch_wifi_connect_timeout_ms(g_ssid, g_pass, CYW43_AUTH_WPA2_AES_PSK, 15000) == 0) {
				save_creds(g_ssid, g_pass);
				connected = true;
				break; // Exit the Provisioning Loop
			}
			printf("New credentials failed. Retrying AP...\n");
		}
	}

	// 3. Final Task (Talk to Server)
	if (connected) {
		talk_to_server();
	}

	// Keep the system alive
	while(1) {
		//cyw43_arch_poll();
		sleep_ms(1000);
	}
}

