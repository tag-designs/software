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
 *          Built twice: as is for PresTag, and with -DPRESTAG_RAW_LOG=1 for
 *          PresTagRaw's packed-block download. The PresTag build runs every
 *          test in each stored-configuration format -- the converted
 *          PresTagLog download and, with lps27.high_resolution, the
 *          PresTagHRLog blocks of five-byte samples -- and each with
 *          lps27.low_noise as well.
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
/* The PresTag system-tick prescaler: 2 MHz / 10 kHz - 1. */
#define SIM_TICK_PSC 199U
stub_tim_t stub_tim = {.PSC = SIM_TICK_PSC};
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

/* Full read. In high resolution the 24-bit pressure is the sequence number
   itself (never -1 or the failed-read value) and the temperature its low 15
   bits. In the 4-byte format, which keeps PRESS_OUT_L, PRESS_OUT_H and the
   temperature, those four bytes carry the 4-byte encoding below, so either
   read path decodes the same way. A sample numbered fail_seq fails as the
   driver does, leaving the 24-bit minimum. */
static uint32_t fail_seq = UINT32_MAX; /**< Sample number whose read fails; none by default. */
static int last_low_noise = -1;        /**< low_noise of the last read; -1 before any read. */

/** Fake full-resolution read: see the comment above. Records @p low_noise. */
bool lps27GetPressureTempFull(const TagRegisterDevice *dev, bool low_noise,
                              uint8_t raw[LPS27_FULL_SAMPLE_BYTES])
{
    (void)dev;
    last_low_noise = low_noise;
    const bool ok = sample_seq != fail_seq;
    const bool hr = sconfig.lps_high_res == 1U;
    const uint32_t p = !ok ? 0x800000u
                     : hr  ? sample_seq
                           : (0x5Au | ((sample_seq % 30000u) << 8));
    const uint16_t t = !ok ? 0x8000u
                     : hr  ? (uint16_t)(sample_seq & 0x7FFFu)
                           : (uint16_t)(sample_seq / 30000u);
    raw[0] = (uint8_t)p;
    raw[1] = (uint8_t)(p >> 8);
    raw[2] = (uint8_t)(p >> 16);
    raw[3] = (uint8_t)t;
    raw[4] = (uint8_t)(t >> 8);
    sample_seq++;
    return ok;
}

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

static uint8_t sim_high_res;  /**< lps_high_res reset_world() stores; PresTagRaw ignores it. */
static uint8_t sim_low_noise; /**< lps_low_noise reset_world() stores; PresTagRaw ignores it. */

/** Samples per page in the format under test. */
#define SPP ((int)prestagSamplesPerPage())

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
    sconfig.lps_low_noise = sim_low_noise;
    sconfig.lps_high_res = sim_high_res;
    memset(sconfig.hibernate, 0, sizeof(sconfig.hibernate));
    fail_seq = UINT32_MAX;
    last_low_noise = -1;
    timestamp = 1000000;
    events = 0;
    sample_seq = 0;
    finished_count = 0;
}

