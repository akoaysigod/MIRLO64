/* N64 audio microcode, old (pre-EU) ABI as SM64 US emits it -- integer C.
 *
 * The arithmetic of every command is ported from the scalar paths of the
 * SM64 PC port's src/audio/mixer.c (sm64ex lineage; the copy used is
 * Ghostship's src/audio/mixer.c, MIT licence -- see NOTICE), which mirrors
 * the RSP ucode: VADPCM decode with the loop state, the 4-tap resampler
 * table, the envelope mixer's exponential ramp with its dry/wet (aux) sends,
 * aMix, aInterleave. It was checked against that reference, bit for bit.
 *
 * Differences from mixer.c, none of which change a result:
 *   - DRAM is reached through dram_rd/dram_wr (on the audio core that is
 *     SDRAM over the SoC bus, word-wise where aligned);
 *   - the work buffer has a guard in front: aResample reads history below
 *     its input address;
 *   - aResample's `in -= tmp[5] / sizeof(int16_t)` divides a negative int16
 *     by an unsigned size_t; the pointer arithmetic then wraps to
 *     `in -= tmp[5] >> 1` (floor) on any two's-complement target, which is
 *     what is written here;
 *   - no divide instructions (the audio core has no divider): the envelope
 *     mixer's `/ 8` is on int32 and compiles to shifts.
 */
#include "abi.h"

#include <stddef.h>
#include <string.h>

#include "audio_cfu.h"   /* the audio core's DSP instructions (or their C model) */
#define ABI_HAVE_CFU 1

/* Code size: the audio core's two 8 KiB IMEM banks are full. ABI_SMALL
 * compiles what is cold (once a game, once a task, rare commands) for size. */
#define ABI_SMALL __attribute__((optimize("Os")))
#define ABI_W __attribute__((aligned(4)))           /* (word copies without swl/swr: the audio core has none) */

#define ROUND_UP_32(v) (((v) + 31) & ~31)
#define ROUND_UP_16(v) (((v) + 15) & ~15)
#define ROUND_UP_8(v)  (((v) + 7) & ~7)

/* naudio's command state (below) */
static struct {
    int16_t vol[2], target[2];
    int32_t rate[2];
    int16_t dry, wet;
    uintptr_t loop;
} na;
/* EAD's command state (below) */
static struct {
    uint16_t in, out, count;
    uintptr_t loop;
    uint16_t env[3], step[3];               /* the envelope: Q16 volumes and their steps */
    uint16_t filter_count;
    uintptr_t filter_lut[2];
} ne;
static int s_abi;               /* ABI_KIND_* */

#define BUF_GUARD 64            /* bytes below DMEM address 0 (aResample history) */
/* naudio's fixed buffers reach DMEM 0xF90. On the audio core (the DMEM state
 * cache) the buffer keeps ABI1's size and naudio's run on over the envelope
 * mixer's state slots, which only ABI1 uses: abi_select() empties them. */
#define SC_BIG_N   20            /* envelope mixer: 80 bytes */
#define BUF_SIZE  2816
#define NA_OVERLAY 1

static struct {
    union {
        int16_t as_s16[(BUF_GUARD + BUF_SIZE) / 2];
        uint8_t as_u8[BUF_GUARD + BUF_SIZE];
        uint32_t align;
    } b;
    uint32_t sc_big[SC_BIG_N][20];
} s_dm;
#define s_buf s_dm.b
_Static_assert(sizeof s_dm - BUF_GUARD >= 0xF90 + 16, "naudio's buffers do not fit");
#define BUF_U8(a)  (s_buf.as_u8 + BUF_GUARD + (a))
/* Mirlo-N64: DMEM as the N64's. On the big-endian audio core that is its
 * own order: an int16 sample reads as one (BUF_S16), byte k is BUF_U8(k),
 * and DRAM transfers are plain copies. */
#define BX(a) (a)
#define BUF_S16(a) ((int16_t *)(s_buf.as_u8 + BUF_GUARD + (a)))

static struct {
    uint16_t in, out, nbytes;
    int16_t vol[2];
    uint16_t dry_right, wet_left, wet_right;
    int16_t target[2];
    int32_t rate[2];
    int16_t vol_dry, vol_wet;
    uintptr_t adpcm_loop_state;
    int16_t adpcm_table[8][2][8];
} rspa;

uint32_t abi_op_count[16];
uint32_t abi_env_cfu_calls;   /* envelope mixes done on the DSP instructions */

const int16_t acfu_resample_table[64][4] = {
    {0x0c39, 0x66ad, 0x0d46, (int16_t)0xffdf}, {0x0b39, 0x6696, 0x0e5f, (int16_t)0xffd8},
    {0x0a44, 0x6669, 0x0f83, (int16_t)0xffd0}, {0x095a, 0x6626, 0x10b4, (int16_t)0xffc8},
    {0x087d, 0x65cd, 0x11f0, (int16_t)0xffbf}, {0x07ab, 0x655e, 0x1338, (int16_t)0xffb6},
    {0x06e4, 0x64d9, 0x148c, (int16_t)0xffac}, {0x0628, 0x643f, 0x15eb, (int16_t)0xffa1},
    {0x0577, 0x638f, 0x1756, (int16_t)0xff96}, {0x04d1, 0x62cb, 0x18cb, (int16_t)0xff8a},
    {0x0435, 0x61f3, 0x1a4c, (int16_t)0xff7e}, {0x03a4, 0x6106, 0x1bd7, (int16_t)0xff71},
    {0x031c, 0x6007, 0x1d6c, (int16_t)0xff64}, {0x029f, 0x5ef5, 0x1f0b, (int16_t)0xff56},
    {0x022a, 0x5dd0, 0x20b3, (int16_t)0xff48}, {0x01be, 0x5c9a, 0x2264, (int16_t)0xff3a},
    {0x015b, 0x5b53, 0x241e, (int16_t)0xff2c}, {0x0101, 0x59fc, 0x25e0, (int16_t)0xff1e},
    {0x00ae, 0x5896, 0x27a9, (int16_t)0xff10}, {0x0063, 0x5720, 0x297a, (int16_t)0xff02},
    {0x001f, 0x559d, 0x2b50, (int16_t)0xfef4}, {(int16_t)0xffe2, 0x540d, 0x2d2c, (int16_t)0xfee8},
    {(int16_t)0xffac, 0x5270, 0x2f0d, (int16_t)0xfedb}, {(int16_t)0xff7c, 0x50c7, 0x30f3, (int16_t)0xfed0},
    {(int16_t)0xff53, 0x4f14, 0x32dc, (int16_t)0xfec6}, {(int16_t)0xff2e, 0x4d57, 0x34c8, (int16_t)0xfebd},
    {(int16_t)0xff0f, 0x4b91, 0x36b6, (int16_t)0xfeb6}, {(int16_t)0xfef5, 0x49c2, 0x38a5, (int16_t)0xfeb0},
    {(int16_t)0xfedf, 0x47ed, 0x3a95, (int16_t)0xfeac}, {(int16_t)0xfece, 0x4611, 0x3c85, (int16_t)0xfeab},
    {(int16_t)0xfec0, 0x4430, 0x3e74, (int16_t)0xfeac}, {(int16_t)0xfeb6, 0x424a, 0x4060, (int16_t)0xfeaf},
    {(int16_t)0xfeaf, 0x4060, 0x424a, (int16_t)0xfeb6}, {(int16_t)0xfeac, 0x3e74, 0x4430, (int16_t)0xfec0},
    {(int16_t)0xfeab, 0x3c85, 0x4611, (int16_t)0xfece}, {(int16_t)0xfeac, 0x3a95, 0x47ed, (int16_t)0xfedf},
    {(int16_t)0xfeb0, 0x38a5, 0x49c2, (int16_t)0xfef5}, {(int16_t)0xfeb6, 0x36b6, 0x4b91, (int16_t)0xff0f},
    {(int16_t)0xfebd, 0x34c8, 0x4d57, (int16_t)0xff2e}, {(int16_t)0xfec6, 0x32dc, 0x4f14, (int16_t)0xff53},
    {(int16_t)0xfed0, 0x30f3, 0x50c7, (int16_t)0xff7c}, {(int16_t)0xfedb, 0x2f0d, 0x5270, (int16_t)0xffac},
    {(int16_t)0xfee8, 0x2d2c, 0x540d, (int16_t)0xffe2}, {(int16_t)0xfef4, 0x2b50, 0x559d, 0x001f},
    {(int16_t)0xff02, 0x297a, 0x5720, 0x0063}, {(int16_t)0xff10, 0x27a9, 0x5896, 0x00ae},
    {(int16_t)0xff1e, 0x25e0, 0x59fc, 0x0101}, {(int16_t)0xff2c, 0x241e, 0x5b53, 0x015b},
    {(int16_t)0xff3a, 0x2264, 0x5c9a, 0x01be}, {(int16_t)0xff48, 0x20b3, 0x5dd0, 0x022a},
    {(int16_t)0xff56, 0x1f0b, 0x5ef5, 0x029f}, {(int16_t)0xff64, 0x1d6c, 0x6007, 0x031c},
    {(int16_t)0xff71, 0x1bd7, 0x6106, 0x03a4}, {(int16_t)0xff7e, 0x1a4c, 0x61f3, 0x0435},
    {(int16_t)0xff8a, 0x18cb, 0x62cb, 0x04d1}, {(int16_t)0xff96, 0x1756, 0x638f, 0x0577},
    {(int16_t)0xffa1, 0x15eb, 0x643f, 0x0628}, {(int16_t)0xffac, 0x148c, 0x64d9, 0x06e4},
    {(int16_t)0xffb6, 0x1338, 0x655e, 0x07ab}, {(int16_t)0xffbf, 0x11f0, 0x65cd, 0x087d},
    {(int16_t)0xffc8, 0x10b4, 0x6626, 0x095a}, {(int16_t)0xffd0, 0x0f83, 0x6669, 0x0a44},
    {(int16_t)0xffd8, 0x0e5f, 0x6696, 0x0b39}, {(int16_t)0xffdf, 0x0d46, 0x66ad, 0x0c39}
};

