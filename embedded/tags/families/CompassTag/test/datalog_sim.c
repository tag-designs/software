/**
 * @file    datalog_sim.c
 * @brief   Host-side simulation of CompassTag logging, resume and download.
 *
 * @details Compiles the real ../src/state_run.c and ../src/datalog.c, by
 *          including them directly, against the minimal stubs in test/stub.
 *          It drives Running() over a synthetic 30 s ticker against a fake
 *          external NOR and a fake internal header array, then downloads every
 *          page through the real data_logAck().
 *
 *          It checks two fixes on the firmware-fix branch that are slow to
 *          reproduce on a tag:
 *
 *          - A1: a resume (restoreLog() after a reset, then Running(T_INIT))
 *            puts the external cursor at pages * DATALOG_PAGE_WORDS, the start
 *            of the next page in 16-bit words. fw-v0.0.3 used pages * 30, which
 *            pointed back into page 0; the fake NOR asserts on programming a
 *            byte that is not erased, so that bug aborts the run here.
 *          - A3: when the external flash fills, the final page, cut short by
 *            the end of the part, is downloaded and holds exactly the blocks
 *            that were completed.
 *
 *          Every sample carries its sequence number in ax/ay, so the download
 *          is checked sample by sample against what was written.
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

#include "../src/datalog.c"
#include "../src/state_run.c"

/* --- runtime globals the sources expect ---------------------------------- */
int32_t timestamp;
uint32_t events;
bool isActive;
t_storedconfig sconfig;
static BackupState backup;
volatile BackupState *const pState = &backup;
stub_flash_t stub_flash;
stub_rcc_t stub_rcc;
stub_tim_t stub_tim = {1};
const TagStorageDevice tagExternalFlash;
const TagRegisterDevice tagCompassTagAccelDevice;
t_StateMarker sEpoch[sEPOCH_SIZE];

/* --- simulated internal flash: the header array and its end -------------- */
#define MAX_HEADERS 30000
t_DataHeader vddHeader[MAX_HEADERS];
uint32_t *sim_persistent_end = (uint32_t *)&vddHeader[MAX_HEADERS];

void FLASH_Lock(void) {}
void FLASH_Unlock(void) {}
void FLASH_Flush_Data_Cache(void) {}

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
        /* NOR cannot rewrite a programmed byte; a cursor that points back
           into written data lands here. */
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
static State_Event finished_reason;
static uint32_t sample_seq;

enum Sleep Finished(enum StateTrans t, State_Event r) { (void)t; finished_count++; finished_reason = r; return SHUTDOWN; }
enum Sleep Hibernating(enum StateTrans t, State_Event r) { (void)t; (void)r; assert(!"unexpected hibernation"); return SHUTDOWN; }
enum Sleep Aborted(enum StateTrans t, State_Event r) { (void)t; (void)r; assert(!"unexpected abort"); return SHUTDOWN; }
void disableAllAlarms(void) {}
void disableTicker(void) {}
void enableTicker(uint16_t interval) { assert(interval == COMPASS_SAMPLE_PERIOD_S); }
void adcVDD(uint16_t *vdd100, int16_t *temp10) { *vdd100 = 300; *temp10 = -55; }
bool tagCompassAccelWakeActive(void) { return false; }
void recordState(State_Event reason) { (void)reason; }
void lis2du12Init(const TagRegisterDevice *d, lis2du12mode_t m) { (void)d; (void)m; }
void lis2du12Deinit(const TagRegisterDevice *d) { (void)d; }
int encode_ack(void) { return 0; }

/* Each sample carries its sequence number: ax = seq % 30000, ay = seq / 30000. */
bool sensorSample(RawSensorData *data)
{
    memset(data, 0, sizeof(*data));
    data->ax = (int16_t)(sample_seq % 30000u);
    data->ay = (int16_t)(sample_seq / 30000u);
    data->mx = 7;
    sample_seq++;
    return true;
}

/* --- harness ------------------------------------------------------------- */

/** Erase everything and set the external flash to @p bytes. */
static void reset_world(uint32_t bytes)
{
    assert(bytes % SECTOR_SIZE == 0 && bytes <= MAX_FLASH);
    /* datalog.c compares header addresses as uint32_t, as it may on the
       32-bit tag. On a 64-bit host that is sound only while the array does
       not straddle a 4 GiB boundary. */
    assert(((uintptr_t)&vddHeader[0] >> 32) ==
           ((uintptr_t)sim_persistent_end >> 32));
    sector_count = (int)(bytes / SECTOR_SIZE);
    memset(nor, 0xFF, sizeof(nor));
    memset(vddHeader, 0xFF, sizeof(vddHeader));
    memset(sEpoch, 0xFF, sizeof(sEpoch));
    memset(&backup, 0, sizeof(backup));
    sconfig.stop = INT_MAX;
    memset(sconfig.hibernate, 0, sizeof(sconfig.hibernate));
    timestamp = 1000000;
    events = 0;
    sample_seq = 0;
    finished_count = 0;
}

/** One ticker wakeup; returns true while still running. */
static bool tick(void)
{
    timestamp += COMPASS_SAMPLE_PERIOD_S;
    events = EVT_RTC_WUTF;
    Running(T_CONT, 0);
    events = 0;
    return finished_count == 0;
}

/** Simulate a reset: lose RAM state, keep flash and the backup registers'
    absence, and resume the way the boot path does. */
static void reset_and_resume(void)
{
    memset(&backup, 0, sizeof(backup));
    (void)restoreLog();
}

