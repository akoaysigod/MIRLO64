/* Audio core firmware: the N64's audio RSP task and its AI, for SM64.
 *
 *   - Tasks: the game CPU hands over an ABI command list (built by
 *     src/audio/synthesis.c) through the mailbox; abi.c executes it. The PCM
 *     it produces lands in SDRAM (aSaveBuffer), like the RSP's did.
 *   - AI: the game queues finished PCM buffers (osAiSetNextBuffer); this
 *     plays them into the APF audio FIFO, resampled from the AI rate (32 kHz
 *     for SM64 US) to the Pocket's fixed 48 kHz, by linear interpolation.
 *
 * Protocol: lang/c/audio/audio_hw.h (AMB_*). The FIFO is kept shallow
 * (FIFO_TARGET) and counted in AI_REMAIN, so the audio engine's own buffer
 * sizing -- which reads osAiGetLength() -- sees what is really queued; it is
 * topped up between commands, so a long task cannot starve it.
 *
 * ABI_N64 (the default build): memory is the N64's order -- the game CPU is
 * big-endian, memory word-native -- so abi.c's transfers go through
 * abi_n64_rd/wr below, and an address can be an N64 game's RDRAM (rdram()).
 * An N64 game also names its microcode's ABI with each task (AMB_TASK_N);
 * the naudio part of the interpreter lives in the second IMEM bank (.text2).
 */
#include <stdint.h>
#include "audio_hw.h"
#include "abi.h"
#include "generated_csr_addrs.h"

/* A game's address: SDRAM as it is (a native Mirlo game, 0x4xxx_xxxx), else
 * the N64's RDRAM (physical, KSEG0 or KSEG1: an N64 game's) -- the game CPU's
 * N64 window, SDRAM 0x4380_0000 (rtl/soc/mips_sys.sv). */
static inline uint32_t rdram(uint32_t a)    { return (a >> 26) == 0x10u ? a : 0x43800000u + (a & 0x7FFFFFu); }

#define FIFO_TARGET 512u            /* 48 kHz samples kept in the APF FIFO (~10.7 ms) */