static inline int16_t clamp16(int32_t v)
{
    return v < -0x8000 ? -0x8000 : v > 0x7fff ? 0x7fff : (int16_t)v;
}

static inline int32_t clamp32(int64_t v)
{
    return v < -0x7fffffff - 1 ? -0x7fffffff - 1 : v > 0x7fffffff ? 0x7fffffff : (int32_t)v;
}

/* ---- DRAM <-> local copies ------------------------------------------------
 * Word-wise when both ends are word aligned (every stream SM64 uses), else
 * halfwords, else bytes. The word/halfword types are may_alias: these copy
 * into int16_t arrays, and with plain uint32_t pointers GCC -O2 is entitled
 * to assume the destination unchanged (it did: aResample read its pitch
 * accumulator back as the initialiser's 0). */
typedef uint32_t __attribute__((may_alias)) u32_alias;
typedef uint16_t __attribute__((may_alias)) u16_alias;
static void copy_mem(void *dst, const void *src, uint32_t n)
{
    uintptr_t a = (uintptr_t)dst | (uintptr_t)src;
    if ((a & 3) == 0) {
        u32_alias *d = dst; const u32_alias *s = src;
        for (; n >= 4; n -= 4) *d++ = *s++;
        dst = d; src = s;
    } else if ((a & 1) == 0) {
        u16_alias *d = dst; const u16_alias *s = src;
        for (; n >= 2; n -= 2) *d++ = *s++;
        dst = d; src = s;
    }
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
}
/* Mirlo-N64: an original ROM's RDRAM, big-endian (word-native in memory).
 * Plain transfers keep the N64's byte order (the ADPCM streams; the buffers
 * the task writes and reads back itself come back as they went); the tables
 * the game hands over (codebooks, loop states: int16s from the ROM) are
 * read as big-endian int16s. The host provides the three (sim/n64). */
void abi_n64_rd(void *dst, uint32_t addr, uint32_t n);
void abi_n64_wr(uint32_t addr, const void *src, uint32_t n);
void abi_n64_rd16(int16_t *dst, uint32_t addr, uint32_t n);
void abi_n64_wr16(uint32_t addr, const int16_t *src, uint32_t n);   /* (as rd16: big-endian int16s) */
#define dram_rd(dst, src_addr, n) abi_n64_rd((dst), (uint32_t)(src_addr), (n))
#define dram_wr(dst_addr, src, n) abi_n64_wr((uint32_t)(dst_addr), (src), (n))
#define dram_rd16(dst, src_addr, n) abi_n64_rd16((int16_t *)(dst), (uint32_t)(src_addr), (n))

/* ---- per-note states kept in DMEM (ABI_STATE_CACHE) ------------------------
 * The ADPCM decoder's and the resampler's 32-byte states and the envelope
 * mixer's 80 bytes live in DRAM (the note's synthesis buffers) and every
 * command read and wrote them there: ~4,600 of a music tick's ~9,400 SDRAM
 * accesses. Only this core ever touches them -- the game CPU just passes
 * their addresses -- so they are kept here, keyed by that address, and go
 * back to DRAM only when a slot is reused (LRU) or abi_state_flush() runs.
 * On the Pocket an SDRAM access waits behind the rasterizer and the video DMA:
 * replayed with 64 wait cycles per access, the heaviest CCM music tick took
 * 23 ms of the 16.7 ms budget (15.5 ms at 16). */
#define SC_SMALL_N 40            /* ADPCM, resampler: 32 bytes */
static uintptr_t s_sc_key_b[SC_BIG_N], s_sc_key_s[SC_SMALL_N];
static uint32_t  s_sc_use_b[SC_BIG_N], s_sc_use_s[SC_SMALL_N], s_sc_clock;
static uint32_t  s_sc_small[SC_SMALL_N][8];
#define s_sc_big s_dm.sc_big
uint32_t abi_sc_miss;
static int s_sc_hint_b, s_sc_hint_s;   /* the slot after the last hit: notes come in the same order every tick */
static void *sc_lookup(uintptr_t key, uintptr_t *keys, uint32_t *use, uint32_t *slots, int n, uint32_t words, int *hint)
{
    int victim = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    s_sc_clock++;
    int h = *hint;
    if (keys[h] == key) {
        use[h] = s_sc_clock;
        *hint = h + 1 == n ? 0 : h + 1;
        return slots + (uint32_t)h * words;
    }
    for (int i = 0; i < n; i++) {
        if (keys[i] == key) { use[i] = s_sc_clock; *hint = i + 1 == n ? 0 : i + 1; return slots + (uint32_t)i * words; }
        if (use[i] < oldest) { oldest = use[i]; victim = i; }
    }
    abi_sc_miss++;
    uint32_t *slot = slots + (uint32_t)victim * words;
    if (keys[victim]) dram_wr(keys[victim], slot, words * 4u);
    dram_rd(slot, key, words * 4u);
    keys[victim] = key; use[victim] = s_sc_clock;
    *hint = victim + 1 == n ? 0 : victim + 1;
    return slot;
}
#define sc_small(key) ((int16_t *)sc_lookup((key), s_sc_key_s, s_sc_use_s, &s_sc_small[0][0], SC_SMALL_N, 8, &s_sc_hint_s))
#define sc_big(key)   ((int16_t *)sc_lookup((key), s_sc_key_b, s_sc_use_b, &s_sc_big[0][0], SC_BIG_N, 20, &s_sc_hint_b))
ABI_SMALL void abi_state_flush(void)
{
    for (int i = 0; i < SC_BIG_N; i++) if (s_sc_key_b[i]) dram_wr(s_sc_key_b[i], s_sc_big[i], 80);
    for (int i = 0; i < SC_SMALL_N; i++) if (s_sc_key_s[i]) dram_wr(s_sc_key_s[i], s_sc_small[i], 32);
}
static void sc_reset(void)
{
    for (int i = 0; i < SC_BIG_N; i++) { s_sc_key_b[i] = 0; s_sc_use_b[i] = 0; }
    for (int i = 0; i < SC_SMALL_N; i++) { s_sc_key_s[i] = 0; s_sc_use_s[i] = 0; }
}
/* one lookup per command: STATE_AT(key, n) finds the slot, state_rd /
 * state_wr copy through it */
#define STATE_AT(key, n)       int16_t *const st_slot_ = (n) == 80 ? sc_big(key) : sc_small(key)
#define state_rd(dst, key, n)  memcpy((dst), st_slot_, (n))
#define state_wr(key, src, n)  memcpy(st_slot_, (src), (n))

/* The ADPCM decoder instructions exist ("ADS3" build of the unit): the
 * codebook is then mirrored into the CFU at every aLoadADPCM. */
static int s_adp_hw;
uint32_t abi_adp_cfu_frames;   /* ADPCM frames decoded on the DSP instructions */

static void adp_load_book(uint32_t nbytes)
{
    const u32_alias *t = (const u32_alias *)rspa.adpcm_table;
    ACFUS(ACFU_ADP_TRST, 0, 0);
    for (uint32_t w = 0; w < (nbytes + 7) / 8; w++) ACFU(ACFU_ADP_TLOAD, t[2 * w], t[2 * w + 1]);
}

