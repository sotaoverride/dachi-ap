#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Use the SDK's common defaults
#include "lwipopts_examples_common.h"

// Required for TCP and raw sockets
#define LWIP_TCP                1
#define TCP_MSS                 1460
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_SND_BUF             (4 * TCP_MSS)

// Required for the Reed Switch timer/background processing
#define MEMP_NUM_SYS_TIMEOUT    (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)

#endif

