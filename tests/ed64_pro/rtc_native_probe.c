/* Standalone destructive-clock experiment using public libdragon RTC APIs.
 * Documented vendor frames use the production bounded transport; test
 * configurations deliberately bypass its game-size validation policy. */
#include <errno.h>
#include <inttypes.h>
#include <libdragon.h>
#include <libcart/cart.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "flashcart/ed64_pro/ed64_pro_ll.c"

#ifndef PROBE_NONCE
#define PROBE_NONCE 0x52544311u
#endif
#ifndef PROBE_RTC_MODE
#define PROBE_RTC_MODE 1
#endif
#ifndef PROBE_SET_TIME
#define PROBE_SET_TIME INT64_C(1749990896) /* 2025-06-15 12:34:56 UTC */
#endif
#ifndef PROBE_ROM_MODE
#define PROBE_ROM_MODE 0 /* Explicit ROM_OFF hypothesis; not preservation. */
#endif
#ifndef PROBE_CONFIG_SIZE
#define PROBE_CONFIG_SIZE 0
#endif
#ifndef PROBE_PRESERVE_SERVICES
#define PROBE_PRESERVE_SERVICES 0
#endif

enum { PACKET_SIZE = 256, ROM_SAMPLE = 1024, SAMPLE_TOTAL = ROM_SAMPLE * 2,
       FIXTURE_OFFSET = 0x280000, FIXTURE_CRC = 0xEF107C8F };
static uint8_t initial_rom[SAMPLE_TOTAL] __attribute__((aligned(16)));
#if !PROBE_PRESERVE_SERVICES
static uint8_t check_rom[SAMPLE_TOTAL] __attribute__((aligned(16)));
#endif
static uint8_t packet[PACKET_SIZE];
static struct {
    uint32_t stage, id, match_pre, match_stop, match_start;
    uint32_t fixture_crc_pre, fixture_crc_stop, fixture_crc_start;
    int error, stop, cfg, start;
    int source_pre, available_pre, source_stop, available_stop, source_start, available_start;
    int native_initial_result, native_set_result, native_after_result;
    time_t native_initial, native_after, target;
    int raw_before_result, raw_after_result;
    uint8_t raw_before[8], raw_after[8];
    uint64_t set_ticks;
    int fresh_result;
    time_t fresh;
    uint8_t fresh_raw[8];
    int fresh_raw_result;
} observation;
static void render (void);

static uint32_t crc32 (const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (unsigned b = 0; b < 8; b++) c = (c >> 1) ^ ((0u - (c & 1u)) & 0xEDB88320u);
    }
    return ~c;
}

static int begin (ed64_pro_ll_transaction_t *t) {
    return ed64_pro_ll_begin(t, TICKS_READ() + TICKS_FROM_MS(5000));
}

static int finish (ed64_pro_ll_transaction_t *t, int result) {
    int end = ed64_pro_ll_end(t);
    return result ? result : end;
}

static int raw_read (uint8_t *out) {
    ed64_pro_ll_transaction_t t;
    int e = begin(&t);
    if (!e) e = finish(&t, ed64_pro_ll_rtc_get(&t, out, t.deadline));
    return e;
}

static uint32_t pi_rom_read (void *out) {
    data_cache_hit_writeback_invalidate(out, SAMPLE_TOTAL);
    dma_read(out, 0x10000000, ROM_SAMPLE);
    dma_read((uint8_t *)out + ROM_SAMPLE, 0x10000000 + FIXTURE_OFFSET, ROM_SAMPLE);
    return crc32((uint8_t *)out + ROM_SAMPLE, ROM_SAMPLE);
}

static void native_source (int *source, int *available) {
    rtc_close();
    /* Exclude legacy PI probes while retaining genuine Joybus detection. */
    cart_type = CART_MAX;
    rtc_init();
    *source = rtc_get_source();
    *available = rtc_is_source_available(RTC_SOURCE_JOYBUS);
}

static int native_read (time_t *out) {
    rtc_source_t source = rtc_get_source();
    if (source != RTC_SOURCE_JOYBUS || !rtc_is_source_available(source)) return RTC_ENOCLOCK;
    /* Public source synchronization performs a fresh hardware read. */
    int result = rtc_set_source(source);
    if (result != RTC_ESUCCESS) return result;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) return -errno;
    *out = tv.tv_sec;
    return RTC_ESUCCESS;
}

