/**
 * @file    sequencer_sim.c
 * @brief   Host-side simulation of the UIUCTag RUNNING acquisition sequencer.
 *
 * @details Compiles the real embedded/tags/UIUCTag/src/state_run.c, by
 *          including it directly, against the minimal stubs in test/stub and
 *          drives Running() over a synthetic minute-alarm timeline. A fake
 *          external flash records every record write and asserts on the faults
 *          that are expensive to detect on hardware: programming a byte twice,
 *          a write that is not one whole aligned record, and writing while the
 *          chip is in deep power-down.
 *
 *          This checks the part of the design that is pure arithmetic over the
 *          acquisition clock - which record a minute lands in, which minute
 *          its activity covers, when a block rolls over - and the recovery
 *          paths that are slow to reproduce on a tag: a reset mid-block, a
 *          hibernation window spanning several block windows, and a reattach.
 *
 *          It deliberately does not model sensors, buses, power, or timing. Its
 *          value is that the sequencer's decisions are separable from all of
 *          that; see the test strategy document for what only hardware can
 *          answer.
 *
 * @note    Not part of any build. Build and run instructions are in
 *          test/README.md.
 *
 * @see     ../design/test-strategy.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "stub/tag.pb.h"
#include "stub/config.h"
#include "stub/persistent.h"
#include "uiuctag_log_format.h"
#include "datalog.h"

/* --- pieces state_run.c expects from the runtime ------------------------- */
enum Sleep { SLEEP, STANDBY, SHUTDOWN };
enum StateTrans { T_INIT, T_CONT, T_ERROR };
#define EVT_RTC_ALRAF 0x100u
#define LINE_WKUP1 0
#define ALARM_MINUTE 1

int32_t timestamp;
uint32_t events;
bool isActive;
t_storedconfig sconfig;
static BackupState backup;
volatile BackupState *const pState = &backup;

/* --- the timeline -------------------------------------------------------- */
/** An arbitrary epoch; the run starts 137 s after it, mid-minute. */
static const int32_t BLOCK0 = 1767225600;
/** The run's first minute boundary: record 0, and checkpoint 0's epoch. */
static const int32_t ANCHOR = 1767225600 + 180;

/* --- recording of everything the sequencer writes ------------------------ */
typedef struct { int32_t at; uint32_t index; t_UIUCTagRecord record; } Write;
static Write writes[8192];
static int write_count;
typedef struct { int32_t at; int32_t epoch; uint32_t block; } Checkpoint;
static Checkpoint checkpoints[512];
static int checkpoint_count;
static bool flash_awake;
static int pressure_samples;

/* --- simulated external flash, byte for byte ----------------------------- */
#define MAX_RECORDS 4096
static uint8_t flash[MAX_RECORDS * UIUCTAG_RECORD_SIZE];
static bool flash_written[MAX_RECORDS * UIUCTAG_RECORD_SIZE];

void dataLogWriteBegin(void) { assert(!flash_awake); flash_awake = true; }
void dataLogWriteEnd(void) { assert(flash_awake); flash_awake = false; }

enum LOGERR dataLogWriteRecord(uint32_t index, const t_UIUCTagRecord *record)
{
    const uint32_t address = index * UIUCTAG_RECORD_SIZE;

    assert(flash_awake && "record written with flash asleep");
    assert(index < MAX_RECORDS);
    /* One whole record, inside one page. */
    assert((address % UIUCTAG_RECORD_SIZE) == 0);
    assert((address / 256u) == ((address + UIUCTAG_RECORD_SIZE - 1u) / 256u));
    /* NOR flash cannot rewrite a programmed byte: catch double writes. */
    for (unsigned i = 0; i < UIUCTAG_RECORD_SIZE; i++) {
        assert(!flash_written[address + i] && "byte programmed twice");
        flash_written[address + i] = true;
    }
    memcpy(&flash[address], record, UIUCTAG_RECORD_SIZE);
    writes[write_count++] = (Write){timestamp, index, *record};
    return LOGWRITE_OK;
}

enum LOGERR writeDataHeader(t_DataHeader *head)
{
    checkpoints[checkpoint_count++] = (Checkpoint){timestamp, head->epoch,
                                                   head->extern_log_block};
    pState->pages++;
    return LOGWRITE_OK;
}

bool readDataHeader(int index, t_DataHeader *header)
{
    if (index < 0 || index >= checkpoint_count) return false;
    header->epoch = checkpoints[index].epoch;
    header->vdd100 = 300;
    header->extern_log_block = (uint16_t)checkpoints[index].block;
    return true;
}