static uint32_t decode_seq(const CompassTagLog_Compass *c)
{
    return (uint32_t)lroundf(c->ax / 0.976f) +
           30000u * (uint32_t)lroundf(c->ay / 0.976f);
}

/**
 * Download page @p index and check its samples run consecutively from
 * @p first_seq. Returns data_count, or -1 when the page is not served.
 */
static int download(int index, uint32_t first_seq)
{
    static Ack ack;
    memset(&ack, 0, sizeof(ack));
    data_logAck(index, &ack);
    if (ack.which_payload != Ack_compasstag_data_log_tag)
        return -1;
    const CompassTagLog *log = &ack.payload.compasstag_data_log;
    assert(log->epoch == vddHeader[index].epoch);
    assert(fabsf(log->temperature - (-5.5f)) < 0.01f);
    for (int i = 0; i < log->data_count; i++)
        assert(decode_seq(&log->data[i]) == first_seq + (uint32_t)i &&
               "downloaded sample out of sequence");
    return log->data_count;
}

/**
 * A1: run 2.5 pages, reset, resume, run two more pages. The resumed cursor
 * must be the start of page 3, and every page must download intact.
 */
static void test_resume(void)
{
    const uint32_t PAGE = DATALOG_PAGE_WORDS;
    const int PER_PAGE = DATALOG_SAMPLES * SAMPLES_PER_BLOCK;

    reset_world(4u * 1024u * 1024u);
    assert(PAGE == 190 && "t_DataLog is 380 bytes");

    Running(T_INIT, 0);
    assert(pState->pages == 1 && pState->external_blocks == 0);

    for (int i = 0; i < 2 * PER_PAGE + PER_PAGE / 2; i++)
        assert(tick());
    assert(pState->pages == 3);

    /* Reset with no FINISHED marker: the cursor comes from the header count. */
    reset_and_resume();
    assert(pState->pages == 3);
    assert(pState->external_blocks == 3 * PAGE && "restoreLog cursor not in words");

    Running(T_INIT, 0);
    assert(pState->pages == 4);
    assert(pState->external_blocks == 3 * PAGE && "Running(T_INIT) cursor not in words");

    uint32_t resumed_seq = sample_seq;
    for (int i = 0; i < 2 * PER_PAGE; i++)
        assert(tick());

    /* A resume straight into Running(T_INIT), as from hibernation, with no
       restoreLog(): the cursor must again move to a fresh page. */
    pState->external_blocks = 12345;   /* any stale value */
    Running(T_INIT, 0);
    assert(pState->external_blocks == 6 * PAGE);
    uint32_t second_seq = sample_seq;
    for (int i = 0; i < 4; i++)
        assert(tick());

    assert(download(0, 0) == PER_PAGE);
    assert(download(1, PER_PAGE) == PER_PAGE);
    /* 15 samples were written to page 2 before the reset: five whole blocks. */
    assert(download(2, 2 * PER_PAGE) == PER_PAGE / 2);
    assert(download(3, resumed_seq) == PER_PAGE);
    assert(download(4, resumed_seq + PER_PAGE) == PER_PAGE);
    /* Page 5's header was written at the boundary after page 4; the
       Running(T_INIT) that followed moved the cursor to page 6 before any
       sample reached it, so it downloads empty. */
    assert(download(5, 0) == 0);
    /* Four samples: one whole block of three, the fourth has no activity word. */
    assert(download(6, second_seq) == 3);
    assert(download(7, 0) == -1);

    printf("A1 resume: cursor %u words after 3 pages; 7 pages downloaded intact\n",
           (unsigned)(3 * PAGE));
}

/**
 * A3: log until @p bytes of external flash are full, then download every
 * page. The last page is cut short by the end of the part and must still be
 * served, holding exactly the blocks completed before the end.
 */
static void test_full(uint32_t bytes, const char *part)
{
    const uint32_t page_bytes = sizeof(t_DataLog);
    const uint32_t block_bytes = sizeof(databuf.data[0]);
    const int PER_PAGE = DATALOG_SAMPLES * SAMPLES_PER_BLOCK;

    reset_world(bytes);
    Running(T_INIT, 0);
    while (tick())
        ;
    assert(finished_reason == State_EVENT_EXTERNALFULL);

    const uint32_t whole_pages = bytes / page_bytes;
    const uint32_t tail_samples =
        (bytes % page_bytes) / block_bytes * SAMPLES_PER_BLOCK;
    assert(pState->pages == whole_pages + 1 && "header for the cut-short page");

    uint32_t seq = 0;
    for (uint32_t i = 0; i < whole_pages; i++) {
        int n = download((int)i, seq);
        assert(n == PER_PAGE);
        seq += (uint32_t)n;
    }
    int last = download((int)whole_pages, seq);
    assert(last >= 0 && "final partial page not served");
    assert((uint32_t)last == tail_samples);
    seq += (uint32_t)last;
    assert(download((int)whole_pages + 1, 0) == -1);

    /* Lost: at most the samples of the block the end of flash cut short,
       counting the sample whose write was refused. */
    assert(sample_seq - seq <= SAMPLES_PER_BLOCK);

    printf("A3 full %s: %u whole pages + final page of %d samples; "
           "%u of %u samples downloaded\n",
           part, (unsigned)whole_pages, last, (unsigned)seq, (unsigned)sample_seq);
}

int main(void)
{
    test_resume();
    test_full(4u * 1024u * 1024u, "4 MiB (AT25XE321D)");
    test_full(8u * 1024u * 1024u, "8 MiB (MX25R6435F)");
    printf("COMPASSTAG DATALOG SIM: all assertions passed\n");
    return 0;
}
