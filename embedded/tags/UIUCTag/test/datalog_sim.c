/**
 * @file    datalog_sim.c
 * @brief   Host-side simulation of a UIUCTag run that fills external flash,
 *          downloaded through the real data_logAck().
 *
 * @details Compiles the real ../src/state_run.c and ../src/datalog.c, by
 *          including them directly, against the minimal stubs in test/stub
 *          and the definitions below. Where sequencer_sim.c replaces the
 *          datalog layer with a recorder, this links the real one to a fake
 *          external NOR and a fake internal checkpoint array, and drives the
 *          minute alarm until the flash is full.
 *
 *          Its subject is A3 on the firmware-fix branch: the flash size is not
 *          a multiple of the 288-byte block, so the end of the part cuts the
 *          final block short. fw-v0.0.3 served only whole blocks, so that block
 *          was written and never downloaded. The final block must be served,
 *          hold every slot that was completed, keep the field of the slot that
 *          was cut short, and read nothing past the end of the part.
 *
 *          Every sample carries its sequence number in its pressure, so the
 *          download is checked slot by slot against what was written.
 *
 * @note    Not part of any build. Build and run instructions are in
 *          test/README.md.
 */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stub/tag.pb.h"
#include "stub/config.h"
#include "stub/persistent.h"
#include "uiuctag_log_format.h"
#include "datalog.h"
#include "stub/storage_flash.h"
#include "stub/flash_internal.h"

/* --- pieces the sources expect from the runtime -------------------------- */
enum Sleep { SLEEP, STANDBY, SHUTDOWN };
enum StateTrans { T_INIT, T_CONT, T_ERROR };
#define EVT_RTC_ALRAF 0x100u
#define LINE_WKUP1 0
#define ALARM_MINUTE 1

/* Register blocks and kernel calls datalog.c touches (stub/hal.h is empty). */
typedef struct { uint32_t ACR; } stub_flash_t;
typedef struct { uint32_t CR; } stub_rcc_t;
typedef struct { uint32_t PSC; } stub_tim_t;
static stub_flash_t stub_flash;
static stub_rcc_t stub_rcc;
static stub_tim_t stub_tim = {1};
#define FLASH (&stub_flash)
#define RCC (&stub_rcc)
#define STM32_ST_TIM (&stub_tim)
#define STM32_MSIRANGE_24M (9u << 4)   /* ChibiOS STM32L4 values */
#define STM32_MSIRANGE (5u << 4)
#define STM32_TIMCLK2 2000000u
#define OSAL_ST_FREQUENCY 1000u
#define HIGHPRIO 255
#define NORMALPRIO 128
static void chSysLock(void) {}
static void chSysUnlock(void) {}
static void chThdYield(void) {}
static void chThdSetPriority(int p) { (void)p; }
void stopMilliseconds(uint32_t ms) { (void)ms; }
/* Declared by the real persistent.h; stub/persistent.h carries only what
   state_run.c needs. */
void eraseExternalStart(void);
bool eraseExternalNextSector(void);
void eraseExternalFinish(void);
uint32_t externalFlashSize(void);

/* The UIUCTagLog Ack, as embedded/proto-c/uiuctag-proto-c generates it. */
typedef uint16_t pb_size_t;
typedef struct {
  int32_t epoch;
  float voltage;
  struct { pb_size_t size; uint8_t bytes[288]; } samples;
} UIUCTagLog;
typedef enum { Ack_Err_OK = 0, Ack_Err_NODATA = 3 } Ack_Err;
#define Ack_uiuctag_data_log_tag 17
typedef struct {
  Ack_Err err;
  uint16_t which_payload;
  union { UIUCTagLog uiuctag_data_log; } payload;
} Ack;

/* The firmware takes the address of a linker symbol; point it at the end of
   the simulated checkpoint array instead. */
extern uint32_t *sim_persistent_end;
#define __persistent_end__ (*sim_persistent_end)

int32_t timestamp;
uint32_t events;
bool isActive;
t_storedconfig sconfig;
static BackupState backup;
volatile BackupState *const pState = &backup;
const TagStorageDevice tagExternalFlash;

/* --- simulated internal flash: the checkpoint array and its end ---------- */
#define MAX_HEADERS 40000
t_DataHeader vddHeader[MAX_HEADERS];
uint32_t *sim_persistent_end = (uint32_t *)&vddHeader[MAX_HEADERS];