ABI_TEXT2 ABI_SMALL void abi_reset(void)
{
    memset(&rspa, 0, sizeof rspa);
    memset(&na, 0, sizeof na);
    memset(&ne, 0, sizeof ne);
    memset(&s_buf, 0, sizeof s_buf);
    memset(abi_op_count, 0, sizeof abi_op_count);
    sc_reset();
    s_adp_hw = ABI_HAVE_CFU && ACFUS(ACFU_ID, 0, 0) == ACFU_ID_VALUE;
    if (s_adp_hw) adp_load_book(sizeof rspa.adpcm_table);
}

/* ---- commands --------------------------------------------------------- */

/* One 16-sample VADPCM frame (a header byte, 8 data bytes) in C. */
ABI_TEXT2 ABI_SMALL static void adpcm_frame(const uint8_t *in, int16_t *out)
{
    int shift = *in >> 4;
    int table_index = *in++ & 0xf;
    int16_t (*tbl)[8] = rspa.adpcm_table[table_index];
    for (int i = 0; i < 2; i++) {
        int16_t ins[8];
        int16_t prev1 = out[-1];
        int16_t prev2 = out[-2];
        for (int j = 0; j < 4; j++) {
            ins[j * 2] = (int16_t)((((*in >> 4) << 28) >> 28) << shift);
            ins[j * 2 + 1] = (int16_t)((((*in++ & 0xf) << 28) >> 28) << shift);
        }
        for (int j = 0; j < 8; j++) {
            int32_t acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + (ins[j] << 11);
            for (int k = 0; k < j; k++) acc += tbl[1][(j - k) - 1] * ins[k];
            acc >>= 11;
            *out++ = clamp16(acc);
        }
    }
}

/* One 16-sample frame of 2-bit VADPCM (a header byte, 4 data bytes: four
 * samples a byte, high bits first), as adpcm_frame(). */
ABI_TEXT2 ABI_SMALL static void adpcm_frame2(const uint8_t *in, int16_t *out)
{
    int shift = *in >> 4;
    int16_t (*tbl)[8] = rspa.adpcm_table[*in++ & 0xf];
    for (int i = 0; i < 2; i++) {
        int16_t ins[8];
        int16_t prev1 = out[-1], prev2 = out[-2];
        for (int j = 0; j < 8; j++) {
            int v = (in[j >> 2] >> (6 - 2 * (j & 3))) & 3;
            ins[j] = (int16_t)((((v << 30) >> 30) << (shift > 14 ? 14 : shift)));
        }
        in += 2;
        for (int j = 0; j < 8; j++) {
            int32_t acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + (ins[j] << 11);
            for (int k = 0; k < j; k++) acc += tbl[1][(j - k) - 1] * ins[k];
            *out++ = clamp16(acc >> 11);
        }
    }
}

/* n bytes of the stream at N64 DMEM address a (through BX) */
static void __attribute__((noinline)) adpcm_fetch(uint8_t *f, uint32_t a, int n)
{
    for (int k = 0; k < n; k++) f[k] = *BUF_U8(BX(a + (uint32_t)k));
}

static void a_adpcm(uint8_t flags, uintptr_t state)
{
    STATE_AT(state, 32);
    uint32_t ia = rspa.in;                  /* the stream's N64 DMEM address: its bytes via BX */
    uint8_t in[9];
#define ADPCM_FETCH(n) do { adpcm_fetch(in, ia, (n)); ia += (n); } while (0)
    int16_t *out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_32(rspa.nbytes);
    if (flags & ABI_F_INIT) {
        memset(out, 0, 16 * sizeof(int16_t));
    } else if (flags & ABI_F_LOOP) {
        dram_rd16(out, rspa.adpcm_loop_state, 16 * sizeof(int16_t));
    } else {
        state_rd(out, state, 16 * sizeof(int16_t));
    }
    out += 16;
    if (flags & 4) {                        /* EAD: 2-bit samples (5-byte frames) */
        for (; nbytes > 0; nbytes -= 16 * sizeof(int16_t)) {
            ADPCM_FETCH(5);
            adpcm_frame2(in, out);
            out += 16;
        }
    } else if (s_adp_hw && ((uintptr_t)out & 3) == 0) {
        /* on the DSP instructions: the CFU keeps the history between groups
         * and answers two samples per instruction */
        u32_alias *o = (u32_alias *)out;
        ACFU(ACFU_ADP_START, *(const u32_alias *)(out - 2), 0);
        for (; nbytes > 0; nbytes -= 16 * sizeof(int16_t)) {
            ADPCM_FETCH(9);
            uint32_t hdr = in[0];
            if (hdr & 8) {                 /* a table index past the book: C */
                adpcm_frame(in, (int16_t *)o);
                o += 8;
                ACFU(ACFU_ADP_START, o[-1], 0);
                continue;
            }
            for (int i = 0; i < 2; i++, o += 4) {
                const uint8_t *g = in + 1 + 4 * i;
                uint32_t d = (uint32_t)g[0] << 24 | (uint32_t)g[1] << 16 | (uint32_t)g[2] << 8 | g[3];
                o[0] = ACFU(ACFU_ADP_GRP, ACFU_HSWAP(hdr), ACFU_HSWAP(d));
                o[1] = ACFU(ACFU_ADP_NXT, 0, 0);
                o[2] = ACFU(ACFU_ADP_NXT, 0, 0);
                o[3] = ACFU(ACFU_ADP_NXT, 0, 0);
            }
            abi_adp_cfu_frames++;
        }
        out = (int16_t *)o;
    } else {
        for (; nbytes > 0; nbytes -= 16 * sizeof(int16_t)) {
            ADPCM_FETCH(9);
            adpcm_frame(in, out);
            out += 16;
        }
    }
#undef ADPCM_FETCH
    state_wr(state, out - 16, 16 * sizeof(int16_t));
}

static void a_resample(uint8_t flags, uint16_t pitch, uintptr_t state_addr)
{
    STATE_AT(state_addr, 32);
    int16_t tmp[16] = {0};
    int16_t state[16];
    int16_t *in_initial = BUF_S16(rspa.in);
    int16_t *in = in_initial;
    int16_t *out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_16(rspa.nbytes);
    uint32_t pitch_accumulator;
    int i;
    if (flags & ABI_F_INIT) {
        memset(tmp, 0, 5 * sizeof(int16_t));
    } else {
        state_rd(tmp, state_addr, 16 * sizeof(int16_t));
    }
    if (flags & 2) {
        memcpy(in - 8, tmp + 8, 8 * sizeof(int16_t));
        in -= tmp[5] >> 1;
    }
    in -= 4;
    pitch_accumulator = (uint16_t)tmp[4];
    memcpy(in, tmp, 4 * sizeof(int16_t));

    /* main loop on the DSP instruction: 4 taps, rounding, clamp and the
     * pitch accumulator in one RS_OUT per output sample */
    if (ABI_HAVE_CFU) {
    ACFUS(ACFU_RS_CFG, (uint32_t)pitch << 1, pitch_accumulator);
    do {
        for (i = 0; i < 8; i++) {
            uint32_t r = ACFU(ACFU_RS_OUT, ACFU_PAIR(in[0], in[1]), ACFU_PAIR(in[2], in[3]));
            *out++ = (int16_t)ACFU_LO(r);
            in += ACFU_HI(r);
        }
        nbytes -= 8 * sizeof(int16_t);
    } while (nbytes > 0);
    pitch_accumulator = ACFUS(ACFU_RS_ACC, 0, 0);
    } else do {
        for (i = 0; i < 8; i++) {
            const int16_t *tbl = acfu_resample_table[pitch_accumulator * 64 >> 16];
            int32_t sample = ((in[0] * tbl[0] + 0x4000) >> 15) +
                             ((in[1] * tbl[1] + 0x4000) >> 15) +
                             ((in[2] * tbl[2] + 0x4000) >> 15) +
                             ((in[3] * tbl[3] + 0x4000) >> 15);
            *out++ = clamp16(sample);
            pitch_accumulator += (uint32_t)pitch << 1;
            in += pitch_accumulator >> 16;
            pitch_accumulator &= 0xffff;
        }
        nbytes -= 8 * sizeof(int16_t);
    } while (nbytes > 0);

    /* The state block is read-modify-written: mixer.c only stores [0..5] and
     * [8..15] here, [6..7] keep what the DRAM block held. */
    if (flags & ABI_F_INIT) state_rd(state, state_addr, 16 * sizeof(int16_t));
    else memcpy(state, tmp, sizeof state);
    state[4] = (int16_t)pitch_accumulator;
    memcpy(state, in, 4 * sizeof(int16_t));
    i = (int)(in - in_initial + 4) & 7;
    in -= i;
    if (i != 0) i = -8 - i;
    state[5] = (int16_t)i;
    memcpy(state + 8, in, 8 * sizeof(int16_t));
    state_wr(state_addr, state, 16 * sizeof(int16_t));
}