static inline void     w32(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t r32(uint32_t a)             { return *(volatile uint32_t *)a; }
static inline uint32_t mbox(int i)                 { return r32(AUDIO_MBOX(i)); }
static inline void     set_mbox(int i, uint32_t v) { w32(AUDIO_MBOX(i), v); }
static inline uint32_t cycles(void)                { return r32(AUDIO_CYCLES); }

/* ---- AI ------------------------------------------------------------------ */
static uint32_t s_cur_addr, s_cur_left;     /* current buffer: next frame, bytes left */
static uint32_t s_freq = 32000, s_step;     /* source frames per output sample, Q16 */
static uint32_t s_phase;                    /* Q16 position between s_prev and s_next */
static int32_t  s_prev_l, s_prev_r, s_next_l, s_next_r;
static int      s_have_frame;

/* (freq << 16) / 48000 by restoring division: this core has no divider,
 * and freq << 16 fits in 32 bits (freq < 65536). */
static uint32_t step_for(uint32_t freq)
{
    uint32_t num = freq << 16, q = 0, r = 0;
    for (int b = 31; b >= 0; b--) {
        r = (r << 1) | ((num >> b) & 1u);
        if (r >= 48000u) { r -= 48000u; q |= 1u << b; }
    }
    return q;
}

/* Next source frame (L, R) from the current buffer, switching to the queued
 * one when it runs out. 0 when the AI has nothing left. */

static int fetch_frame(void)
{
    if (s_cur_left < 4) {
        uint32_t next = mbox(AMB_AI_NEXT);
        if (!next) { s_cur_left = 0; return 0; }
        s_cur_left = mbox(AMB_AI_LEN) & ~3u;
        s_cur_addr = rdram(next);
        set_mbox(AMB_AI_NEXT, 0);
        if (s_cur_left < 4) return 0;
    }
    uint32_t w = *(volatile uint32_t *)s_cur_addr;      /* [15:0] left, [31:16] right */
    /* the N64's frame: L then R, big-endian halfwords */
    w = w >> 16 | w << 16;
    s_cur_addr += 4;
    s_cur_left -= 4;
    s_prev_l = s_next_l; s_prev_r = s_next_r;
    s_next_l = (int16_t)w; s_next_r = (int16_t)(w >> 16);
    return 1;
}

static void ai_service(void)
{
    uint32_t f = mbox(AMB_AI_FREQ);
    if (f == 0) f = 32000;
    if (f != s_freq) { s_freq = f; s_step = step_for(f); }

    uint32_t fill = r32(CSR_APF_AUDIO_BUFFER_FILL_ADDR);
    while (fill < FIFO_TARGET) {
        if (!s_have_frame) {                    /* prime two frames */
            if (!fetch_frame() || !fetch_frame()) {
                break;
            }
            s_have_frame = 1;
            s_phase = 0;
        }
        s_phase += s_step;
        int starved = 0;
        while (s_phase >= 0x10000u) {
            s_phase -= 0x10000u;
            if (!fetch_frame()) { starved = 1; break; }
        }
        if (starved) {
            s_have_frame = 0;
            break;
        }
        /* Q15 phase: a 17-bit difference times a 16-bit phase would overflow */
        int32_t ph = (int32_t)(s_phase >> 1);
        int32_t l = s_prev_l + (((s_next_l - s_prev_l) * ph) >> 15);
        int32_t r = s_prev_r + (((s_next_r - s_prev_r) * ph) >> 15);
        w32(CSR_APF_AUDIO_OUT_ADDR, ((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
        fill++;
    }
    /* what the AI still holds, in AI-rate bytes: the current buffer plus the
     * FIFO's content converted back to the source rate */
    set_mbox(AMB_AI_REMAIN, s_cur_left + (((fill * s_step) >> 16) << 2));
}

/* ---- tasks --------------------------------------------------------------- */
static void run_task(uint32_t list, uint32_t n)
{
    uint32_t t0 = cycles();
    abi_op_count[0] = 0;
    for (uint32_t i = 0; i < n; i++, list += 8) {
        uint32_t w0 = r32(rdram(list)), w1 = r32(rdram(list + 4));
        abi_exec(w0, w1);
        if ((i & 7) == 7) ai_service();
    }
    set_mbox(AMB_STATS, cycles() - t0);
}

/* abi.c's RDRAM (ABI_N64): plain transfers keep the N64's byte order. This
 * core is big-endian, as the N64: its DMEM holds the N64's DMEM as it is, so
 * a transfer is a plain copy. The RSP's DMA works in 8-byte units: the
 * aligned case is the one that runs, bytes are the fallback. */
void abi_n64_rd(void *dst, uint32_t addr, uint32_t n)
{
    if (((addr | (uint32_t)(uintptr_t)dst | n) & 3u) == 0) {
        uint32_t *d = dst;
        for (uint32_t i = 0; i < n; i += 4) *d++ = *(volatile uint32_t *)rdram(addr + i);
        return;
    }
    uint8_t *d = dst;
    for (uint32_t i = 0; i < n; i++) d[i] = *(volatile uint8_t *)rdram(addr + i);
}
void abi_n64_wr(uint32_t addr, const void *src, uint32_t n)
{
    if (((addr | (uint32_t)(uintptr_t)src | n) & 3u) == 0) {
        const uint32_t *s = src;
        for (uint32_t i = 0; i < n; i += 4) *(volatile uint32_t *)rdram(addr + i) = *s++;
        return;
    }
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++) *(volatile uint8_t *)rdram(addr + i) = s[i];
}
void abi_n64_rd16(int16_t *dst, uint32_t addr, uint32_t n)
{
    for (uint32_t i = 0; i < n / 2; i++)
        dst[i] = *(volatile int16_t *)rdram(addr + 2 * i);
}

void abi_n64_wr16(uint32_t addr, const int16_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n / 2; i++)
        *(volatile int16_t *)rdram(addr + 2 * i) = src[i];
}


static int s_kind = ABI_KIND_ABI1;
ABI_TEXT2
int main(void)
{
    abi_reset();
    s_step = step_for(s_freq);
    set_mbox(AMB_AI_REMAIN, 0);
    w32(CSR_APF_AUDIO_BUFFER_FLUSH_ADDR, 1);
    w32(CSR_APF_AUDIO_PLAYBACK_EN_ADDR, 1);
    set_mbox(AMB_STATE, AUDIO_ST_ABI);
    for (;;) {
        ai_service();
        uint32_t list = mbox(AMB_TASK);
        if (list) {
            uint32_t n = mbox(AMB_TASK_N);
            /* an N64 game names its microcode's ABI (abi.h ABI_KIND_*) + 1 in
             * the count's top byte (0: as before; a native game: ABI 1) */
            uint32_t k = n >> 24;
            n &= 0xFFFFFFu;
            if (k && (int)(k - 1u) != s_kind) { s_kind = (int)(k - 1u); abi_select(s_kind); }
            set_mbox(AMB_STATE, AUDIO_ST_ABI_BUSY);
            set_mbox(AMB_TASK, 0);          /* taken: the game may queue the next one */
            run_task(list, n);
            set_mbox(AMB_STATE, AUDIO_ST_ABI);
        }
    }
}

