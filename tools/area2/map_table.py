#!/usr/bin/env python3
# map_table.py mirlo.map.rpt [thr] [other.map.rpt]: ALUTs / regs by entity from the Analysis & Synthesis report (optionally with a delta vs another)
import sys,re
def load(f):
    L=open(f,errors='ignore').read().split('\n'); out={}; order=[]
    st=[i for i,l in enumerate(L) if l.startswith('; Analysis & Synthesis Resource Utilization by Entity')][0]
    for l in L[st+4:]:
        if not l.startswith(';'): break
        c=[x.strip() for x in l.split(';')[1:-1]]
        def tv(s):
            m=re.match(r'(\d+) \((\d+)\)',s); return (int(m.group(1)),int(m.group(2))) if m else (int(s),0)
        out[c[0]+'#'+c[-3]]=(tv(c[1]),tv(c[2]),int(c[3]),c[4]); order.append(c[0]+'#'+c[-3])
    return out,order
a,order=load(sys.argv[1]); thr=int(sys.argv[2]) if len(sys.argv)>2 else 30
b=load(sys.argv[3])[0] if len(sys.argv)>3 else None
for k in order:
    n=k.split('#')[0]; (al,als),(rg,rgs),bits,dsp=a[k]
    if al<thr and rg<thr: continue
    if 'altsyncram' in n or 'dffpipe' in n: continue
    s=f"{n[:44]:44s} ALUT {al:6d} ({als:5d}) reg {rg:6d} ({rgs:5d}) DSP {dsp}"
    if b and k in b: s+=f"   dALUT {al-b[k][0][0]:+5d} (self {als-b[k][0][1]:+5d}) dreg {rg-b[k][1][0]:+5d}"
    print(s)
