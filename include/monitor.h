#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#define DEBUGVERSION 0x20

/*
 * STM32U3 shared-memory monitor ABI.
 *
 * Host and firmware both include this header, so keep every field that crosses
 * the SWD boundary here. See embedded/tags/design/monitor_interface.md for the
 * L4 and U3 monitor transports.
 *
 * The block lives in a reserved hole at the bottom of U3 SRAM. Firmware must
 * keep that address out of normal .data/.bss/noinit ownership, and host code
 * must treat the address and layout as an ABI.
 *
 * This struct is only for host/tag handshake and RPC transport state. Local
 * firmware policy, such as delaying idle WFI until startup is complete, should
 * remain local to the firmware and should not add fields here unless the host
 * must observe or modify that state.
 */
#define MONITOR_SHARED_ADDR 0x20000000U
#define MONITOR_SHARED_SIZE 0x40U

/*
 * The request word is the monitor session state:
 *   0                       detached, idle, or no host request
 *   MONITOR_REQUEST_MAGIC   host requested attach before reset release
 *   MONITOR_CONNECTED_MAGIC target accepted and published metadata
 *
 * Firmware writes MONITOR_CONNECTED_MAGIC only after publishing metadata. The
 * SESSION_READY flag is set later, after ChibiOS is running and the monitor
 * watchdog/IRQ path is armed. Detach is requested with MONITORSTOP; the target
 * disarms the watchdog/session and acknowledges by clearing request back to 0.
 *
 * On U3, request remains the attached-state truth. MONITORSTOP is explicit
 * host intent, but the host should treat request==0 as the detached
 * confirmation rather than relying on command/status after the session is gone.
 *
 * The host keeps the U3 session alive by periodically setting host_activity.
 * The target checks it every MONITOR_HEARTBEAT_PERIOD_S seconds, clears it
 * when set, and disconnects on the next check if it remains clear. The
 * effective no-poll timeout is therefore one to two heartbeat periods.
 */
#define MONITOR_REQUEST_MAGIC 0xC0FFEE00U
#define MONITOR_CONNECTED_MAGIC 0xDEADBEEFU
#define MONITOR_PATH_U3_MAGIC 0x55330003U
#define MONITOR_SHARED_ABI_VERSION 1U
#define MONITOR_SHARED_FLAG_SESSION_READY (1U << 0)
#define MONITOR_HEARTBEAT_PERIOD_S 5U

/*
 * U3 monitor RPCs use a reserved external IRQ as a wake-capable software kick.
 * The host pends/clears it through NVIC ISPR/ICPR rather than using STIR.
 */
#define MONITOR_SHARED_KICK_IRQN 39U
#define MONITOR_SHARED_KICK_ISPR_ADDR 0xE000E204U
#define MONITOR_SHARED_KICK_ICPR_ADDR 0xE000E284U
#define MONITOR_SHARED_KICK_MASK (1U << 7)

enum MONITOR_STATUS {
  MONITOR_STATUS_IDLE = 0,
  MONITOR_STATUS_PENDING = 1,
  MONITOR_STATUS_DONE = 2,
  MONITOR_STATUS_BUSY = 3,
  MONITOR_STATUS_BAD_COMMAND = 4,
  MONITOR_STATUS_NOT_ATTACHED = 5,
};

typedef struct __attribute__((packed, aligned(4))) {
  volatile uint32_t request;
  volatile uint32_t abi_version;
  volatile uint32_t debug_version;
  volatile uint32_t flags;
  volatile uint32_t buf_addr;
  volatile uint32_t buf_size;
  volatile uint32_t sha_addr;
  volatile uint32_t command;
  volatile uint32_t result;
  volatile uint32_t status;
  volatile uint32_t host_activity;
  volatile uint32_t watchdog_ticks;
  volatile uint32_t path_magic;
} monitor_shared_t;

// monitor interrupt handler opcodes

enum DBGOP {
  TAG_MONITORINFO = 0,  // returns *sMonitor
  MONITORSTART,         // returns 0 (fail) 1 (success)
  MONITORSTOP,
  PROTOBUF,             // returns packet length
};


// operands for MONITORINFO

enum DBGINFO { MONITORVERSION = 0, MONITORBUF = 1, MONITORBUFSIZE = 2, TAGSHASTR = 3};

#ifdef __cplusplus
extern "C" {
#endif

void monitorServicePending(uint32_t monitor_events);
void monitorPostPendingEvents(void);
void monitorSharedEarlyInit(void);
void monitorSharedSessionStart(void);
/*
 * Runtime monitor session state. U3 uses the shared request word as the
 * attached-state indicator; L4 uses the legacy DebugMonitor session state.
 */
bool monitorIsAttached(void);
/* Compatibility alias for older tag code. */
bool isMonitorEnabled(void);
#ifdef __cplusplus
}
#endif

#endif
