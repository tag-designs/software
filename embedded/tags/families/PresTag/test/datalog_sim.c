/**
 * @file    datalog_sim.c
 * @brief   Host-side simulation of PresTag logging, resume and download.
 *
 * @details Compiles the real ../src/state_run.c and ../src/datalog.c, by
 *          including them directly, against the minimal stubs in test/stub.
 *          It drives Running() over a synthetic sample ticker against a fake
 *          external NOR and a fake internal header array, then downloads every
 *          page through the real data_logAck().
 *
 *          Its main subject is A3 on the firmware-fix branch: when the
 *          external flash fills, the final page, cut short by the end of the
 *          part, must be downloaded and end exactly where the samples end.
 *          fw-v0.0.3 served only pages that fit whole, so the last one was
 *          written and never downloaded. It also checks a mid-page reset
 *          resumes on a fresh page without reprogramming written bytes.
 *
 *          Built twice: as is for the converted PresTagLog download, and with
 *          -DPRESTAG_RAW_LOG=1 for PresTagRaw's packed-block download.
 *
 *          Every sample carries its sequence number in pressure/temperature,
 *          so the download is checked sample by sample against what was
 *          written.
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
t_storedconfig sconfig;
static BackupState backup;
volatile BackupState *const pState = &backup;
stub_flash_t stub_flash;
stub_rcc_t stub_rcc;
stub_tim_t stub_tim = {1};
const TagStorageDevice tagExternalFlash;
const TagRegisterDevice tagPressureDevice;

/* --- simulated internal flash: the header array and its end -------------- */
#define MAX_HEADERS 40000
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
void enableTicker(uint16_t interval) { (void)interval; }
void adcVDD(uint16_t *vdd100, int16_t *temp10) { *vdd100 = 300; *temp10 = 250; }
void recordState(State_Event reason) { (void)reason; }
void stopMilliseconds(uint32_t ms) { (void)ms; }
void tagStopRtcTickerInit(void) {}
int encode_ack(void) { return 0; }

/* Identity conversions, so a downloaded sample decodes to its raw words. */
float lps27Pressure(int16_t pressure) { return (float)pressure; }
float lps27Temperature(int16_t temperature) { return (float)temperature; }

/* Each sample carries its sequence number: pressure = seq % 30000 (never the
   -1 end marker), temperature = seq / 30000. */
void lps27GetPressureTemp(const TagRegisterDevice *dev, int16_t *pressure,
                          int16_t *temperature)
{
    (void)dev;
    *pressure = (int16_t)(sample_seq % 30000u);
    *temperature = (int16_t)(sample_seq / 30000u);
    sample_seq++;
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
    memset(&backup, 0, sizeof(backup));
    sconfig.stop = INT_MAX;
    sconfig.lps_period = 1;
    memset(sconfig.hibernate, 0, sizeof(sconfig.hibernate));
    timestamp = 1000000;
    events = 0;
    sample_seq = 0;
    finished_count = 0;
}

/** One ticker wakeup; returns true while still running. */
static bool tick(void)
{
    timestamp += 1;
    events = EVT_RTC_WUTF;
    Running(T_CONT, 0);
    events = 0;
    return finished_count == 0;
}

static uint32_t decode_seq(int16_t pressure, int16_t temperature)
{
    return (uint32_t)pressure + 30000u * (uint32_t)temperature;
}

/** A page as downloaded: its header epoch and its samples' sequence numbers. */
typedef struct { int32_t epoch; int count; uint32_t seq[DATALOG_SAMPLES]; } Page;

#if PRESTAG_RAW_LOG
/**
 * Download from page @p index with one raw request, which packs whole pages
 * from @p index on. Returns the number of pages, 0 when none are served.
 */
static int download(int index, Page *pages, int max_pages)
{
    static Ack ack;
    memset(&ack, 0, sizeof(ack));
    data_logAck(index, &ack);
    if (ack.which_payload == 0)
        return 0;
    assert(ack.which_payload == Ack_prestag_raw_data_log_tag);
    const PresTagRawLog *log = &ack.payload.prestag_raw_data_log;
    assert(log->samples.size % sizeof(t_PresTagRawBlock) == 0);
    int n = (int)(log->samples.size / sizeof(t_PresTagRawBlock));
    assert(n <= max_pages);
    for (int b = 0; b < n; b++) {
        t_PresTagRawBlock block;
        memcpy(&block, &log->samples.bytes[b * sizeof(block)], sizeof(block));
        pages[b].epoch = block.epoch;
        pages[b].count = 0;
        /* The host decoder ends a page at the first erased sample. */
        for (int j = 0; j < DATALOG_SAMPLES; j++) {
            const t_PresTagRawSample *s = &block.samples.data[j];
            if (s->pressure == PRESTAG_RAW_PRESSURE_END)
                break;
            pages[b].seq[pages[b].count++] = decode_seq(s->pressure, s->temperature);
        }
    }
    return n;
}
#else
/** Download page @p index. Returns 1, or 0 when the page is not served. */
static int download(int index, Page *pages, int max_pages)
{
    static Ack ack;
    assert(max_pages >= 1);
    memset(&ack, 0, sizeof(ack));
    data_logAck(index, &ack);
    if (ack.which_payload == 0)
        return 0;
    assert(ack.which_payload == Ack_prestag_data_log_tag);
    const PresTagLog *log = &ack.payload.prestag_data_log;
    pages[0].epoch = log->epoch;
    pages[0].count = log->data_count;
    for (int j = 0; j < log->data_count; j++)
        pages[0].seq[j] = decode_seq((int16_t)log->data[j].pressure,
                                     (int16_t)log->data[j].temperature);
    return 1;
}
#endif