/* The envelope mixer on the DSP instructions (audio_cfu.h): the CFU holds
 * the 16 lane volumes for the whole call and mixes a word (two samples) per
 * ENV_MIX2, walking send -> lane pair -> channel -> block like the C loop
 * below walks send -> lane -> channel -> block. The two orders differ only
 * in when a lane's dry and wet writes happen relative to its neighbour's, so
 * the buffers must not overlap; they must also be word aligned. Returns 0
 * -- the C does it -- when that or a value's range does not hold. */
typedef uint32_t __attribute__((may_alias)) u32_word;
static inline int overlaps(const int16_t *x, const int16_t *y, int n) { return x < y + n && y < x + n; }
static int env_cfu(const int16_t *in, int16_t *dry[2], int16_t *wet[2], int aux, int nbytes,
                   int32_t vols[2][8], const int16_t target[2], const int32_t rate[2],
                   int16_t vol_dry, int16_t vol_wet)
{
    if (!ABI_HAVE_CFU) return 0;
    int ns = ((nbytes + 15) & ~15) / 2;
    for (int c = 0; c < 2; c++) {
        if (target[c] < 0 || rate[c] < 0 || rate[c] > 0x1FFFF) return 0;
        for (int i = 0; i < 8; i++) if (vols[c][i] < 0) return 0;
    }
    const int16_t *bufs[5] = { in, dry[0], dry[1], wet[0], wet[1] };
    int nb = aux ? 5 : 3;
    for (int x = 0; x < nb; x++) {
        if ((uintptr_t)bufs[x] & 3) return 0;
        for (int y = x + 1; y < nb; y++) if (overlaps(bufs[x], bufs[y], ns)) return 0;
    }
    ACFUS(ACFU_ENV_CFG_B, (uint16_t)vol_dry, (uint16_t)vol_wet);
    ACFUS(ACFU_ENV_CFG_A, rate[0], (uint16_t)target[0]);
    ACFUS(ACFU_ENV_CFG_A, (uint32_t)rate[1] | 0x80000000u, (uint16_t)target[1]);
    for (int l = 0; l < 16; l++) ACFUS(ACFU_ENV_VSET, l, vols[l >> 3][l & 7]);
    ACFUS(ACFU_ENV_START, aux, 0);
    const u32_word *iw = (const u32_word *)in;
    u32_word *d0 = (u32_word *)dry[0], *d1 = (u32_word *)dry[1];
    u32_word *w0 = (u32_word *)wet[0], *w1 = (u32_word *)wet[1];
    for (int blk = ns / 8; blk > 0; blk--) {
        /* unrolled: the loop counters were 4 of every 11 instructions */
        if (aux) {
#pragma GCC unroll 4
            for (int p = 0; p < 4; p++) { uint32_t x = iw[p]; d0[p] = ACFU(ACFU_ENV_MIX2, d0[p], x); w0[p] = ACFU(ACFU_ENV_MIX2, w0[p], x); }
#pragma GCC unroll 4
            for (int p = 0; p < 4; p++) { uint32_t x = iw[p]; d1[p] = ACFU(ACFU_ENV_MIX2, d1[p], x); w1[p] = ACFU(ACFU_ENV_MIX2, w1[p], x); }
            w0 += 4; w1 += 4;
        } else {
#pragma GCC unroll 4
            for (int p = 0; p < 4; p++) d0[p] = ACFU(ACFU_ENV_MIX2, d0[p], iw[p]);
#pragma GCC unroll 4
            for (int p = 0; p < 4; p++) d1[p] = ACFU(ACFU_ENV_MIX2, d1[p], iw[p]);
        }
        iw += 4; d0 += 4; d1 += 4;
    }
    for (int l = 0; l < 16; l++) vols[l >> 3][l & 7] = (int32_t)ACFUS(ACFU_ENV_VGET, l, 0);
    return 1;
}

static void a_env_mixer(uint8_t flags, uintptr_t state_addr)
{
    STATE_AT(state_addr, 80);
    int16_t *in = BUF_S16(rspa.in);
    int16_t *dry[2] = { BUF_S16(rspa.out), BUF_S16(rspa.dry_right) };
    int16_t *wet[2] = { BUF_S16(rspa.wet_left), BUF_S16(rspa.wet_right) };
    int nbytes = ROUND_UP_16(rspa.nbytes);
    int16_t state[40];
    int16_t target[2];
    int32_t rate[2];
    int16_t vol_dry, vol_wet;
    int32_t step_diff[2];
    int32_t vols[2][8];
    int c, i;

    if (flags & ABI_F_INIT) {
        target[0] = rspa.target[0];
        target[1] = rspa.target[1];
        rate[0] = rspa.rate[0];
        rate[1] = rspa.rate[1];
        vol_dry = rspa.vol_dry;
        vol_wet = rspa.vol_wet;
        step_diff[0] = rspa.vol[0] * (rate[0] - 0x10000) / 8;
        step_diff[1] = rspa.vol[0] * (rate[1] - 0x10000) / 8;   /* vol[0] twice: as the reference */
        for (i = 0; i < 8; i++) {
            vols[0][i] = clamp32((int64_t)(rspa.vol[0] << 16) + step_diff[0] * (i + 1));
            vols[1][i] = clamp32((int64_t)(rspa.vol[1] << 16) + step_diff[1] * (i + 1));
        }
    } else {
        state_rd(state, state_addr, 80);
        memcpy(vols[0], state, 32);
        memcpy(vols[1], state + 16, 32);
        target[0] = state[32];
        target[1] = state[35];
        rate[0] = (state[33] << 16) | (uint16_t)state[34];
        rate[1] = (state[36] << 16) | (uint16_t)state[37];
        vol_dry = state[38];
        vol_wet = state[39];
    }

    /* Same arithmetic and order as mixer.c (block, then channel, then lane),
     * reorganised for a scalar core without an FPU or a fast 64-bit path:
     *  - a lane's gains depend only on (vols >> 16), so they are computed once
     *    per lane and block (they were already), and not at all again while a
     *    lane's volume cannot change -- rate exactly 1.0, the steady state;
     *  - vols * rate >> 16 with its int32 clamp is mul + mulh and a sign test
     *    instead of a 64-bit multiply, shift and compare. */
    const int inc[2] = { (rate[0] >> 16) > 0, (rate[1] >> 16) > 0 };
    const int aux = (flags & ABI_F_AUX) != 0;
    if (env_cfu(in, dry, wet, aux, nbytes, vols, target, rate, vol_dry, vol_wet)) { abi_env_cfu_calls++; goto save_state; }
    do {
        /* Same arithmetic and order as mixer.c (block, channel, lane) --
         * the fallback when env_cfu() cannot take it; vols * rate >> 16
         * with its int32 clamp as mul + mulh and a sign test; x * 0x7fff
         * as (x << 15) - x. */
        for (c = 0; c < 2; c++) {
            int16_t *d = dry[c], *wt = wet[c];
            const int32_t rc = rate[c], tg = target[c];
            int32_t *vc = vols[c];
            for (i = 0; i < 8; i++) {
                int32_t v = vc[i];
                if (inc[c] ? (v >> 16) > tg : (v >> 16) < tg) v = tg << 16;
                int32_t vi = v >> 16;
                int32_t gdi = (vi * vol_dry + 0x4000) >> 15;
                int32_t x = d[i];
                d[i] = clamp16(((x << 15) - x + in[i] * gdi + 0x4000) >> 15);
                if (aux) {
                    int32_t gwi = (vi * vol_wet + 0x4000) >> 15;
                    x = wt[i];
                    wt[i] = clamp16(((x << 15) - x + in[i] * gwi + 0x4000) >> 15);
                }
                if (rc != 0x10000) {
                    int64_t p64 = (int64_t)v * rc;
                    int32_t hi = (int32_t)(p64 >> 32);
                    uint32_t lo = (uint32_t)p64;
                    int32_t r = (int32_t)(((uint32_t)hi << 16) | (lo >> 16));
                    if ((hi >> 15) != (r >> 31)) r = hi < 0 ? (-0x7fffffff - 1) : 0x7fffffff;
                    v = r;
                }
                vc[i] = v;
            }
            dry[c] += 8;
            if (aux) wet[c] += 8;
        }
        nbytes -= 16;
        in += 8;
    } while (nbytes > 0);

save_state:
    memcpy(state, vols[0], 32);
    memcpy(state + 16, vols[1], 32);
    state[32] = target[0];
    state[35] = target[1];
    state[33] = (int16_t)(rate[0] >> 16);
    state[34] = (int16_t)rate[0];
    state[36] = (int16_t)(rate[1] >> 16);
    state[37] = (int16_t)rate[1];
    state[38] = vol_dry;
    state[39] = vol_wet;
    state_wr(state_addr, state, 80);
}

