#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Core Features
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define IP_REASSEMBLY               1
#define IP_FRAG                     1

// TCP/HTTP Specifics (The fixes for -16)
#define LWIP_TCP                    1
#define LWIP_HTTPC                  1
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              0
#define LWIP_DNS                    1

// Memory Pools - Crucial for LwIP State Machine
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     8
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_SYS_TIMEOUT        10
#define MEMP_NUM_HTTPC_STATE        4  // This allocates the internal slots

// TCP Window Settings
#define TCP_MSS                     1460
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)

#endif