void FLASH_Lock(void) {}
void FLASH_Unlock(void) {}
void FLASH_Flush_Data_Cache(void) {}
void FLASH_ClearAllErrors(void) {}

uint32_t FLASH_Read_Checked(const void *Address, void *Data, size_t Bytes)
{
    memcpy(Data, Address, Bytes);
    return 0;
}

uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words)
{
    for (int i = 0; i < words; i++) {
        assert(Address[i] == 0xFFFFFFFFu && "internal flash programmed twice");
        Address[i] = array[i];
    }
    return 0;
}

/* --- simulated external NOR ---------------------------------------------- */
#define SECTOR_SIZE 4096
#define MAX_FLASH (8u * 1024u * 1024u)
static uint8_t nor[MAX_FLASH];
static int sector_count;
static bool nor_awake;

int tagStorageSectorSize(const TagStorageDevice *dev) { (void)dev; return SECTOR_SIZE; }
int tagStorageSectorCount(const TagStorageDevice *dev) { (void)dev; return sector_count; }
void tagStorageWake(const TagStorageDevice *dev) { (void)dev; assert(!nor_awake); nor_awake = true; }
void tagStorageSleep(const TagStorageDevice *dev) { (void)dev; assert(nor_awake); nor_awake = false; }

bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address,
                     uint8_t *buf, int *cnt)
{
    (void)dev;
    assert(nor_awake && "external flash written while asleep");
    assert(address + (uint32_t)*cnt <= (uint32_t)sector_count * SECTOR_SIZE &&
           "write past the end of external flash");
    for (int i = 0; i < *cnt; i++) {
        assert(nor[address + i] == 0xFF && "external flash programmed twice");
        nor[address + i] = buf[i];
    }
    return true;
}

bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    (void)dev;
    memset(&nor[address], 0xFF, SECTOR_SIZE);
    return true;
}

void tagStorageRead(const TagStorageDevice *dev, uint32_t address,
                    uint8_t *buf, int num)
{
    (void)dev;
    assert(nor_awake && "external flash read while asleep");
    /* The real part has no bytes past its end; reading them is the A3 bug. */
    assert(address + (uint32_t)num <= (uint32_t)sector_count * SECTOR_SIZE &&
           "read past the end of external flash");
    memcpy(buf, &nor[address], num);
}

/* --- other runtime stubs ------------------------------------------------- */
static int finished_count;
static uint32_t sample_seq;

enum Sleep Finished(enum StateTrans t, State_Event r) { (void)t; (void)r; finished_count++; return SHUTDOWN; }
enum Sleep Hibernating(enum StateTrans t, State_Event r) { (void)t; (void)r; assert(!"unexpected hibernation"); return SHUTDOWN; }
enum Sleep Aborted(enum StateTrans t, State_Event r) { (void)t; (void)r; assert(!"unexpected abort"); return SHUTDOWN; }
void disableAllAlarms(void) {}
void disableTicker(void) {}
void enableAlarm(unsigned int a, int type) { (void)a; (void)type; }
void adcVDD(uint16_t *vdd100, int16_t *temp10) { *vdd100 = 300; *temp10 = 250; }
void recordState(State_Event reason) { (void)reason; }
void initDataCollection(void) {}
int palReadLine(int line) { (void)line; return 0; }
int encode_ack(void) { return 0; }

/* Each sample carries its sequence number as its pressure (exact in a float
   well past the 349525 samples 4 MiB holds). */
bool samplePressure(float *pressure_hpa, float *temperature_c)
{
    *pressure_hpa = (float)sample_seq;
    *temperature_c = 20.0f;
    sample_seq++;
    return true;
}

#include "datalog.c"
#include "state_run.c"

/* --- the simulation ------------------------------------------------------ */
static const int32_t BLOCK0 = 1767225600;   /* multiple of 7200 */

/**
 * Fill @p bytes of external flash with minute alarms, then download every
 * block and check it slot by slot.
 */