static void experiment (void) {
    observation.stage = 1;
    render();
    observation.fixture_crc_pre = pi_rom_read(initial_rom);
    observation.match_pre = observation.fixture_crc_pre == FIXTURE_CRC;
    if (!observation.match_pre) { observation.error = -200; return; }
    ed64_pro_probe_t probe = ed64_pro_ll_probe(TICKS_READ() + TICKS_FROM_MS(2000));
    observation.id = probe.id;
    if ((observation.error = probe.error) || probe.outcome != ED64_PRO_PROBE_IDENTIFIED) return;
    ed64_pro_ll_transaction_t t;
    int e = begin(&t);
    if (!e) {
        size_t drained;
        e = ed64_pro_ll_drain(&t, &drained, t.deadline);
        ed64_pro_status_t status;
        if (!e) e = ed64_pro_ll_read_status(&t, &status, t.deadline);
        e = finish(&t, e);
    }
    if ((observation.error = e)) return;
    observation.stage = 2;
    render();
    native_source(&observation.source_pre, &observation.available_pre);
    observation.raw_before_result = raw_read(observation.raw_before);
#if PROBE_PRESERVE_SERVICES
    /* Observe the launching menu's policy without changing its services. */
    observation.source_start = rtc_get_source();
    observation.available_start = rtc_is_source_available(RTC_SOURCE_JOYBUS);
#else
    observation.stage = 3;
    render();
    rtc_close();
    e = begin(&t);
    if (!e) {
        e = ed64_pro_ll_stop(&t, t.deadline);
        size_t drained;
        if (!e) e = ed64_pro_ll_drain(&t, &drained, t.deadline);
        ed64_pro_status_t status;
        if (!e) e = ed64_pro_ll_check_status(&t, &status, t.deadline);
        e = finish(&t, e);
    }
    observation.stop = e;
    if ((observation.error = e)) return;
    observation.fixture_crc_stop = pi_rom_read(check_rom);
    observation.match_stop = !memcmp(initial_rom, check_rom, SAMPLE_TOTAL);
    native_source(&observation.source_stop, &observation.available_stop);
    rtc_close();
    observation.stage = 4;
    render();
    /* Explicit documented DevCfg hypothesis: no BRM, DD, ROM writes or key.
     * Zero size is an experiment, never a guessed preserve-config convention. */
    uint8_t config[64] = {0};
    pro_put_u32(config, PROBE_ROM_MODE);
    pro_put_u32(config + 4, PROBE_CONFIG_SIZE);
    pro_put_u32(config + 16, PROBE_RTC_MODE);
    e = begin(&t);
    if (!e) {
        e = pro_dev_command(&t, 0x20, config, sizeof(config), t.deadline);
        observation.cfg = e;
        if (!e) {
            e = ed64_pro_ll_dev_start(&t, t.deadline);
            observation.start = e;
        }
        e = finish(&t, e);
    }
    if ((observation.error = e)) return;
    observation.stage = 5;
    render();
    observation.fixture_crc_start = pi_rom_read(check_rom);
    observation.match_start = !memcmp(initial_rom, check_rom, SAMPLE_TOTAL);
    if (!observation.match_start || observation.fixture_crc_start != FIXTURE_CRC) {
        observation.error = -201;
        return;
    }
    native_source(&observation.source_start, &observation.available_start);
#endif
    observation.native_initial_result = native_read(&observation.native_initial);
    if (!observation.available_start || observation.source_start != RTC_SOURCE_JOYBUS) return;
    observation.stage = 6;
    render();
    observation.target = PROBE_SET_TIME;
    struct timeval tv = { .tv_sec = observation.target };
    uint64_t start = get_ticks();
    observation.native_set_result = settimeofday(&tv, NULL);
    if (observation.native_set_result) observation.native_set_result = -errno;
    observation.set_ticks = get_ticks() - start;
    observation.native_after_result = native_read(&observation.native_after);
    observation.raw_after_result = raw_read(observation.raw_after);
    observation.stage = 7;
}

static size_t cursor;
static void put (uint64_t value, unsigned n) {
    for (unsigned i = 0; i < n; i++) packet[cursor++] = value >> (8 * (n - i - 1));
}
static void bytes (const uint8_t *p, unsigned n) {
    while (n--) put(*p++, 1);
}

