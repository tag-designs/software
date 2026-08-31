/**
 * @file    sequencer_sim.c
 * @brief   Host-side simulation of the UIUCTag RUNNING acquisition sequencer.
 *
 * @details Compiles the real embedded/tags/UIUCTag/src/state_run.c, by
 *          including it directly, against the minimal stubs in test/stub and
 *          drives Running() over a synthetic minute-alarm timeline. A fake
 *          external flash records every field write and asserts on the two
 *          faults that are expensive to detect on hardware: programming a NOR
 *          word twice, and writing while the chip is in deep power-down.
 *
 *          This checks the part of the design that is pure arithmetic over the
 *          acquisition clock - which slot a sample lands in, when its activity
 *          word follows, when a block rolls over - and the recovery paths that
 *          are slow to reproduce on a tag: a reset mid-block, and a hibernation
 *          window spanning several block windows.
 *
 *          It deliberately does not model sensors, buses, power, or timing. Its
 *          value is that the sequencer's decisions are separable from all of
 *          that; see the test strategy document for what only hardware can
 *          answer.
 *
 * @note    Not part of any build. Build and run instructions are in
 *          test/README.md.
 *
 * @see     ../../families/BitPresTag/design/uiuctag-test-strategy.md
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

/* --- recording of everything the sequencer writes ------------------------ */
typedef struct { int32_t at; uint32_t index; uint32_t field; uint32_t word; } Write;
static Write writes[8192];
static int write_count;
typedef struct { int32_t at; int32_t epoch; uint32_t block; } Checkpoint;
static Checkpoint checkpoints[512];
static int checkpoint_count;
static bool flash_awake;
static int pressure_samples;
static bool pressure_fails_at_slot7;

/* --- simulated flash: one word per (index, field) ------------------------ */
#define MAX_SAMPLES 4096
static uint32_t flash[MAX_SAMPLES][3];
static bool flash_written[MAX_SAMPLES][3];

void dataLogWriteBegin(void) { assert(!flash_awake); flash_awake = true; }
void dataLogWriteEnd(void) { assert(flash_awake); flash_awake = false; }