static void test_full(uint32_t bytes, const char *part)
{
    static Ack ack;
    const uint32_t block_bytes = DATALOG_BLOCK_BYTES;

    assert(bytes % SECTOR_SIZE == 0 && bytes <= MAX_FLASH);
    /* datalog.c compares checkpoint addresses as uint32_t, as it may on the
       32-bit tag. On a 64-bit host that is sound only while the array does
       not straddle a 4 GiB boundary. */
    assert(((uintptr_t)&vddHeader[0] >> 32) ==
           ((uintptr_t)sim_persistent_end >> 32));
    sector_count = (int)(bytes / SECTOR_SIZE);
    memset(nor, 0xFF, sizeof(nor));
    memset(vddHeader, 0xFF, sizeof(vddHeader));
    memset(&backup, 0, sizeof(backup));
    sconfig.stop = INT_MAX;
    memset(sconfig.hibernate, 0, sizeof(sconfig.hibernate));
    sample_seq = 0;
    finished_count = 0;

    timestamp = BLOCK0 + 137;
    events = 0;
    Running(T_INIT, 0);
    for (timestamp = BLOCK0 + 180; finished_count == 0; timestamp += 60) {
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
    }

    const uint32_t whole = bytes / block_bytes;
    const uint32_t tail_bytes = bytes % block_bytes;
    const uint32_t tail_slots = tail_bytes / UIUCTAG_SAMPLE_SIZE;
    /* The slot cut short keeps the 4-byte fields that fit: pressure, then
       temperature. Its activity word is the third field and never fits. */
    const uint32_t tail_fields = (tail_bytes % UIUCTAG_SAMPLE_SIZE) / 4;
    const bool tail_pressure = tail_fields >= 1;
    assert(tail_slots > 0 && "choose a size that cuts the last block short");
    assert(pState->pages == whole + 1 && "checkpoint for the cut-short block");

    uint32_t seq = 0;
    for (uint32_t b = 0; b <= whole; b++) {
        memset(&ack, 0, sizeof(ack));
        data_logAck((int)b, &ack);
        assert(ack.which_payload == Ack_uiuctag_data_log_tag &&
               "block not served (the final partial block, if b == whole)");
        const UIUCTagLog *log = &ack.payload.uiuctag_data_log;
        assert(log->epoch == vddHeader[b].epoch);
        assert(log->samples.size % UIUCTAG_SAMPLE_SIZE == 0);
        uint32_t slots = log->samples.size / UIUCTAG_SAMPLE_SIZE;
        uint32_t complete = (b < whole) ? UIUCTAG_LOG_SAMPLES : tail_slots;
        assert(slots == complete + ((b == whole && tail_pressure) ? 1 : 0));

        for (uint32_t s = 0; s < slots; s++, seq++) {
            t_UIUCTagSample sample;
            memcpy(&sample, &log->samples.bytes[s * UIUCTAG_SAMPLE_SIZE],
                   sizeof(sample));
            assert(uiuctagSampleHasPressure(&sample));
            assert(sample.pressure == (float)seq && "slot out of sequence");
            if (s < complete) {
                assert(uiuctagSampleHasTemperature(&sample));
                /* The last complete slot's activity is written at the next
                   sample's wake; in the final block that wake ran out of
                   flash after the activity and pressure, so it is there. */
                assert(uiuctagSampleHasActivity(&sample));
            } else {
                /* Cut short: the fields that fit are written, the rest lie
                   past the end of the part and are served as missing. */
                assert(uiuctagSampleHasTemperature(&sample) == (tail_fields >= 2));
                assert(!uiuctagSampleHasActivity(&sample));
            }
        }
    }

    memset(&ack, 0, sizeof(ack));
    data_logAck((int)whole + 1, &ack);
    assert(ack.which_payload == 0 && ack.err == Ack_Err_NODATA);
    /* At most one sample is lost: one taken at the wake whose first write
       found the flash full. */
    assert(sample_seq - seq <= 1);

    printf("A3 full %s: %u whole blocks + final block of %u slots%s; "
           "%u samples downloaded\n",
           part, (unsigned)whole, (unsigned)tail_slots,
           tail_fields >= 2 ? " + one cut short (pressure, temperature)"
           : tail_pressure ? " + one cut short (pressure)" : "",
           (unsigned)seq);
}

int main(void)
{
    test_full(4u * 1024u * 1024u, "4 MiB (AT25XE321D)");
    test_full(8u * 1024u * 1024u, "8 MiB");
    printf("UIUCTAG DATALOG SIM: all assertions passed\n");
    return 0;
}