/** One ticker wakeup @p dt seconds after the last; true while still running. */
static bool tick_by(int32_t dt)
{
    timestamp += dt;
    events = EVT_RTC_WUTF;
    Running(T_CONT, 0);
    events = 0;
    return finished_count == 0;
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

/** Sequence number recorded for a high-resolution sample whose read failed. */
#define SEQ_FAILED UINT32_MAX

#if !PRESTAG_RAW_LOG
/**
 * Decode a high-resolution reply, which packs whole pages from the requested
 * index on, as the host does (sqlitelog/pressure.cc). Returns the number of
 * pages.
 */
static int decode_hr(const Ack *ack, Page *pages, int max_pages)
{
    assert(sconfig.lps_high_res == 1U && "high-resolution reply in the 4-byte format");
    const PresTagHRLog *log = &ack->payload.prestag_hr_data_log;
    assert(log->samples.size > 0);
    assert(log->samples.size % sizeof(t_PresTagHRBlock) == 0);
    int n = (int)(log->samples.size / sizeof(t_PresTagHRBlock));
    assert(n <= max_pages);
    for (int b = 0; b < n; b++) {
        t_PresTagHRBlock block;
        memcpy(&block, &log->samples.bytes[b * sizeof(block)], sizeof(block));
        pages[b].epoch = block.epoch;
        pages[b].count = 0;
        for (int j = 0; j < PRESTAG_HR_LOG_SAMPLES; j++) {
            const t_PresTagHRSample *s = &block.samples.data[j];
            const int32_t p = prestagHRPressureRaw(s);
            if (p == PRESTAG_RAW_PRESSURE_END)
                break;
            if (p == PRESTAG_HR_PRESSURE_FAILED) {
                assert(s->temperature == INT16_MIN);
                pages[b].seq[pages[b].count++] = SEQ_FAILED;
                continue;
            }
            assert(s->temperature == (int16_t)(p & 0x7FFF) &&
                   "temperature bytes misplaced");
            pages[b].seq[pages[b].count++] = (uint32_t)p;
        }
    }
    return n;
}
#endif

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
    /* The clock switch around a page must leave the tick prescaler as found. */
    assert(stub_tim.PSC == SIM_TICK_PSC);
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
/**
 * Download from page @p index: one converted page in the 4-byte format, or
 * as many whole high-resolution pages as one reply holds. Returns the number
 * of pages, 0 when none are served.
 */
static int download(int index, Page *pages, int max_pages)
{
    static Ack ack;
    assert(max_pages >= 1);
    memset(&ack, 0, sizeof(ack));
    data_logAck(index, &ack);
    /* The clock switch around a page must leave the tick prescaler as found. */
    assert(stub_tim.PSC == SIM_TICK_PSC);
    if (ack.which_payload == 0)
        return 0;
    if (ack.which_payload == Ack_prestag_hr_data_log_tag)
        return decode_hr(&ack, pages, max_pages);
    assert(sconfig.lps_high_res != 1U && "4-byte reply in high resolution");
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
                assert((pages[b].seq[j] == first[index] + (uint32_t)j ||
                        (pages[b].seq[j] == SEQ_FAILED &&
                         first[index] + (uint32_t)j == fail_seq)) &&
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
    for (int i = 0; i < 2 * SPP + SPP / 2; i++)
        assert(tick());
    assert(pState->pages == 3);

    memset(&backup, 0, sizeof(backup));
    (void)restoreLog();
    assert(pState->pages == 3);
    assert(pState->external_blocks == (uint32_t)(3 * SPP));

    uint32_t resumed = sample_seq;
    Running(T_INIT, 0);
    for (int i = 0; i < SPP; i++)
        assert(tick());

    const int expect[] = {SPP, SPP, SPP / 2,
                          SPP};
    const uint32_t first[] = {0, SPP, 2 * SPP, resumed};
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
    const int sample_bytes = sim_high_res ? PRESTAG_HR_SAMPLE_BYTES : 4;
    const int tail = (int)((bytes % page_bytes) / sample_bytes);
    assert(tail > 0 && "choose a size that cuts the last page short");
    assert(pState->pages == (uint32_t)whole + 1 && "header for the cut-short page");

    for (int i = 0; i <= whole; i++) {
        expect[i] = (i < whole) ? SPP : tail;
        first[i] = (uint32_t)i * SPP;
    }
    int served = download_all(whole + 1, expect, first);
    assert(served == whole + 1 && "final partial page not served");

    /* Lost: only the sample whose write was refused. */
    uint32_t downloaded = (uint32_t)whole * SPP + (uint32_t)tail;
    assert(sample_seq - downloaded == 1);

    printf("A3 full %s: %d whole pages + final page of %d samples; "
           "%u of %u samples downloaded\n",
           part, whole, tail, (unsigned)downloaded, (unsigned)sample_seq);
}

/**
 * Gap: a halt of @p halt_s seconds mid-page, at a period of @p period_s --
 * what a tag-capture does to a running tag. Samples carry no timestamps, so
 * the samples after the halt must start a new page whose header holds their
 * own time; continuing the old page would place them @p halt_s too early.
 */
static void test_gap(int32_t period_s, int32_t halt_s)
{
    reset_world(4u * 1024u * 1024u);
    sconfig.lps_period = (uint32_t)period_s;
    Running(T_INIT, 0);
    for (int i = 0; i < 20; i++)
        assert(tick_by(period_s));
    timestamp += halt_s;               /* core halted: no wakeups */
    const int32_t resumed_at = timestamp + period_s;
    for (int i = 0; i < 10; i++)
        assert(tick_by(period_s));

    const int expect[] = {20, 10};
    const uint32_t first[] = {0, 20};
    assert(download_all(2, expect, first) == 2 && "halt did not start a new page");
    assert(vddHeader[1].epoch == resumed_at && "new page header has the wrong time");
    printf("gap: %d s halt at %d s period starts page 2 at the resumed time\n",
           (int)halt_s, (int)period_s);
}

/**
 * Jitter: integer-second timestamps of a 1 s ticker can step 0 or 2 when a
 * wakeup lands near a second boundary. That must not split a page.
 */
static void test_jitter(void)
{
    static const int32_t steps[] = {1, 1, 2, 0, 1, 2, 0, 1};
    reset_world(4u * 1024u * 1024u);
    Running(T_INIT, 0);
    for (int i = 0; i < SPP; i++)
        assert(tick_by(steps[i % 8]));
    const int expect[] = {SPP};
    const uint32_t first[] = {0};
    assert(download_all(1, expect, first) == 1);
    assert(pState->pages == 1 && "jitter split a page");
    printf("jitter: 1 s wake jitter keeps one page\n");
}

#if !PRESTAG_RAW_LOG
/**
 * High resolution: a failed read keeps its slot as the 24-bit minimum, so the
 * samples after it keep their times.
 */
static void test_hr_failed_read(void)
{
    reset_world(4u * 1024u * 1024u);
    fail_seq = 7;
    Running(T_INIT, 0);
    for (int i = 0; i < SPP + 5; i++)
        assert(tick());
    const int expect[] = {SPP, 5};
    const uint32_t first[] = {0, (uint32_t)SPP};
    assert(download_all(2, expect, first) == 2);
    static Page pages[16];
    assert(download(0, pages, 16) == 2);
    assert(pages[0].seq[7] == SEQ_FAILED && pages[0].seq[8] == 8);
    printf("hr: a failed read holds its slot\n");
}
#endif

/** Every test, in the format and noise mode set in sim_high_res and
    sim_low_noise, then a check that the noise mode reached the driver. */
static void run_all(void)
{
    printf("-- %s format, %s conversion\n",
           PRESTAG_RAW_LOG ? "raw" : (sim_high_res ? "high-resolution" : "4-byte"),
           sim_low_noise ? "low-noise" : "low-power");
    test_resume();
    test_full(4u * 1024u * 1024u, "4 MiB (AT25XE321D)");
    test_gap(1, 55);
    test_gap(10, 55);
    test_jitter();
    /* The default configuration keeps PresTag's original read; either option
       takes the full read and passes the stored noise mode on. */
    if (sim_high_res || sim_low_noise)
        assert(last_low_noise == sim_low_noise && "noise mode not passed on");
    else
        assert(last_low_noise == -1 && "default configuration used the full read");
}

int main(void)
{
#if PRESTAG_RAW_LOG
    printf("PresTag raw download\n");
    run_all();
#else
    printf("PresTag download, both formats\n");
    for (int hr = 0; hr <= 1; hr++) {
        for (int ln = 0; ln <= 1; ln++) {
            sim_high_res = (uint8_t)hr;
            sim_low_noise = (uint8_t)ln;
            run_all();
        }
    }
    sim_high_res = 1;
    sim_low_noise = 0;
    test_hr_failed_read();
#endif
    printf("PRESTAG DATALOG SIM: all assertions passed\n");
    return 0;
}