static void a_mix(int16_t gain, uint16_t in_addr, uint16_t out_addr)
{
    int nbytes = ROUND_UP_32(rspa.nbytes);
    int16_t *in = BUF_S16(in_addr);
    int16_t *out = BUF_S16(out_addr);
    if (gain == -0x8000) {
        while (nbytes > 0) {
            for (int i = 0; i < 16; i++) { *out = clamp16(*out - *in++); out++; }
            nbytes -= 16 * sizeof(int16_t);
        }
    }
    while (nbytes > 0) {
        for (int i = 0; i < 16; i++) {
            int32_t x = *out;
            int32_t sample = (((x << 15) - x + *in++ * gain) + 0x4000) >> 15;
            *out++ = clamp16(sample);
        }
        nbytes -= 16 * sizeof(int16_t);
    }
}

static void a_interleave(uint16_t left, uint16_t right)
{
    int count = ROUND_UP_16(rspa.nbytes) / (int)sizeof(int16_t) / 8;
    int16_t *l = BUF_S16(left);
    int16_t *r = BUF_S16(right);
    int16_t *d = BUF_S16(rspa.out);
    while (count > 0) {
        int16_t lv[8], rv[8];
        for (int k = 0; k < 8; k++) lv[k] = *l++;
        for (int k = 0; k < 8; k++) rv[k] = *r++;
        for (int k = 0; k < 8; k++) { *d++ = lv[k]; *d++ = rv[k]; }
        --count;
    }
}

static void a_set_volume(uint8_t flags, int16_t v, int16_t t, int16_t r)
{
    if (flags & ABI_F_AUX) {
        rspa.vol_dry = v;
        rspa.vol_wet = r;
    } else if (flags & ABI_F_VOL) {
        if (flags & ABI_F_LEFT) rspa.vol[0] = v;
        else rspa.vol[1] = v;
    } else {
        if (flags & ABI_F_LEFT) {
            rspa.target[0] = v;
            rspa.rate[0] = (int32_t)((uint32_t)(uint16_t)t << 16 | (uint16_t)r);
        } else {
            rspa.target[1] = v;
            rspa.rate[1] = (int32_t)((uint32_t)(uint16_t)t << 16 | (uint16_t)r);
        }
    }
}

/* ---- naudio (n_aspMain, the n_audio library's microcode) ------------------
 * The command set many 1998-2000 games use (Pokemon Stadium, ...): ABI1's
 * operations with fixed DMEM buffers and a fixed count (0x170 bytes, 184
 * samples) instead of SETBUFF, a linear envelope ramp, and a 10-byte
 * resampler state. Encodings and the envelope mixer after mupen64plus-rsp-hle
 * (alist_naudio.c, alist.c: GPL-2.0+, used as the reference); the ADPCM
 * decoder, the resampler's arithmetic and MIXER's are this file's (the RSP's,
 * as the SM64 checks have them). The state blocks the game hands over are
 * written at their own sizes: naudio's resampler keeps 10 bytes, so it does
 * not go through the 32-byte DMEM state cache. On the audio core this code
 * lives in the second IMEM bank (ABI_TEXT2). */
#define NA_COUNT  0x170
#define NA_MAIN   0x4f0
#define NA_MAIN2  0x660
#define NA_DRY_L  0x9d0
#define NA_DRY_R  0xb40
#define NA_WET_L  0xcb0
#define NA_WET_R  0xe20

ABI_TEXT2 ABI_SMALL void abi_select(int kind)
{
    if (kind == s_abi) return;
    abi_state_flush();          /* (NA_OVERLAY: naudio's buffers overwrite the envelope slots) */
    sc_reset();
    s_abi = kind;
}

ABI_TEXT2 ABI_SMALL int abi_identify(uint32_t d00, uint32_t d10, uint32_t d28, uint32_t d30)
{
    if (d00 == 1u && d30 == 0xf0000f00u) return d28 == 0x1e24138cu ? ABI_KIND_ABI1 : -1;
    if (d10 == 0x0000127cu) return ABI_KIND_NAUDIO;
    if (d10 == 0x11181350u) return ABI_KIND_NEAD_MK;      /* Mario Kart 64 (and rsp-hle: Wave Race (E)) */
    if (d10 == 0x110412ccu) return ABI_KIND_NEAD_SF;      /* Star Fox 64 (except J) */
    if (d10 == 0x1f681230u) return ABI_KIND_NEAD_OOT;     /* Ocarina of Time (and MM J) */
    if (d10 == 0x1f801250u) return ABI_KIND_NEAD_OOT;     /* Majora's Mask (U, E): OoT's table, 3 a no-op */
    return -1;
}

/* a linear ramp: Q16 value, step per sample, stops at the target (mupen's
 * ramp_step, in 32 bits: a sum that overflows is past any int32 target) */
typedef struct { int32_t value, step, target; } na_ramp_t;
static inline int16_t na_ramp(na_ramp_t *r)
{
    int32_t v;
    int ovf = __builtin_add_overflow(r->value, r->step, &v);
    if (ovf || (r->step <= 0 ? v <= r->target : v >= r->target)) { v = r->target; r->step = 0; }
    r->value = v;
    return (int16_t)(v >> 16);
}

/* dst += in * g >> 15, clamped; nothing to do for a zero gain */
ABI_TEXT2 static void na_mixg(int16_t *dst, const int16_t *in, int32_t g, int n)
{
    if (!g) return;
    if ((uintptr_t)dst & 2) { *dst = clamp16(*dst + ((*in * g) >> 15)); dst++; in++; n--; }
    u32_alias *d = (u32_alias *)dst;
    const u32_alias *x = (const u32_alias *)in;       /* (the buffers are word aligned alike) */
    for (; n >= 2; n -= 2, d++, x++) {                /* two samples a word */
        uint32_t dw = *d, xw = *x;
        int32_t lo = clamp16((int16_t)dw + (((int16_t)xw * g) >> 15));
        int32_t hi = clamp16((int16_t)(dw >> 16) + (((int32_t)xw >> 16) * g >> 15));
        *d = (uint16_t)lo | (uint32_t)hi << 16;
    }
    if (n) { dst = (int16_t *)d; in = (const int16_t *)x; *dst = clamp16(*dst + ((*in * g) >> 15)); }
}

ABI_TEXT2 static void na_envmix(uint8_t flags, uintptr_t addr)
{
    int16_t save[40] = {0};
    na_ramp_t r[2];
    int16_t dry = na.dry, wet = na.wet;
    const int16_t *in = BUF_S16(NA_MAIN);
    int16_t *dl = BUF_S16(NA_DRY_L), *dr = BUF_S16(NA_DRY_R), *wl = BUF_S16(NA_WET_L), *wr = BUF_S16(NA_WET_R);
    if (flags & ABI_F_INIT) {
        for (int c = 0; c < 2; c++) {
            r[c].step = na.rate[c] / 8;
            r[c].value = (int32_t)((uint32_t)na.vol[c] << 16);
            r[c].target = (int32_t)((uint32_t)na.target[c] << 16);
        }
    } else {
        dram_rd(save, addr, 80);
        wet = save[0]; dry = save[2];
        for (int c = 0; c < 2; c++) {
            r[c].target = (int32_t)((uint32_t)(uint16_t)save[4 + 2 * c] << 16);
            r[c].step = (int32_t)((uint32_t)(uint16_t)save[8 + 2 * c] << 16 | (uint16_t)save[9 + 2 * c]);
            r[c].value = (int32_t)((uint32_t)(uint16_t)save[16 + 2 * c] << 16 | (uint16_t)save[17 + 2 * c]);
        }
    }
    /* per sample while a ramp moves; once both have stopped (a step of 0
     * leaves the value fixed from the next sample on) the gains are constant */
    int k = 0;
    int32_t g0 = 0, g1 = 0, g2 = 0, g3 = 0;
    for (; k < NA_COUNT / 2; k++) {
        int32_t lv = na_ramp(&r[0]), rv = na_ramp(&r[1]), x = in[k];
        g0 = clamp16((lv * dry + 0x4000) >> 15); g1 = clamp16((rv * dry + 0x4000) >> 15);
        g2 = clamp16((lv * wet + 0x4000) >> 15); g3 = clamp16((rv * wet + 0x4000) >> 15);
        dl[k] = clamp16(dl[k] + ((x * g0) >> 15));
        dr[k] = clamp16(dr[k] + ((x * g1) >> 15));
        wl[k] = clamp16(wl[k] + ((x * g2) >> 15));
        wr[k] = clamp16(wr[k] + ((x * g3) >> 15));
        if (!r[0].step && !r[1].step) { k++; break; }
    }
    if (k < NA_COUNT / 2) {
        int n = NA_COUNT / 2 - k;
        na_mixg(dl + k, in + k, g0, n); na_mixg(dr + k, in + k, g1, n);
        na_mixg(wl + k, in + k, g2, n); na_mixg(wr + k, in + k, g3, n);
    }
    save[0] = wet; save[2] = dry;
    for (int c = 0; c < 2; c++) {
        save[4 + 2 * c] = (int16_t)(r[c].target >> 16);
        save[8 + 2 * c] = (int16_t)((uint32_t)r[c].step >> 16); save[9 + 2 * c] = (int16_t)r[c].step;
        save[16 + 2 * c] = (int16_t)((uint32_t)r[c].value >> 16); save[17 + 2 * c] = (int16_t)r[c].value;
    }
    dram_wr(addr, save, 80);
}

