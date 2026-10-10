#!/usr/bin/env python3
"""The runtime's TLB instructions (lang/n64rt/n64rt_os.c: tlb_write, tlb_read, tlb_probe, as the game's tlbwi / tlbwr, tlbr and
tlbp reach them in the monitor level) against the VR4300's registers as the reference model has them, on two million random
operations: all page masks, ASIDs, global and not, writes at any of the 32 entries. The functions are cut out of n64rt_os.c
and compiled for the host, the COP0 registers a model of the core's (the masks mtc0 applies).

    tlb_insn_host.py            (needs gcc; exits 1 on a difference)
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "n64rt_os.c")
HARNESS = r'''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint32_t u32; typedef int32_t s32;
#define TLB_N 32
static u32 cp0[32];
#define CP0_GET(reg, v) ((v) = cp0[reg])
#define CP0_SET(reg, v) (cp0[reg] = (reg)==10 ? ((v) & 0xFFFFE0FFu) : (reg)==0 ? ((v) & 0x8000003Fu) : (reg)==2||(reg)==3 ? ((v) & 0x3FFFFFFFu) : (reg)==5 ? ((v) & 0x01FFE000u) : (v))
static struct { u32 va, span, pa[2]; s32 asid; } s_tlb[TLB_N];
static u32 s_tlb_n, s_tlb_hits, s_tlb_fills; static int flushes;
static void tr_flush(void) { flushes++; }
#define n64rt_log(...) ((void)0)
/*SPLICE*/
/* the VR4300's instructions on its registers as the reference model (MIRLO64 sim/n64) has them: tlbr reads entry Index & 31 into
 * PageMask, EntryHi and both EntryLo (G in both: their AND); tlbwi writes PageMask & 0x01FFE000, EntryHi without the bits under the
 * mask and 0x1F00, and both EntryLo; tlbp takes the first entry whose VPN2 under its mask and whose ASID (or both G) match */
typedef struct { u32 mask, hi, lo0, lo1; } ref_t;
static ref_t rtlb[32]; static u32 rcp[32];
static void r_tlbr(void){ u32 i=rcp[0]&31u; const ref_t*e=&rtlb[i]; u32 g=e->lo0&e->lo1&1u; rcp[5]=e->mask; rcp[10]=e->hi; rcp[2]=(e->lo0&~1u)|g; rcp[3]=(e->lo1&~1u)|g; }
static void r_tlbw(u32 i){ if(i>=32) return; ref_t*e=&rtlb[i]; e->mask=rcp[5]&0x01FFE000u; e->hi=rcp[10]&~(e->mask|0x1F00u); e->lo0=rcp[2]; e->lo1=rcp[3]; }
static void r_tlbp(void){ u32 hi=rcp[10]; rcp[0]=0x80000000u; for(int i=0;i<32;i++){ const ref_t*e=&rtlb[i]; u32 vpn=~(e->mask|0x1FFFu); if((hi&vpn)==(e->hi&vpn)&&((e->lo0&e->lo1&1u)||(hi&0xFFu)==(e->hi&0xFFu))){rcp[0]=(u32)i;break;} } }
static u32 rnd32(void){ static u32 x=88172645u; x^=x<<13; x^=x>>17; x^=x<<5; return x; }
static const u32 masks[]={0,0x6000,0x1E000,0x7E000,0x1FE000,0x7FE000,0x1FFE000};
int main(void)
{
    for (u32 i = 0; i < 32; i++) { s_tlbr[i].hi = 0; }
    for (int i=0;i<32;i++){ rtlb[i].hi=0x80000000u; }
    long bad=0, ops=0;
    for (int n=0;n<2000000;n++){
        u32 k=rnd32()%10;
        /* random registers (as the mtc0's leave them) */
        u32 hi = (rnd32() & 0xFFFFE0FFu), lo0=rnd32()&0x3FFFFFFFu, lo1=rnd32()&0x3FFFFFFFu, pm=masks[rnd32()%7], ix=rnd32()&0x3Fu;
        if (rnd32()&1) hi &= 0xF00000FFu | 0x0F000000u;       /* fewer distinct pages: more matches */
        if (rnd32()%3==0) { lo0&=~1u; }
        if (rnd32()%3==0) { lo1|=1u; lo0|=1u; }
        u32 asids[]={0,1,2,5}; hi=(hi&~0xFFu)|asids[rnd32()%4]; hi=(hi&0xF0FFE0FFu)|((rnd32()%4)<<24);
        if (rnd32()%4) ix=(ix&31u);
        cp0[10]=rcp[10]=hi; cp0[2]=rcp[2]=lo0; cp0[3]=rcp[3]=lo1; cp0[5]=rcp[5]=pm; cp0[0]=rcp[0]=ix|(rnd32()&0x80000000u);
        u32 idx=ix;
        if (k<5) { if ((idx&63u)<TLB_N) tlb_write(idx&63u); r_tlbw(rcp[0]&63u); }
        else if (k<8) { tlb_read(cp0[0]&31u); r_tlbr(); }
        else { tlb_probe(); r_tlbp(); }
        ops++;
        /* after a probe Index has P or the index; after a read the four registers */
        if (k>=8 && cp0[0]!=rcp[0] && !((cp0[0]&0x80000000u)==(rcp[0]&0x80000000u) && (cp0[0]&0x3F)==(rcp[0]&0x3F)&&0)) { bad++; if(bad<10) printf("tlbp: model %08x runtime %08x (hi %08x)\n", rcp[0], cp0[0], hi); }
        if (k>=5 && k<8) for (int r=0;r<4;r++){ int c=(int[]){5,10,2,3}[r]; if(cp0[c]!=rcp[c]){ bad++; if(bad<10) printf("tlbr: reg %d model %08x runtime %08x\n", c, rcp[c], cp0[c]); } }
    }
    printf("%ld operations, %ld differences\n", ops, bad);
    return bad != 0;
}
'''

s = open(SRC).read()
a = s.index("static struct { u32 mask, hi, lo0, lo1; } s_tlbr[TLB_N];")
b = s.index("static int tlb_insn(n64rt_ctx_t *x, int bd)")
blk = "\n".join(l for l in s[a:b].splitlines() if not l.startswith(("#define CP0_GET", "#define CP0_SET")))
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d, "h.c"), "w").write(HARNESS.replace("/*SPLICE*/", blk))
    subprocess.check_call(["gcc", "-O1", "-w", "-o", os.path.join(d, "h"), os.path.join(d, "h.c")])
    sys.exit(subprocess.call([os.path.join(d, "h")]))