static void render (void) {
    memset(packet, 0, sizeof(packet)); cursor = 0;
    put(0x52545032,4); put(PROBE_NONCE,4); put(observation.stage,4); put(observation.error,4);
    put(observation.id,4); put(PROBE_ROM_MODE,4); put(PROBE_CONFIG_SIZE,4); put(PROBE_RTC_MODE,4);
    put(FIXTURE_OFFSET,4); put(FIXTURE_CRC,4);
    put(observation.fixture_crc_pre,4); put(observation.fixture_crc_stop,4); put(observation.fixture_crc_start,4);
    put(observation.match_pre,4); put(observation.match_stop,4); put(observation.match_start,4);
    put(observation.stop,4); put(observation.cfg,4); put(observation.start,4);
    put(observation.source_pre,4); put(observation.available_pre,4);
    put(observation.source_stop,4); put(observation.available_stop,4);
    put(observation.source_start,4); put(observation.available_start,4);
    put(observation.native_initial_result,4); put(observation.native_initial,8);
    put(observation.native_set_result,4); put(observation.target,8);
    put(observation.native_after_result,4); put(observation.native_after,8);
    put(observation.raw_before_result,4); bytes(observation.raw_before,8);
    put(observation.raw_after_result,4); bytes(observation.raw_after,8);
    put(observation.set_ticks,8); put(observation.fresh_result,4); put(observation.fresh,8);
    put(get_ticks(),8); put(time(NULL),8); put(get_memory_size(),4);
    bytes(observation.fresh_raw,8); put(observation.fresh_raw_result,4);
    cursor = PACKET_SIZE - 4; put(crc32(packet, PACKET_SIZE - 4),4);
    surface_t *s = display_get();
    uint32_t black = graphics_make_color(0,0,0,255), white = graphics_make_color(255,255,255,255);
    graphics_fill_screen(s, black); graphics_set_color(white, black);
    char line[100];
    graphics_draw_text(s, 40, 24, "ED64 Pro native RTC probe - public RTC APIs / no FatFs");
#if PROBE_PRESERVE_SERVICES
    snprintf(line,sizeof(line),"stage=%" PRIu32 " error=%d inherited services; expected RTC=%d",observation.stage,observation.error,PROBE_RTC_MODE);
#else
    snprintf(line,sizeof(line),"stage=%" PRIu32 " error=%d ROM=%d size=%u RTC=%d",observation.stage,observation.error,PROBE_ROM_MODE,(unsigned)PROBE_CONFIG_SIZE,PROBE_RTC_MODE);
#endif
    graphics_draw_text(s,40,48,line);
    snprintf(line,sizeof(line),"source pre/STOP/START: %d/%d/%d; avail %d/%d/%d",observation.source_pre,observation.source_stop,observation.source_start,observation.available_pre,observation.available_stop,observation.available_start);
    graphics_draw_text(s,40,72,line);
    snprintf(line,sizeof(line),"SET=%d native=%" PRId64 " MCU=%02X%02X%02X %02X:%02X:%02X",observation.native_set_result,(int64_t)observation.native_after,observation.raw_after[0],observation.raw_after[1],observation.raw_after[2],observation.raw_after[3],observation.raw_after[4],observation.raw_after[5]);
    graphics_draw_text(s,40,96,line);
    snprintf(line,sizeof(line),"2.5 MiB fixture pre/STOP/START: %08" PRIX32 "/%08" PRIX32 "/%08" PRIX32,observation.fixture_crc_pre,observation.fixture_crc_stop,observation.fixture_crc_start);
    graphics_draw_text(s,40,120,line);
    graphics_draw_text(s,40,144,"Clock intentionally changed; coordinator must restore baseline.");
    graphics_draw_box(s,60,192,520,264,graphics_make_color(255,0,255,255));
    for (unsigned bit=0;bit<sizeof(packet)*8;bit++) {
        bool value=(packet[bit/8]>>(7-bit%8))&1;
        graphics_draw_box(s,64+(bit%64)*8,196+(bit/64)*8,8,8,value?white:black);
    }
    display_show(s);
}

int main (void) {
    display_init(RESOLUTION_640x480, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    observation.native_set_result = observation.native_initial_result = observation.native_after_result = -100;
    observation.raw_before_result = observation.raw_after_result = observation.fresh_result = -100;
    observation.fresh_raw_result = -100;
    observation.stop = observation.cfg = observation.start = -100;
    experiment();
    uint64_t last = 0;
    for (;;) {
        if (observation.available_start && get_ticks() - last >= TICKS_PER_SECOND) {
            observation.fresh_result = native_read(&observation.fresh);
            observation.fresh_raw_result = raw_read(observation.fresh_raw);
            last = get_ticks();
        }
        render();
    }
}