/* the resampler: this file's arithmetic, a 10-byte state (4 samples, the pitch
 * accumulator) -- naudio's and EAD's: those microcodes write no more, and OoT
 * keeps its own data in the rest of the block (the game's command lists
 * changed once this wrote ABI1's 32 bytes) */
ABI_TEXT2 static void na_resample(uint8_t flags, uint16_t pitch, uint16_t dmemi, uint16_t dmemo, uintptr_t addr, int count)
{
    int16_t st[5];
    int16_t *in = BUF_S16(dmemi) - 4, *out = BUF_S16(dmemo);
    uint32_t acc;
    if (flags & ABI_F_INIT) { memset(in, 0, 8); acc = 0; }
    else { dram_rd(st, addr, 10); memcpy(in, st, 8); acc = (uint16_t)st[4]; }
    if (ABI_HAVE_CFU) {                         /* the DSP instruction, as a_resample */
        ACFUS(ACFU_RS_CFG, (uint32_t)pitch << 1, acc);
        for (int n = 0; n < count; n++) {
            uint32_t r = ACFU(ACFU_RS_OUT, ACFU_PAIR(in[0], in[1]), ACFU_PAIR(in[2], in[3]));
            *out++ = (int16_t)ACFU_LO(r);
            in += ACFU_HI(r);
        }
        acc = ACFUS(ACFU_RS_ACC, 0, 0);
    } else for (int n = 0; n < count; n++) {
        const int16_t *tbl = acfu_resample_table[acc >> 10];
        int32_t sample = ((in[0] * tbl[0] + 0x4000) >> 15) + ((in[1] * tbl[1] + 0x4000) >> 15) +
                         ((in[2] * tbl[2] + 0x4000) >> 15) + ((in[3] * tbl[3] + 0x4000) >> 15);
        *out++ = clamp16(sample);
        acc += (uint32_t)pitch << 1;
        in += acc >> 16;
        acc &= 0xffff;
    }
    memcpy(st, in, 8);
    st[4] = (int16_t)acc;
    dram_wr(addr, st, 10);
}

/* MIXER: this file's arithmetic (the RSP's), naudio's exact count */
ABI_TEXT2 static void na_mix(int16_t gain, uint16_t dmemi, uint16_t dmemo)
{
    const int16_t *in = BUF_S16(dmemi);
    int16_t *out = BUF_S16(dmemo);
    for (int i = 0; i < NA_COUNT / 2; i++) {
        int32_t x = out[i];
        out[i] = clamp16(((x << 15) - x + in[i] * gain + 0x4000) >> 15);
    }
}

ABI_TEXT2 ABI_SMALL static void na_exec(uint32_t w0, uintptr_t w1)
{
    uint32_t op = (w0 >> 24) & 0xf, lo = (uint32_t)w1;
    uintptr_t addr = w1 & 0xffffff;
    switch (op) {
    case 1: {                                                   /* ADPCM */
        rspa.in = (uint16_t)(((lo >> 12) & 0xf) + NA_MAIN);
        rspa.out = (uint16_t)((lo & 0xfff) + NA_MAIN);
        rspa.nbytes = (uint16_t)((lo >> 16) & 0xfff);
        rspa.adpcm_loop_state = na.loop;
        a_adpcm((uint8_t)(lo >> 28), w0 & 0xffffff);
        break;
    }
    case 2:                                                     /* CLEARBUFF */
        memset(BUF_U8((uint16_t)(w0 + NA_MAIN) & 0xfff), 0, lo & 0xfff);
        break;
    case 3:                                                     /* ENVMIXER */
        na.vol[1] = (int16_t)w0;
        na_envmix((uint8_t)(w0 >> 16), addr);
        break;
    case 4:                                                     /* LOADBUFF */
        dram_rd(BUF_U8(((w0 & 0xfff) + NA_MAIN) & ~3u), addr & ~7u, ROUND_UP_8((w0 >> 12) & 0xfff));
        break;
    case 5:                                                     /* RESAMPLE */
        na_resample((uint8_t)(lo >> 30) & ABI_F_INIT, (uint16_t)(lo >> 14), (uint16_t)(((lo >> 2) & 0xfff) + NA_MAIN),
                    (lo & 3) ? NA_MAIN2 : NA_MAIN, w0 & 0xffffff, NA_COUNT / 2);
        break;
    case 6:                                                     /* SAVEBUFF */
        dram_wr(addr & ~7u, BUF_U8(((w0 & 0xfff) + NA_MAIN) & ~3u), ROUND_UP_8((w0 >> 12) & 0xfff));
        break;
    case 9: {                                                   /* SETVOL */
        uint8_t f = (uint8_t)(w0 >> 16);
        if (f & ABI_F_VOL) {
            if (f & ABI_F_LEFT) { na.vol[0] = (int16_t)w0; na.dry = (int16_t)(lo >> 16); na.wet = (int16_t)lo; }
            else { na.target[1] = (int16_t)w0; na.rate[1] = (int32_t)lo; }
        } else { na.target[0] = (int16_t)w0; na.rate[0] = (int32_t)lo; }
        break;
    }
    case 10:                                                    /* DMEMMOVE */
        memmove(BUF_U8((uint16_t)((lo >> 16) + NA_MAIN) & 0xfff), BUF_U8((uint16_t)(w0 + NA_MAIN) & 0xfff),
                ((lo & 0xffff) + 3) & ~3u);
        break;
    case 11: {                                                  /* LOADADPCM */
        uint32_t n = w0 & 0xffff;
        if (n > sizeof rspa.adpcm_table) n = sizeof rspa.adpcm_table;
        dram_rd16(rspa.adpcm_table, addr, n);
        if (s_adp_hw) adp_load_book(n);
        break;
    }
    case 12:                                                    /* MIXER */
        na_mix((int16_t)w0, (uint16_t)((lo >> 16) + NA_MAIN), (uint16_t)(lo + NA_MAIN));
        break;
    case 13:                                                    /* INTERLEAVE */
        rspa.out = NA_MAIN; rspa.nbytes = NA_COUNT;
        a_interleave(NA_DRY_L, NA_DRY_R);
        break;
    case 14:                                                    /* (at 0x12b0, inside SETVOL) */
        na.rate[1] = (int32_t)(((uint32_t)na.rate[1] & ~0xffffu) | (lo & 0xffff));
        break;
    case 15:                                                    /* SETLOOP */
        na.loop = addr;
        break;
    default:                                                    /* SPNOOP; 7, 8: unknown, nothing */
        break;
    }
}

/* ---- EAD (the SM64 engine's successors' aspMain: Mario Kart 64, Star Fox
 * 64, Ocarina of Time) ----------------------------------------------------
 * ABI1's operations addressed in DMEM directly (SETBUFF's in/out/count, or
 * the command's own), a Q16 envelope mixer stepped every 8 samples, 2-bit
 * ADPCM, and some small DMEM operations; the three games' command numbers
 * differ (ne_ops). Encodings after mupen64plus-rsp-hle (alist_nead.c,
 * alist.c: GPL-2.0+, used as the reference, as for naudio); the ADPCM
 * decoder, the resampler's and MIXER's arithmetic are this file's. */