/* --- other runtime stubs ------------------------------------------------- */
static int finished_count, hibernating_count;
enum Sleep Finished(enum StateTrans t, State_Event r) { (void)t; (void)r; finished_count++; return SHUTDOWN; }
enum Sleep Hibernating(enum StateTrans t, State_Event r) { (void)t; (void)r; hibernating_count++; return SHUTDOWN; }
enum Sleep Aborted(enum StateTrans t, State_Event r) { (void)t; (void)r; return SHUTDOWN; }
/* The minute alarm is all that wakes RUNNING, so its state is tracked. */
static bool minute_alarm_armed;
void disableAllAlarms(void) { minute_alarm_armed = false; }
void disableTicker(void) {}
void enableAlarm(unsigned int a, int type) { minute_alarm_armed = (a == 0) && (type == ALARM_MINUTE); }
void adcVDD(uint16_t *vdd100, int16_t *temp10) { *vdd100 = 300; *temp10 = 250; }
void recordState(State_Event reason) { (void)reason; }
void initDataCollection(void) {}
int palReadLine(int line) { (void)line; return isActive ? 1 : 0; }

/**
 * @brief Stand-in for the BMP585: a value derived from the minute.
 *
 * @details Pressure is (1000 + n) hPa and temperature (20 + n/256) degC for
 *          the n-th minute after the anchor, so a decoded value names the
 *          minute it came from. Minute 7 fails, as a DRDY timeout would.
 */
bool samplePressureRaw(t_UIUCTagRecord *record)
{
    const uint32_t n = (uint32_t)((timestamp - ANCHOR) / 60);

    pressure_samples++;
    if (n == 7) {
        uiuctagPut24(record->pressure, UIUCTAG_RAW_FAILED);
        uiuctagPut24(record->temperature, UIUCTAG_RAW_FAILED);
        return false;
    }
    uiuctagPut24(record->pressure, (1000u + n) * 6400u);
    uiuctagPut24(record->temperature, (20u * 65536u + n * 256u) & 0xFFFFFFu);
    return true;
}

#include "state_run.c"

/* --- the simulation ------------------------------------------------------ */

/**
 * @brief Find the write of one record.
 *
 * @param[in] index Global record index.
 * @return Position in writes[], or -1 when the record was never written.
 */
static int find_write(uint32_t index)
{
    for (int i = 0; i < write_count; i++)
        if (writes[i].index == index) return i;
    return -1;
}

/**
 * @brief Global record index of a minute on the original grid.
 *
 * @param[in] t Epoch second of a minute boundary at or after ANCHOR.
 * @return Record index, valid while no block has been re-anchored.
 */
static uint32_t index_at(int32_t t)
{
    return (uint32_t)((t - ANCHOR) / 60);
}

