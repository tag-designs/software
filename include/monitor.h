#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

/*
 * Monitor protocol version of the DebugMonitor transport (handlersL4.c): what
 * an STM32L4 image without the mailbox answers to TAG_MONITORINFO/
 * MONITORVERSION. Unchanged since before fw-v0.7, and up to fw-v0.7 also the
 * version every STM32U3 image published.
 */
#define DEBUGVERSION 0x20

/*
 * Monitor protocol version of every mailbox image: STM32U3, and STM32L4 built
 * with TAG_L4_MONITOR_MAILBOX. It is the shared block's debug_version, and on
 * an STM32L4 mailbox image also the MONITORVERSION answer over the
 * DebugMonitor.
 *
 * From 0x21 an STM32L4 image answers MONITORPROTOCOL, which says whether
 * protobuf requests go over the DebugMonitor or the shared-memory mailbox; a
 * host asks that question only of 0x21 or later, because the DebugMonitor
 * handler answers an unknown operand with the host's own command word still
 * in DCRDR.
 *
 * Hosts up to v4.0.1 require a U3 block's debug_version to equal 0x20
 * exactly, so they refuse 0x21 firmware; later hosts accept 0x20 up to
 * DEBUGVERSION_MAILBOX.
 */
#define DEBUGVERSION_MAILBOX 0x21

/*
 * STM32U3 shared-memory monitor ABI.
 *
 * Host and firmware both include this header, so keep every field that crosses
 * the SWD boundary here. See docs/shared/monitor-interface.md for the
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
#define MONITOR_PATH_L4_MAGIC 0x554C0004U
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

/*
 * monitor_shared_t mirrors a fixed block of tag memory read over SWD, so its
 * layout must be identical on every toolchain that sees this header. GCC and
 * Clang take the attribute syntax; MSVC (the host tools on Windows) needs
 * #pragma pack plus __declspec, so the packing is spelled per compiler here
 * and applied once to the struct below.
 */
#if defined(_MSC_VER)
#define MONITOR_SHARED_PACK_BEGIN __pragma(pack(push, 1))
#define MONITOR_SHARED_PACK_END __pragma(pack(pop))
#define MONITOR_SHARED_ALIGNED __declspec(align(4))
#else
#define MONITOR_SHARED_PACK_BEGIN
#define MONITOR_SHARED_PACK_END
#define MONITOR_SHARED_ALIGNED __attribute__((packed, aligned(4)))
#endif

MONITOR_SHARED_PACK_BEGIN
typedef struct MONITOR_SHARED_ALIGNED {
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
MONITOR_SHARED_PACK_END

// monitor interrupt handler opcodes

enum DBGOP {
  TAG_MONITORINFO = 0,  // returns *sMonitor
  MONITORSTART,         // returns 0 (fail) 1 (success)
  MONITORSTOP,
  PROTOBUF,             // returns packet length
};


// operands for MONITORINFO

enum DBGINFO { MONITORVERSION = 0, MONITORBUF = 1, MONITORBUFSIZE = 2, TAGSHASTR = 3,
               MONITORPROTOCOL = 4 };

/*
 * Answers to MONITORPROTOCOL, asked of an STM32L4 image reporting
 * DEBUGVERSION_MAILBOX or later: which transport carries protobuf requests.
 * An image that answers MONITOR_PROTOCOL_MAILBOX keeps the DebugMonitor for
 * TAG_MONITORINFO only; the host then starts the session by writing
 * MONITOR_REQUEST_MAGIC to the shared block and pending the kick IRQ, and the
 * image refuses MONITORSTART and PROTOBUF over the DebugMonitor.
 * MONITOR_PROTOCOL_DEBUGMON is for a future 0x21-or-later image that stays on
 * the DebugMonitor; DebugMonitor images today report DEBUGVERSION (0x20) and
 * are never asked.
 */
enum MONITOR_PROTOCOL {
  MONITOR_PROTOCOL_DEBUGMON = 1,
  MONITOR_PROTOCOL_MAILBOX = 2,
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Serve a latched monitor request on the main thread. Returns true when one
 * was served, so the caller can run the state machine again before sleeping.
 */
bool monitorServicePending(uint32_t monitor_events);
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
