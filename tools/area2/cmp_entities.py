#!/usr/bin/env python3
# cmp_entities.py old.fit.rpt new.fit.rpt: ALMs / regs / M10K / DSP by entity (fitter), old vs new
import sys,re
def load(f):
    L=open(f,errors='ignore').read().split('\n'); out={}
    st=[i for i,l in enumerate(L) if l.startswith('; Fitter Resource Utilization by Entity')][0]
    for l in L[st+4:]:
        if not l.startswith(';'): break
        c=[x.strip() for x in l.split(';')[1:-1]]
        def tv(s):
            m=re.match(r'(-?[\d.]+) \((-?[\d.]+)\)',s); return (float(m.group(1)),float(m.group(2))) if m else (float(s),0.0)
        out[c[0]]=(tv(c[1]),tv(c[7]),int(c[10]),int(c[11]),int(c[9]) if c[9].isdigit() else 0)
    return out
a=load(sys.argv[1]); b=load(sys.argv[2])
rows=[('apf_top (whole design)','|apf_top'),('  core_top (all but pads)','|core_top:ic|'),
('  game CPU (mips_core, with the FPU)','|mips_core:cpu|'),('    FPU (fpu_n64)','|fpu_n64:fpu|'),
('  RDP (rdp_top, total)','|rdp_top:rdp|'),('    rdp_pix (pixel pipe)','|rdp_pix:pix|'),('    rdp_mem (memory engine)','|rdp_mem:mem|'),
('  RSP (rsp_top, total)','|rsp_top:rsp|'),('    rsp_vu (vector unit)','|rsp_vu:vu|'),('    rsp_su (scalar unit)','|rsp_su:su|'),
('  n64_video (VI scan-out path, AA/dedither)','|n64_video:video|'),('  mirlo_regs (CSR block, incl. cmdq)','|mirlo_regs:regs|'),
('  rcp_ai','|rcp_ai:ai|'),('  rcp_si','|rcp_si:si|'),('  rcp_vi','|rcp_vi:vi|'),('  rcp_mi (MI/PI)','|rcp_mi:mi|'),('  rdp_dpc','|rdp_dpc:dpc|'),
('  cpu_port (CPU clock crossing)','|cpu_port:pc|'),('  SDRAM arb','|sdr_arb:arb|'),('  SDRAM ctrl','|sdr_ctrl:sdram|'),('  SDRAM phy','|sdr_phy:phy|'),
('  core_bridge_cmd (APF)','|core_bridge_cmd:icb|'),('  synch_3 to_bridge_s','|synch_3:to_bridge_s|'),('  synch_3 from_bridge_s','|synch_3:from_bridge_s|'),
('  io_pad_controller','|io_pad_controller:ipm|'),('  io_bridge_peripheral','|io_bridge_peripheral:ibs|'),('  audio','|audio:audio|'),('  gb_cart','|gb_cart:gbcart|')]
print(f"{'entity':44s} {'ALMs old':>9s} {'new':>8s} {'d':>6s} | {'regs old':>8s} {'new':>6s} | M10K {'old':>3s} {'new':>3s} | DSP {'old':>3s} {'new':>3s}")
for n,k in rows:
    if k not in a or k not in b: continue
    (x,_),(r0,_),m0,d0,_=a[k]; (y,_),(r1,_),m1,d1,_=b[k]
    print(f"{n:44s} {x:9.0f} {y:8.0f} {y-x:+6.0f} | {r0:8.0f} {r1:6.0f} | {m0:8d} {m1:3d} | {d0:7d} {d1:3d}")