enum LOGERR dataLogWriteField(uint32_t sample_index, uint32_t field_offset,
                              const void *word)
{
    assert(flash_awake && "field written with flash asleep");
    assert(sample_index < MAX_SAMPLES);
    unsigned f = field_offset / 4;
    assert(f < 3);
    /* NOR flash cannot rewrite a programmed word: catch double writes. */
    assert(!flash_written[sample_index][f] && "field programmed twice");
    memcpy(&flash[sample_index][f], word, 4);
    flash_written[sample_index][f] = true;
    writes[write_count++] = (Write){timestamp, sample_index, field_offset,
                                    flash[sample_index][f]};
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
void disableAllAlarms(void) {}
void disableTicker(void) {}
void enableAlarm(unsigned int a, int type) { (void)a; (void)type; }
void adcVDD(uint16_t *vdd100, int16_t *temp10) { *vdd100 = 300; *temp10 = 250; }
void recordState(State_Event reason) { (void)reason; }
void initDataCollection(void) {}
int palReadLine(int line) { (void)line; return isActive ? 1 : 0; }

bool samplePressure(float *pressure_hpa, float *temperature_c)
{
    unsigned slot = (unsigned)((timestamp % (int32_t)UIUCTAG_DATA_LOG_SECONDS)
                               / (int32_t)UIUCTAG_EXTERNAL_BLOCK_SECONDS);
    pressure_samples++;
    if (pressure_fails_at_slot7 && slot == 7) {
        *pressure_hpa = __builtin_nanf("");
        *temperature_c = __builtin_nanf("");
        return false;
    }
    *pressure_hpa = 1000.0f + slot;
    *temperature_c = 20.0f + slot;
    return true;
}

#include "state_run.c"

/* --- the simulation ------------------------------------------------------ */
static const int32_t BLOCK0 = 1767225600;   /* multiple of 7200 */

/*
 * The expected slot indices, checkpoint epochs, and activity words below are
 * written for the shipping five-minute period. The invariants that hold at any
 * period - no word programmed twice, no write while the flash sleeps, every
 * sampled slot getting both pressure and temperature - stay active either way,
 * so building this with -DUIUCTAG_SAMPLE_PERIOD_SEC set still exercises the
 * addressing arithmetic that a shortened period is most likely to break.
 */
#if UIUCTAG_SAMPLE_PERIOD_SEC == UIUCTAG_EXTERNAL_BLOCK_SECONDS
#define SIM_DEFAULT_GEOMETRY 1
#else
#define SIM_DEFAULT_GEOMETRY 0
#endif

/**
 * @brief Unpack an activity bucket from a recorded activity word.
 *
 * @details Goes through the shared accessor rather than re-deriving the shift,
 *          so the simulation cannot agree with a bug in the packing.
 *
 * @param[in] word Recorded packed activity word.
 * @param[in] b Bucket index.
 * @return Active seconds in that bucket.
 */
static uint32_t bucket(uint32_t word, unsigned b)
{
    t_UIUCTagSample sample;
    memset(&sample, 0xFF, sizeof(sample));
    sample.packed_activity_data = word;
    return uiuctagActivityBucket(&sample, b);
}

static int find_write(uint32_t index, uint32_t field)
{
    for (int i = 0; i < write_count; i++)
        if (writes[i].index == index && writes[i].field == field) return i;
    return -1;
}

int main(int argc, char **argv)
{
    /* Where to dump the simulated flash images for the host end-to-end check. */
    const char *block_output_path =
        (argc > 1) ? argv[1] : "uiuc_sim_blocks.bin";

    memset(&backup, 0, sizeof(backup));
    sconfig.stop = BLOCK0 + 100000;
    sconfig.hibernate[0].start_epoch = 0;
    sconfig.hibernate[0].end_epoch = 0;
    sconfig.hibernate[1].start_epoch = 0;
    sconfig.hibernate[1].end_epoch = 0;
    pressure_fails_at_slot7 = true;

    /* Start mid-minute, deliberately off any boundary. */
    timestamp = BLOCK0 + 137;
    events = 0;
    Running(T_INIT, 0);
    printf("T_INIT at block0+137: checkpoints=%d writes=%d\n",
           checkpoint_count, write_count);
    assert(checkpoint_count == 0 && "no block should open before a sample");

    /* Minute alarms for three hours. The grid is anchored at the first minute
       boundary of the run, block0+180, so sample boundaries are +180, +480,
       ... and the sample covering [+3780, +4080) is the one exercised below:
       the whole minute at +3780 fills its bucket 0, and 30 s from +3900 fills
       half of its bucket 2. */
    for (int32_t t = BLOCK0 + 180; t <= BLOCK0 + 10800; t += 60) {
        timestamp = t;
        events = EVT_RTC_ALRAF;
        isActive = false;
        if (t == BLOCK0 + 3780) { isActive = true; }        /* active from here */
        if (t == BLOCK0 + 3840) { isActive = false; }       /* 60 s of activity */
        if (t == BLOCK0 + 3900) { isActive = true; }
        Running(T_CONT, 0);
        /* lastactstart is what carries "active since" across wakes; emulate 30 s
           of activity starting mid-minute the way an edge wake would. */
        if (t == BLOCK0 + 3900) { pState->lastactstart = t + 30; }
        if (t == BLOCK0 + 3960) { pState->lastactstart = INT_MAX; }
    }

    printf("after 3 h: pressure_samples=%d writes=%d checkpoints=%d\n",
           pressure_samples, write_count, checkpoint_count);

    /* --- checkpoint expectations --- */
#if SIM_DEFAULT_GEOMETRY
    /* The run's first minute boundary is block0+180, and that instant is both
       checkpoint 0 and its own slot 0. Every later block starts exactly one
       block period after the previous one, so the grid never depends on where
       the run happened to begin relative to absolute time. */
    const int32_t ANCHOR = BLOCK0 + 180;
    assert(checkpoint_count == 2);
    assert(checkpoints[0].epoch == ANCHOR && checkpoints[0].block == 0);
    assert(checkpoints[1].epoch == ANCHOR + 7200 && checkpoints[1].block == 1);
    printf("checkpoint 0: epoch=block0+%d block=%u (run anchor)\n",
           checkpoints[0].epoch - BLOCK0, checkpoints[0].block);
    printf("checkpoint 1: epoch=block0+%d block=%u (anchor + one block)\n",
           checkpoints[1].epoch - BLOCK0, checkpoints[1].block);

    /* --- slot addressing, measured from each block's own start --- */
    /* The anchor sample itself is slot 0 of block 0 -> index 0. */
    int w = find_write(0, DATALOG_FIELD_PRESSURE);
    assert(w >= 0 && writes[w].at == ANCHOR);
    /* One period later is slot 1 -> index 1. */
    w = find_write(1, DATALOG_FIELD_PRESSURE);
    assert(w >= 0 && writes[w].at == ANCHOR + 300);
    /* The 24th sample opens block 1 as its slot 0 -> index 24. */
    w = find_write(24, DATALOG_FIELD_PRESSURE);
    assert(w >= 0 && writes[w].at == ANCHOR + 7200);
    printf("slot addressing: index 0 @anchor, index 1 @anchor+300, "
           "index 24 @anchor+7200\n");

    /* --- failed conversion stores NaN, and only NaN --- */
    /* slot 7 of block 0 = index 7, written one period per slot after the
       anchor. */
    w = find_write(7, DATALOG_FIELD_PRESSURE);
    assert(w >= 0 && writes[w].at == ANCHOR + 7 * 300);
    {
        float p; memcpy(&p, &writes[w].word, 4);
        assert(uiuctagFloatMissing(p) && "failed sample must store NaN");
        assert(writes[w].word != UIUCTAG_ERASED_WORD &&
               "stored NaN must be distinguishable from erased flash");
    }
    printf("failed conversion at index 7 stored 0x%08x (canonical NaN)\n",
           writes[w].word);

    /* --- activity is written one sample period late, into the right slot --- */
    /* The sample at anchor+3600 is slot 12 of block 0, covering
       [anchor+3600, anchor+3900) = [block0+3780, block0+4080). Its activity
       word must be programmed one period later, at anchor+3900. */
    w = find_write(12, DATALOG_FIELD_ACTIVITY);
    assert(w >= 0);
    assert(writes[w].at == ANCHOR + 3900 && "activity lags its sample by one period");
    printf("activity for slot 12 written at +%d, word=0x%08x buckets=%u,%u,%u,%u,%u\n",
           writes[w].at - BLOCK0, writes[w].word,
           bucket(writes[w].word, 0), bucket(writes[w].word, 1),
           bucket(writes[w].word, 2), bucket(writes[w].word, 3),
           bucket(writes[w].word, 4));
    assert(bucket(writes[w].word, 0) == 60 && "full minute of activity");
    assert(bucket(writes[w].word, 1) == 0);
    assert(bucket(writes[w].word, 2) == 30 && "half minute of activity");
    assert(bucket(writes[w].word, 3) == 0);
    assert(bucket(writes[w].word, 4) == 0);

#endif /* SIM_DEFAULT_GEOMETRY */

    /* --- period-independent invariants: every written sample got exactly
           pressure+temperature, and every completed sample also got activity --- */
    int with_pressure = 0, with_activity = 0;
    for (unsigned i = 0; i < MAX_SAMPLES; i++) {
        if (flash_written[i][0]) {
            with_pressure++;
            assert(flash_written[i][1] && "temperature must accompany pressure");
        }
        if (flash_written[i][2]) {
            with_activity++;
            assert(flash_written[i][0] && "activity only for a sampled slot");
        }
    }
    printf("samples with pressure=%d with activity=%d\n", with_pressure, with_activity);
    assert(with_pressure == pressure_samples);
    assert(with_activity == with_pressure - 1 && "newest sample has no activity yet");

    /* --- stop time ends the run --- */
    sconfig.stop = BLOCK0 + 10800;
    timestamp = BLOCK0 + 10860;
    events = EVT_RTC_ALRAF;
    Running(T_CONT, 0);
    assert(finished_count == 1);
    printf("stop time honoured: finished=%d\n", finished_count);

#if SIM_DEFAULT_GEOMETRY
    /* ---------------------------------------------------------------- */
    /* Phase 2: reset mid-block. The state machine re-enters RUNNING via
       T_INIT with the flash checkpoints intact but volatile progress lost. */
    /* ---------------------------------------------------------------- */
    {
        int writes_before = write_count;
        int checkpoints_before = checkpoint_count;
        finished_count = 0;
        sconfig.stop = BLOCK0 + 100000;

        /* Reset between two grid points, deliberately off the sample grid. */
        timestamp = ANCHOR + 10860;
        Running(T_INIT, 0);
        assert(checkpoint_count == checkpoints_before &&
               "re-entry must not open a block on its own");
        assert(write_count == writes_before);

        /* A wake that is minute aligned but off the grid must not sample. */
        timestamp = ANCHOR + 10920;
        events = EVT_RTC_ALRAF;
        isActive = false;
        Running(T_CONT, 0);
        assert(write_count == writes_before &&
               "an off-grid wake must not write a sample");

        /* The next grid point resumes the original grid: slot 12 of block 1. */
        timestamp = ANCHOR + 11100;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        int w2 = find_write(24 + 13, DATALOG_FIELD_PRESSURE);
        assert(w2 >= 0 && writes[w2].at == ANCHOR + 11100);
        assert(checkpoint_count == checkpoints_before &&
               "an unfilled block must be reused, not replaced");
        /* The sample written before the reset keeps its erased activity word:
           its accumulation was lost, so nothing is invented for it. */
        assert(!flash_written[24 + 12][2] &&
               "activity spanning a reset must stay unwritten, not guessed");
        printf("reset mid-block: off-grid wake ignored, grid resumed at "
               "index %d, prior activity left erased\n", 24 + 13);
    }

    /* ---------------------------------------------------------------- */
    /* Phase 3: hibernation window, then resume in a later block window. */
    /* ---------------------------------------------------------------- */
    {
        int checkpoints_before = checkpoint_count;
        hibernating_count = 0;

        sconfig.hibernate[0].start_epoch = ANCHOR + 11400;
        sconfig.hibernate[0].end_epoch = ANCHOR + 25200;

        timestamp = ANCHOR + 11400;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        assert(hibernating_count == 1 && "must hibernate at a sample boundary");

        /* Resume well after the window: the grid has run far past the block's
           last slot, so the resumed run re-anchors on a fresh block rather than
           addressing slots it skipped. */
        timestamp = ANCHOR + 25200;
        Running(T_INIT, 0);
        /* ANCHOR + 25500 is the next point on the grid the old checkpoint
           defines; a resumed run samples there, not at the first minute wake. */
        timestamp = ANCHOR + 25500;
        events = EVT_RTC_ALRAF;
        Running(T_CONT, 0);
        assert(checkpoint_count == checkpoints_before + 1);
        assert(checkpoints[checkpoint_count - 1].block ==
               checkpoints[checkpoints_before - 1].block + 1 &&
               "resumed run must take the next external block");
        assert(checkpoints[checkpoint_count - 1].epoch == ANCHOR + 25500 &&
               "a resumed block is anchored at its own first sample");
        printf("hibernate/resume: new block %u anchored at anchor+%d\n",
               checkpoints[checkpoint_count - 1].block,
               checkpoints[checkpoint_count - 1].epoch - ANCHOR);
    }

#endif /* SIM_DEFAULT_GEOMETRY */

    /* ---------------------------------------------------------------- */
    /* Emit the simulated external blocks and their checkpoints so the host
       decoder can be run against firmware-produced bytes. */
    /* ---------------------------------------------------------------- */
    {
        FILE *f = fopen(block_output_path, "wb");
        assert(f != NULL);
        for (int c = 0; c < checkpoint_count; c++) {
            uint8_t image[UIUCTAG_SAMPLE_BYTES_MAX];
            uint32_t base = checkpoints[c].block * UIUCTAG_LOG_SAMPLES;

            memset(image, 0xFF, sizeof(image));   /* erased flash */
            for (unsigned slot = 0; slot < UIUCTAG_LOG_SAMPLES; slot++) {
                for (unsigned field = 0; field < 3; field++) {
                    if (flash_written[base + slot][field]) {
                        memcpy(&image[slot * UIUCTAG_SAMPLE_SIZE + field * 4],
                               &flash[base + slot][field], 4);
                    }
                }
            }
            int32_t epoch = checkpoints[c].epoch;
            uint16_t vdd100 = 300;
            fwrite(&epoch, sizeof(epoch), 1, f);
            fwrite(&vdd100, sizeof(vdd100), 1, f);
            fwrite(image, sizeof(image), 1, f);
        }
        fclose(f);
        printf("wrote %d simulated blocks to %s\n", checkpoint_count,
               block_output_path);
    }

    printf("\nFIRMWARE SEQUENCER SIM: all assertions passed (%s)\n",
           SIM_DEFAULT_GEOMETRY ? "default geometry"
                                : "invariants only, period overridden");
    return 0;
}