enum { NE_NOP, NE_ADPCM, NE_CLEAR, NE_ADDMIX, NE_RESAMPLE, NE_ZOH, NE_FILTER, NE_SETBUFF, NE_DUP,
       NE_MOVE, NE_LOADADPCM, NE_MIX, NE_ILV_MK, NE_ILV, NE_POLEF, NE_HILO, NE_SETLOOP, NE_BLOCKS,
       NE_INTERL, NE_ENV1_MK, NE_ENV1, NE_ENVMIX_MK, NE_ENVMIX, NE_LOAD, NE_SAVE, NE_ENV2 };
static const uint8_t ne_ops[3][27] = {
    {   /* Mario Kart 64 */
        NE_NOP, NE_ADPCM, NE_CLEAR, NE_NOP, NE_NOP, NE_RESAMPLE, NE_NOP, NE_NOP,
        NE_SETBUFF, NE_NOP, NE_MOVE, NE_LOADADPCM, NE_MIX, NE_ILV_MK, NE_POLEF, NE_SETLOOP,
        NE_BLOCKS, NE_INTERL, NE_ENV1_MK, NE_ENVMIX_MK, NE_LOAD, NE_SAVE, NE_ENV2, NE_NOP,
        NE_NOP, NE_NOP, NE_NOP },
    {   /* Star Fox 64 */
        NE_NOP, NE_ADPCM, NE_CLEAR, NE_NOP, NE_ADDMIX, NE_RESAMPLE, NE_ZOH, NE_NOP,
        NE_SETBUFF, NE_NOP, NE_MOVE, NE_LOADADPCM, NE_MIX, NE_ILV_MK, NE_POLEF, NE_SETLOOP,
        NE_BLOCKS, NE_INTERL, NE_ENV1, NE_ENVMIX, NE_LOAD, NE_SAVE, NE_ENV2, NE_NOP,
        NE_HILO, NE_NOP, NE_DUP },
    {   /* Ocarina of Time */
        NE_NOP, NE_ADPCM, NE_CLEAR, NE_NOP, NE_ADDMIX, NE_RESAMPLE, NE_ZOH, NE_FILTER,
        NE_SETBUFF, NE_DUP, NE_MOVE, NE_LOADADPCM, NE_MIX, NE_ILV, NE_HILO, NE_SETLOOP,
        NE_BLOCKS, NE_INTERL, NE_ENV1, NE_ENVMIX, NE_LOAD, NE_SAVE, NE_ENV2, NE_NOP,
        NE_NOP, NE_NOP, NE_NOP },
};

/* the envelope mixer: in (count samples) into dry L/R and wet L/R, the
 * volumes stepping every 8 samples; xors invert a send */
ABI_TEXT2 static void ne_envmix(uint32_t w0, uint32_t w1, int mk)
{
    const int16_t *in = BUF_S16((w0 >> 12) & 0xff0);
    int16_t *dl = BUF_S16((w1 >> 20) & 0xff0), *dr = BUF_S16((w1 >> 12) & 0xff0);
    int16_t *wl = BUF_S16((w1 >> 4) & 0xff0), *wr = BUF_S16((w1 << 4) & 0xff0);
    int x0 = -(int)((w0 >> 1) & 1), x1 = -(int)(w0 & 1), x2 = 0, x3 = 0;
    if (!mk) {
        x2 = -(int)((w0 >> 3) & 1); x3 = -(int)((w0 >> 2) & 1);
        if ((w0 >> 4) & 1) { int16_t *t = wl; wl = wr; wr = t; }
    }
    int n = (int)(((w0 >> 8) & 0xff) + 7) & ~7;
    for (; n > 0; n -= 8) {
        for (int i = 0; i < 8; i++) {
            int16_t l = (int16_t)((((int32_t)in[i] * ne.env[0]) >> 16) ^ x0);
            int16_t r = (int16_t)((((int32_t)in[i] * ne.env[1]) >> 16) ^ x1);
            int16_t l2 = (int16_t)((((int32_t)l * ne.env[2]) >> 16) ^ x2);
            int16_t r2 = (int16_t)((((int32_t)r * ne.env[2]) >> 16) ^ x3);
            dl[i] = clamp16(dl[i] + l); dr[i] = clamp16(dr[i] + r);
            wl[i] = clamp16(wl[i] + l2); wr[i] = clamp16(wr[i] + r2);
        }
        for (int k = 0; k < 3; k++) ne.env[k] = (uint16_t)(ne.env[k] + ne.step[k]);
        in += 8; dl += 8; dr += 8; wl += 8; wr += 8;
    }
}

/* the 2-pole filter (the codebook's first 16 entries are its taps): count
 * bytes of samples, its last two outputs kept at state + 4 */
/* DMEM to DMEM, a halfword at a time (the buffers are halfword aligned; an
 * inlined memcpy would use swl/swr, which the audio core does not have) */
ABI_TEXT2 static void ne_copy(uint16_t dst, uint16_t src, uint32_t n)
{
    u16_alias *d = (u16_alias *)BUF_U8(dst);
    const u16_alias *s = (const u16_alias *)BUF_U8(src);
    for (n >>= 1; n > 0; n--) *d++ = *s++;
}
ABI_TEXT2 ABI_SMALL static void ne_polef(int init, uint16_t gain, uintptr_t state)
{
    int16_t *h1 = rspa.adpcm_table[0][0], *h2 = h1 + 8, h2b[8] ABI_W, st[4] ABI_W = {0};
    const int16_t *in = BUF_S16(ne.in);
    int16_t *out = BUF_S16(ne.out);
    if (!init) dram_rd(st, state, 8);
    int16_t l1 = st[2], l2 = st[3];
    for (int i = 0; i < 8; i++) { h2b[i] = h2[i]; h2[i] = (int16_t)(((int32_t)h2[i] * gain) >> 14); }
    for (int n = (ne.count + 15) & ~15; n > 0; n -= 16) {
        int16_t f[8];
        for (int i = 0; i < 8; i++) f[i] = in[i];
        for (int i = 0; i < 8; i++) {
            int32_t acc = f[i] * (int32_t)gain + h1[i] * l1 + h2b[i] * l2;
            for (int k = 0; k < i; k++) acc += h2[k] * f[i - 1 - k];
            out[i] = clamp16(acc >> 14);
        }
        l1 = out[6]; l2 = out[7];
        in += 8; out += 8;
    }
    dram_wr(state, out - 4, 8);
}

/* the 8-tap FIR: its taps the average of two tables (written back to both,
 * as the microcode does), the last 8 input samples kept at state */
ABI_TEXT2 ABI_SMALL static void ne_filter(uint16_t dmem, uintptr_t state)
{
    int16_t c[8] ABI_W, c5[8] ABI_W, x[16] ABI_W, out[8] ABI_W;
    int16_t *buf = BUF_S16(dmem);
    dram_rd16(c, ne.filter_lut[0], 16);
    dram_rd16(c5, ne.filter_lut[1], 16);
    for (int i = 0; i < 8; i++) c[i] = c5[i] = (int16_t)((c[i] + c5[i]) >> 1);
    abi_n64_wr16(ne.filter_lut[0], c, 16);
    abi_n64_wr16(ne.filter_lut[1], c5, 16);
    dram_rd(x, state, 16);                          /* the previous block */
    for (int n = 0; n < ne.filter_count; n += 16, buf += 8) {
        for (int i = 0; i < 8; i++) x[8 + i] = buf[i];
        for (int i = 0; i < 8; i++) {
            int32_t acc = 0;
            for (int k = 0; k < 8; k++) acc += x[8 + i - k] * c[k];
            out[i] = (int16_t)((acc + 0x4000) >> 15);
        }
        for (int i = 0; i < 8; i++) { x[i] = x[8 + i]; buf[i] = out[i]; }
    }
    dram_wr(state, x, 16);
}