/**
 * Download every page from 0 and check each page's samples run on from the
 * previous page's. @p expect[i] is page i's expected sample count; @p first[i]
 * its first sequence number. Returns the number of pages served.
 */
static int download_all(int npages, const int *expect, const uint32_t *first)
{
    static Page pages[16];
    int index = 0;
    for (;;) {
        int n = download(index, pages, 16);
        if (n == 0)
            break;
        for (int b = 0; b < n; b++, index++) {
            assert(index < npages && "page served beyond the last header");
            assert(pages[b].epoch == vddHeader[index].epoch);
            assert(pages[b].count == expect[index] && "wrong sample count");
            for (int j = 0; j < pages[b].count; j++)
                assert(pages[b].seq[j] == first[index] + (uint32_t)j &&
                       "downloaded sample out of sequence");
        }
    }
    return index;
}

/**
 * Resume: reset after 2.5 pages, resume on page 3 without reprogramming,
 * and download all four pages intact.
 */
static void test_resume(void)
{
    reset_world(4u * 1024u * 1024u);
    Running(T_INIT, 0);
    for (int i = 0; i < 2 * DATALOG_SAMPLES + DATALOG_SAMPLES / 2; i++)
        assert(tick());
    assert(pState->pages == 3);

    memset(&backup, 0, sizeof(backup));
    (void)restoreLog();
    assert(pState->pages == 3);
    assert(pState->external_blocks == 3 * DATALOG_SAMPLES);

    uint32_t resumed = sample_seq;
    Running(T_INIT, 0);
    for (int i = 0; i < DATALOG_SAMPLES; i++)
        assert(tick());

    const int expect[] = {DATALOG_SAMPLES, DATALOG_SAMPLES, DATALOG_SAMPLES / 2,
                          DATALOG_SAMPLES};
    const uint32_t first[] = {0, DATALOG_SAMPLES, 2 * DATALOG_SAMPLES, resumed};
    assert(download_all(4, expect, first) == 4);
    printf("resume: 4 pages downloaded intact across a mid-page reset\n");
}

/**
 * A3: log until @p bytes of external flash are full, then download every
 * page. The last page is cut short by the end of the part and must still be
 * served, holding exactly the samples that fit.
 */
static void test_full(uint32_t bytes, const char *part)
{
    static int expect[MAX_HEADERS];
    static uint32_t first[MAX_HEADERS];
    const uint32_t page_bytes = sizeof(t_DataLog);

    reset_world(bytes);
    Running(T_INIT, 0);
    while (tick())
        ;

    const int whole = (int)(bytes / page_bytes);
    const int tail = (int)((bytes % page_bytes) / sizeof(t_PresTagRawSample));
    assert(tail > 0 && "choose a size that cuts the last page short");
    assert(pState->pages == (uint32_t)whole + 1 && "header for the cut-short page");

    for (int i = 0; i <= whole; i++) {
        expect[i] = (i < whole) ? DATALOG_SAMPLES : tail;
        first[i] = (uint32_t)i * DATALOG_SAMPLES;
    }
    int served = download_all(whole + 1, expect, first);
    assert(served == whole + 1 && "final partial page not served");

    /* Lost: only the sample whose write was refused. */
    uint32_t downloaded = (uint32_t)whole * DATALOG_SAMPLES + (uint32_t)tail;
    assert(sample_seq - downloaded == 1);

    printf("A3 full %s: %d whole pages + final page of %d samples; "
           "%u of %u samples downloaded\n",
           part, whole, tail, (unsigned)downloaded, (unsigned)sample_seq);
}

int main(void)
{
    printf("PresTag %s download\n", PRESTAG_RAW_LOG ? "raw" : "converted");
    test_resume();
    test_full(4u * 1024u * 1024u, "4 MiB (AT25XE321D)");
    printf("PRESTAG DATALOG SIM: all assertions passed\n");
    return 0;
}