int main(int argc, char **argv)
{
    /* Where to dump the simulated flash images for the host end-to-end check. */
    const char *block_output_path =
        (argc > 1) ? argv[1] : "uiuc_sim_blocks.bin";

    memset(&backup, 0, sizeof(backup));
    memset(flash, 0xFF, sizeof(flash));
    sconfig.stop = BLOCK0 + 100000;

    /* Start mid-minute, deliberately off any boundary. */
    timestamp = BLOCK0 + 137;
    events = 0;
    Running(T_INIT, 0);
    assert(checkpoint_count == 0 && "no block should open before a record");

    /* Minute alarms for three hours, anchored at block0+180. Activity: the
       whole minute [+3780, +3840), then 30 s from +3930 to +3960. */
    for (int32_t t = ANCHOR; t <= BLOCK0 + 10800; t += 60) {
        timestamp = t;
        events = EVT_RTC_ALRAF;
        isActive = (t == BLOCK0 + 3780) || (t == BLOCK0 + 3900);
        Running(T_CONT, 0);
        /* lastactstart carries "active since" across wakes; emulate an edge
           wake 30 s into the minute. */
        if (t == BLOCK0 + 3900) { pState->lastactstart = t + 30; }
    }
    printf("after 3 h: readings=%d writes=%d checkpoints=%d\n",
           pressure_samples, write_count, checkpoint_count);

    /* --- one record per minute, one checkpoint per 32 --- */
    const uint32_t records = index_at(BLOCK0 + 10800) + 1;     /* 178 */
    assert(write_count == (int)records && pressure_samples == (int)records);
    assert(checkpoint_count == (int)((records + 31) / 32));
    for (int c = 0; c < checkpoint_count; c++) {
        assert(checkpoints[c].block == (uint32_t)c);
        assert(checkpoints[c].epoch == ANCHOR + c * 1920 &&
               "each block starts exactly 32 minutes after the previous one");
    }
    for (int i = 0; i < write_count; i++)
        assert(writes[i].index == index_at(writes[i].at) &&
               "record n is written at anchor + 60 n");
    printf("%u records in %d blocks of 32, checkpoints 1920 s apart\n",
           records, checkpoint_count);

    /* --- the first record's minute was not observed; the rest were --- */
    {
        int w = find_write(0);
        assert(w >= 0 && writes[w].record.activity == UIUCTAG_ACTIVITY_UNOBSERVED_WORD);
        for (int i = 1; i < write_count; i++)
            assert(writes[i].record.activity != UIUCTAG_ACTIVITY_UNOBSERVED_WORD &&
                   uiuctagRecordHasActivity(&writes[i].record));
    }

    /* --- failed reading stores the failure value, still a written record --- */
    {
        int w = find_write(7);
        assert(w >= 0);
        assert(!uiuctagRecordErased(&writes[w].record));
        assert(!uiuctagRecordHasReading(&writes[w].record));
        assert(uiuctagRecordHasActivity(&writes[w].record));
        int ok = find_write(8);
        assert(ok >= 0 && uiuctagRecordHasReading(&writes[ok].record));
        assert(uiuctagRecordPressureHpa(&writes[ok].record) == 1008.0);
    }

    /* --- activity covers the minute before the record --- */
    {
        int w = find_write(index_at(BLOCK0 + 3840));
        const t_UIUCTagRecord *r = &writes[w].record;
        printf("record at +3840: buckets %u,%u,%u\n", uiuctagActivityBucket(r, 0),
               uiuctagActivityBucket(r, 1), uiuctagActivityBucket(r, 2));
        assert(uiuctagActivityBucket(r, 0) == 20 && uiuctagActivityBucket(r, 1) == 20 &&
               uiuctagActivityBucket(r, 2) == 20 && "a whole active minute");
        w = find_write(index_at(BLOCK0 + 3960));
        r = &writes[w].record;
        printf("record at +3960: buckets %u,%u,%u\n", uiuctagActivityBucket(r, 0),
               uiuctagActivityBucket(r, 1), uiuctagActivityBucket(r, 2));
        assert(uiuctagActivityBucket(r, 0) == 0 && uiuctagActivityBucket(r, 1) == 10 &&
               uiuctagActivityBucket(r, 2) == 20 && "30 s from 10 s into bucket 1");
        w = find_write(index_at(BLOCK0 + 3900));
        assert(writes[w].record.activity == 0 && "an inactive minute is zero");
    }

    /* --- stop time ends the run without a further record --- */
    sconfig.stop = BLOCK0 + 10800;
    timestamp = BLOCK0 + 10860;
    events = EVT_RTC_ALRAF;
    Running(T_CONT, 0);
    assert(finished_count == 1 && write_count == (int)records);
    printf("stop time honoured\n");

    /* ---------------------------------------------------------------- */
    /* Phase 2: reset mid-block. RUNNING is re-entered through T_INIT with
       the flash checkpoints intact but volatile progress lost. */
    /* ---------------------------------------------------------------- */
    {
        int checkpoints_before = checkpoint_count;
        sconfig.stop = BLOCK0 + 100000;

        timestamp = BLOCK0 + 10890;                  /* mid-minute */
        Running(T_INIT, 0);
        assert(checkpoint_count == checkpoints_before &&
               "re-entry must not open a block on its own");

        timestamp = BLOCK0 + 10920;
        events = EVT_RTC_ALRAF;
        isActive = false;
        Running(T_CONT, 0);
        int w = find_write(index_at(BLOCK0 + 10920));
        assert(w >= 0 && "the grid resumes at the next minute");
        assert(find_write(index_at(BLOCK0 + 10860)) < 0 &&
               "the minute missed during the reset stays erased");
        assert(writes[w].record.activity == UIUCTAG_ACTIVITY_UNOBSERVED_WORD &&
               "a minute spanning a reset is not observed, not guessed");
        assert(checkpoint_count == checkpoints_before &&
               "an unfilled block is reused, not replaced");

        timestamp = BLOCK0 + 10980;
        Running(T_CONT, 0);
        w = find_write(index_at(BLOCK0 + 10980));
        assert(w >= 0 && writes[w].record.activity == 0 &&
               "the next whole minute is observed again");
        printf("reset mid-block: grid resumed, missed minute erased, "
               "first minute unobserved\n");
    }

    /* ---------------------------------------------------------------- */
    /* Phase 3: hibernation window, then resume in a later block window. */
    /* ---------------------------------------------------------------- */
    int32_t resumed_at;
    {
        int checkpoints_before = checkpoint_count;

        sconfig.hibernate[0].start_epoch = BLOCK0 + 11040;
        sconfig.hibernate[0].end_epoch = BLOCK0 + 25200;

        timestamp = BLOCK0 + 11040;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        assert(hibernating_count == 1 && "must hibernate at a minute boundary");

        /* Resume well after the window: the grid has run far past the block's
           last record, so the run re-anchors on a fresh block. */
        timestamp = BLOCK0 + 25200;
        Running(T_INIT, 0);
        resumed_at = BLOCK0 + 25260;
        timestamp = resumed_at;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        assert(checkpoint_count == checkpoints_before + 1);
        assert(checkpoints[checkpoint_count - 1].block ==
               checkpoints[checkpoints_before - 1].block + 1 &&
               "a resumed run takes the next external block");
        assert(checkpoints[checkpoint_count - 1].epoch == resumed_at &&
               "a resumed block is anchored at its own first record");
        int w = find_write(checkpoints[checkpoint_count - 1].block * 32u);
        assert(w >= 0 && writes[w].at == resumed_at);
        assert(writes[w].record.activity == UIUCTAG_ACTIVITY_UNOBSERVED_WORD);
        printf("hibernate/resume: new block %u anchored at +%d\n",
               checkpoints[checkpoint_count - 1].block, resumed_at - BLOCK0);
    }

    /* ---------------------------------------------------------------- */
    /* Phase 4: reattach. An attach that lands while the tag is awake boots
       as a power init, deviceInit() turns every alarm off, and recovery
       resumes through T_CONT with POWERFAIL. The handler must re-arm the
       minute alarm, or the run stays RUNNING and stores nothing more. */
    /* ---------------------------------------------------------------- */
    {
        int writes_before = write_count;
        int checkpoints_before = checkpoint_count;

        assert(minute_alarm_armed && "a running tag has its minute alarm");

        /* A wake from Standby (State_EVENT_OK) keeps the alarm untouched. */
        timestamp = resumed_at + 30;
        events = 0;
        Running(T_CONT, 0);
        assert(minute_alarm_armed);

        disableAllAlarms();           /* deviceInit() on a power-init boot */
        timestamp = resumed_at + 40;  /* between minutes, no alarm event */
        events = 0;
        Running(T_CONT, State_EVENT_POWERFAIL);
        assert(minute_alarm_armed &&
               "a reattach must re-arm the minute alarm");
        assert(write_count == writes_before &&
               checkpoint_count == checkpoints_before &&
               "re-arming must not write a record or open a block");

        /* The resumed run writes at its next minute, in the same block, and
           pState carried the minute's activity across the reattach. */
        timestamp = resumed_at + 60;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        assert(write_count == writes_before + 1 && "the resumed run collects");
        assert(checkpoint_count == checkpoints_before &&
               "a reattach continues its block");
        assert(writes[write_count - 1].index ==
               checkpoints[checkpoint_count - 1].block * 32u + 1u);
        printf("reattach: minute alarm re-armed, collection resumed\n");
    }

    /* ---------------------------------------------------------------- */
    /* Emit the simulated external blocks and their checkpoints so the host
       decoder can be run against firmware-produced bytes. */
    /* ---------------------------------------------------------------- */
    {
        FILE *f = fopen(block_output_path, "wb");
        assert(f != NULL);
        for (int c = 0; c < checkpoint_count; c++) {
            int32_t epoch = checkpoints[c].epoch;
            uint16_t vdd100 = 300;
            fwrite(&epoch, sizeof(epoch), 1, f);
            fwrite(&vdd100, sizeof(vdd100), 1, f);
            fwrite(&flash[checkpoints[c].block * UIUCTAG_BLOCK_BYTES],
                   UIUCTAG_BLOCK_BYTES, 1, f);
        }
        fclose(f);
        printf("wrote %d simulated blocks to %s\n", checkpoint_count,
               block_output_path);
    }

    printf("\nFIRMWARE SEQUENCER SIM: all assertions passed\n");
    return 0;
}