ABI_TEXT2 ABI_SMALL static void ne_exec(uint32_t w0, uintptr_t w1)
{
    uint32_t op = (w0 >> 24) & 0x7f, lo = (uint32_t)w1;
    uintptr_t addr = w1 & 0xffffff;
    int mk = s_abi == ABI_KIND_NEAD_MK;
    if (op >= 27) return;
    switch (ne_ops[s_abi - ABI_KIND_NEAD_MK][op]) {
    case NE_ADPCM:
        rspa.in = ne.in; rspa.out = ne.out; rspa.nbytes = ne.count; rspa.adpcm_loop_state = ne.loop;
        a_adpcm((uint8_t)(w0 >> 16) & 7, addr);
        break;
    case NE_CLEAR:
        memset(BUF_U8(w0 & 0xffff), 0, lo & 0xfff);
        break;
    case NE_ADDMIX: {
        const int16_t *in = BUF_S16(lo >> 16);
        int16_t *out = BUF_S16(lo & 0xffff);
        for (int n = ((w0 >> 12) & 0xff0) >> 1; n > 0; n--, out++) *out = clamp16(*out + *in++);
        break;
    }
    case NE_RESAMPLE:
        na_resample((uint8_t)(w0 >> 16) & ABI_F_INIT, (uint16_t)w0, ne.in, ne.out, addr, ((ne.count + 15) & ~15) >> 1);
        break;
    case NE_ZOH: {                                  /* zero-order hold: the pitch steps the input */
        const int16_t *in = BUF_S16(ne.in);
        int16_t *out = BUF_S16(ne.out);
        uint32_t pitch = (w0 & 0xffff) << 1, acc = lo & 0xffff, ip = 0;
        for (int n = ne.count >> 1; n > 0; n--) {
            *out++ = in[ip];
            acc += pitch; ip += acc >> 16; acc &= 0xffff;
        }
        break;
    }
    case NE_FILTER:
        if (((w0 >> 16) & 0xff) > 1) { ne.filter_count = (uint16_t)w0; ne.filter_lut[0] = addr; }
        else { ne.filter_lut[1] = addr + 0x10; ne_filter((uint16_t)w0, addr); }
        break;
    case NE_SETBUFF:
        ne.in = (uint16_t)w0; ne.out = (uint16_t)(lo >> 16); ne.count = (uint16_t)lo;
        break;
    case NE_DUP: {                                  /* 128 bytes, count times (read once, first) */
        int16_t t[64] ABI_W;
        uint16_t o = (uint16_t)(lo >> 16), s = (uint16_t)(w0 & 0xffff);
        for (int k = 0; k < 64; k++) t[k] = BUF_S16(s)[k];
        for (int n = (w0 >> 16) & 0xff; n > 0; n--, o += 128) for (int k = 0; k < 64; k++) BUF_S16(o)[k] = t[k];
        break;
    }
    case NE_MOVE: {                                 /* bytes, ascending (as the microcode: overlap smears) */
        uint8_t *d = BUF_U8(lo >> 16);
        const uint8_t *s = BUF_U8(w0 & 0xffff);
        uint32_t n = lo & 0xffff;
        if (n) for (n = (n + 3) & ~3u; n > 0; n--) *d++ = *s++;
        break;
    }
    case NE_LOADADPCM: {
        uint32_t n = w0 & 0xffff;
        if (n > sizeof rspa.adpcm_table) n = sizeof rspa.adpcm_table;
        dram_rd16(rspa.adpcm_table, addr, n);
        if (s_adp_hw) adp_load_book(n);
        break;
    }
    case NE_MIX: {                                  /* ABI1's arithmetic, the command's count */
        const int16_t *in = BUF_S16(lo >> 16);
        int16_t *out = BUF_S16(lo & 0xffff);
        int16_t gain = (int16_t)w0;
        for (int n = ((w0 >> 12) & 0xff0) >> 1; n > 0; n--, out++) {
            int32_t x = *out;
            *out = clamp16(((x << 15) - x + *in++ * gain + 0x4000) >> 15);
        }
        break;
    }
    case NE_ILV_MK:
        if (!ne.count) break;
        rspa.out = ne.out; rspa.nbytes = ne.count;
        a_interleave((uint16_t)(lo >> 16), (uint16_t)lo);
        break;
    case NE_ILV:
        rspa.out = (uint16_t)w0; rspa.nbytes = (uint16_t)((w0 >> 12) & 0xff0);
        a_interleave((uint16_t)(lo >> 16), (uint16_t)lo);
        break;
    case NE_POLEF:
        if (ne.count) ne_polef((w0 >> 16) & ABI_F_INIT, (uint16_t)w0, addr);
        break;
    case NE_HILO: {                                 /* a Q4.4 gain */
        int16_t *d = BUF_S16(lo >> 16);
        int32_t g = (int8_t)(w0 >> 16);
        for (int n = (w0 & 0xfff) >> 1; n > 0; n--, d++) *d = clamp16((*d * g) >> 4);
        break;
    }
    case NE_SETLOOP:
        ne.loop = addr;
        break;
    case NE_BLOCKS: {                               /* count blocks of block_size bytes, 32 at a time */
        uint16_t i = (uint16_t)w0, o = (uint16_t)(lo >> 16);
        int blocks = (w0 >> 16) & 0xff;
        do {
            int left = lo & 0xffff;
            do { ne_copy(o, i, 32); i += 32; o += 32; left -= 32; } while (left > 0);
        } while (--blocks > 0);
        break;
    }
    case NE_INTERL: {                               /* every other sample */
        const int16_t *in = BUF_S16(lo >> 16);
        int16_t *out = BUF_S16(lo & 0xffff);
        for (int n = w0 & 0xffff; n > 0; n--, in += 2) *out++ = *in;
        break;
    }
    case NE_ENV1_MK:
    case NE_ENV1:
        ne.env[2] = (uint16_t)((w0 >> 8) & 0xff00);
        ne.step[2] = mk ? 0 : (uint16_t)w0;
        ne.step[0] = (uint16_t)(lo >> 16); ne.step[1] = (uint16_t)lo;
        break;
    case NE_ENVMIX_MK:
    case NE_ENVMIX:
        ne_envmix(w0, lo, mk);
        break;
    case NE_LOAD:
        dram_rd(BUF_U8((w0 & 0xfff) & ~3u), addr & ~7u, ROUND_UP_8((w0 >> 12) & 0xfff));
        break;
    case NE_SAVE:
        dram_wr(addr & ~7u, BUF_U8((w0 & 0xfff) & ~3u), ROUND_UP_8((w0 >> 12) & 0xfff));
        break;
    case NE_ENV2:
        ne.env[0] = (uint16_t)(lo >> 16); ne.env[1] = (uint16_t)lo;
        break;
    default:                                        /* SPNOOP, SEGMENT (flat addresses), the unknown */
        break;
    }
}


void abi_exec(uint32_t w0, uintptr_t w1)
{
    if (s_abi == ABI_KIND_NAUDIO) { na_exec(w0, w1); return; }
    if (s_abi >= ABI_KIND_NEAD_MK) { ne_exec(w0, w1); return; }
    uint32_t op = (w0 >> 24) & 0xff;
    uint32_t lo = (uint32_t)w1;
    if (op < 16) abi_op_count[op]++;
    switch (op) {
    case ABI_ADPCM:
        a_adpcm((uint8_t)(w0 >> 16), w1);
        break;
    case ABI_CLEARBUFF:
        memset(BUF_U8((uint16_t)(w0 & 0xffffff)), 0, ROUND_UP_16(lo));
        break;
    case ABI_ENVMIXER:
        a_env_mixer((uint8_t)(w0 >> 16), w1);
        break;
    case ABI_LOADBUFF:
        dram_rd(BUF_U8(rspa.in), w1, ROUND_UP_8(rspa.nbytes));
        break;
    case ABI_RESAMPLE:
        a_resample((uint8_t)(w0 >> 16), (uint16_t)w0, w1);
        break;
    case ABI_SAVEBUFF:
        dram_wr(w1, BUF_S16(rspa.out), ROUND_UP_8(rspa.nbytes));
        break;
    case ABI_SETBUFF:
        if ((w0 >> 16) & ABI_F_AUX) {
            rspa.dry_right = (uint16_t)w0;
            rspa.wet_left = (uint16_t)(lo >> 16);
            rspa.wet_right = (uint16_t)lo;
        } else {
            rspa.in = (uint16_t)w0;
            rspa.out = (uint16_t)(lo >> 16);
            rspa.nbytes = (uint16_t)lo;
        }
        break;
    case ABI_SETVOL:
        a_set_volume((uint8_t)(w0 >> 16), (int16_t)w0, (int16_t)(lo >> 16), (int16_t)lo);
        break;
    case ABI_DMEMMOVE:
        memmove(BUF_U8((uint16_t)(lo >> 16)), BUF_U8((uint16_t)(w0 & 0xffffff)), ROUND_UP_16(lo & 0xffff));
        break;
    case ABI_LOADADPCM: {
        uint32_t n = w0 & 0xffffff;
        if (n > sizeof rspa.adpcm_table) n = sizeof rspa.adpcm_table;
        dram_rd16(rspa.adpcm_table, w1, n);
        if (s_adp_hw) adp_load_book(n);
        break;
    }
    case ABI_MIXER:
        a_mix((int16_t)w0, (uint16_t)(lo >> 16), (uint16_t)lo);
        break;
    case ABI_INTERLEAVE:
        a_interleave((uint16_t)(lo >> 16), (uint16_t)lo);
        break;
    case ABI_SETLOOP:
        rspa.adpcm_loop_state = w1;
        break;
    default:   /* SPNOOP, SEGMENT (flat addresses), POLEF (unused by SM64) */
        break;
    }
}
