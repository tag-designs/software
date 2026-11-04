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
 *          It checks the capacity claim and the end of the log: a 4 MiB part
 *          holds 524,288 minute records (364 days) in 16,384 whole blocks, the
 *          run ends cleanly when the next record would pass the end of the
 *          part, every block downloads complete, nothing is read past the end,
 *          and a request past the last block answers NODATA.
 *
 *          Every record carries its sequence number in its pressure, so the
 *          download is checked record by record against what was written.
 *
 *          It also checks restoreLog(): after an empty log, a run partway into
 *          its second block, a run with missed minutes, and a full part, the
 *          recovered record count equals the one Running() kept.
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
  struct { pb_size_t size; uint8_t bytes[256]; } samples;
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
    /* The real part has no bytes past its end. */
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

/* Each record carries its sequence number as its raw pressure (24 bits hold
   the 1,048,576 records 8 MiB holds). */
bool samplePressureRaw(t_UIUCTagRecord *record)
{
    uiuctagPut24(record->pressure, sample_seq);
    uiuctagPut24(record->temperature, 20u * 65536u);
    sample_seq++;
    return true;
}

#include "datalog.c"
#include "state_run.c"

/* --- the simulation ------------------------------------------------------ */
static const int32_t BLOCK0 = 1767225600;

/**
 * Erase the simulated flash and state for a fresh run on a part of @p bytes.
 */
static void sim_reset(uint32_t bytes)
{
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
}

/**
 * Check that restoreLog() rebuilds the counters Running() kept, from flash
 * alone, after the retained copies are lost.
 */
static void check_recovery(const char *what)
{
    const uint32_t pages = pState->pages;
    const uint32_t records = pState->external_blocks;

    pState->pages = 777;
    pState->external_blocks = 12345;
    assert(restoreLog() == 0);
    assert(!nor_awake && "external flash left awake");
    assert(pState->pages == pages && "checkpoint count recovered");
    assert(pState->external_blocks == records && "record count recovered");
    printf("recovery %s: %u checkpoints, %u records\n", what,
           (unsigned)pages, (unsigned)records);
}

/**
 * restoreLog() on an empty log, partway into a second block, after missed
 * minutes, and (from test_full) on a full part.
 */
static void test_recovery(void)
{
    sim_reset(4u * 1024u * 1024u);
    check_recovery("empty log");
    assert(pState->external_blocks == 0);

    /* 45 minute alarms: one full block and 13 records of the next. */
    timestamp = BLOCK0 + 137;
    events = 0;
    Running(T_INIT, 0);
    for (int i = 0; i < 45; i++) {
        timestamp = BLOCK0 + 180 + 60 * i;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
    }
    assert(pState->external_blocks == 45);
    check_recovery("45 minutes");

    /* Ten missed minutes, then three more records: the count is a position,
       so the gap counts, as it does in the download. */
    for (int i = 55; i < 58; i++) {
        timestamp = BLOCK0 + 180 + 60 * i;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
    }
    assert(pState->external_blocks == 58);
    check_recovery("after missed minutes");
}

/**
 * Fill @p bytes of external flash with minute alarms, then download every
 * block and check it record by record.
 */
static void test_full(uint32_t bytes, const char *part)
{
    static Ack ack;

    sim_reset(bytes);
    timestamp = BLOCK0 + 137;
    events = 0;
    Running(T_INIT, 0);
    for (timestamp = BLOCK0 + 180; finished_count == 0; timestamp += 60) {
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
    }

    const uint32_t blocks = bytes / UIUCTAG_BLOCK_BYTES;
    const uint32_t records = bytes / UIUCTAG_RECORD_SIZE;
    assert(bytes % UIUCTAG_BLOCK_BYTES == 0 && "a part is whole blocks");
    assert(pState->pages == blocks && "one checkpoint per block");
    /* The wake after the last record took a reading and found no room. */
    assert(sample_seq == records + 1);

    uint32_t seq = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        memset(&ack, 0, sizeof(ack));
        data_logAck((int)b, &ack);
        assert(ack.which_payload == Ack_uiuctag_data_log_tag);
        const UIUCTagLog *log = &ack.payload.uiuctag_data_log;
        assert(log->epoch == vddHeader[b].epoch);
        assert(log->epoch == BLOCK0 + 180 + (int32_t)(b * UIUCTAG_DATA_LOG_SECONDS));
        assert(log->samples.size == UIUCTAG_BLOCK_BYTES && "every block is full");

        for (uint32_t s = 0; s < UIUCTAG_LOG_RECORDS; s++, seq++) {
            t_UIUCTagRecord record;
            memcpy(&record, &log->samples.bytes[s * UIUCTAG_RECORD_SIZE],
                   sizeof(record));
            assert(!uiuctagRecordErased(&record));
            assert(uiuctagRecordHasReading(&record) || seq == 0);
            assert(uiuctagRecordPressureRaw(&record) == seq &&
                   "record out of sequence");
            assert(uiuctagRecordHasActivity(&record));
            assert(record.activity ==
                   (seq == 0 ? UIUCTAG_ACTIVITY_UNOBSERVED_WORD : 0u) &&
                   "first minute unobserved, every later one watched");
        }
    }

    memset(&ack, 0, sizeof(ack));
    data_logAck((int)blocks, &ack);
    assert(ack.which_payload == 0 && ack.err == Ack_Err_NODATA);
    assert(seq == records);

    printf("full %s: %u blocks, %u records = %.1f days of minute records\n",
           part, (unsigned)blocks, (unsigned)seq, seq / 1440.0);
    assert(pState->external_blocks == records);
    check_recovery(part);
}

int main(void)
{
    test_recovery();
    test_full(4u * 1024u * 1024u, "4 MiB (AT25XE321D)");
    test_full(8u * 1024u * 1024u, "8 MiB");
    printf("UIUCTAG DATALOG SIM: all assertions passed\n");
    return 0;
}
