#!/usr/bin/env python3
# registers whose D is another register's Q (or a RAM/DSP output): pure pipeline stages; by hierarchy and stem
import re,sys,collections
txt=open(sys.argv[1],errors='ignore').read()
depth=int(sys.argv[2]) if len(sys.argv)>2 else 4
n=int(sys.argv[3]) if len(sys.argv)>3 else 50
# map net -> driver kind
drv={}
for m in re.finditer(r'(cyclonev_lcell_comb|dffeas|cyclonev_ram_block|cyclonev_mac|cyclonev_mlab_cell)\s+(\S+)\s*\((.*?)\);',txt,re.S):
    kind,name,body=m.groups()
    for o in re.finditer(r'\.(q|combout|sumout|cout|shareout|portadataout|portbdataout|dataout|resulta|resultb|result|eab_dataout)\(([^)]*)\)',body):
        drv[o.group(2).strip()]=kind
cnt=collections.Counter(); stems=collections.Counter(); tot=0; allreg=0
for m in re.finditer(r'dffeas\s+(\S+)\s*\((.*?)\);',txt,re.S):
    name,body=m.groups(); allreg+=1
    d=re.search(r'\.d\(([^)]*)\)',body)
    if not d: continue
    k=drv.get(d.group(1).strip(),'pin/other')
    if k=='cyclonev_lcell_comb': continue
    tot+=1
    parts=re.sub(r'(_I|~I)$','',name.lstrip('\\')).split('|')
    h='|'.join(x.split(':')[-1] for x in parts[:-1][:depth])
    stem=re.sub(r'\d+','#',re.sub(r'\[\d+\]','[]',parts[-1]))
    cnt[(h,k)]+=1; stems[(h,stem,k)]+=1
print("registers",allreg,"not fed by a LUT:",tot)
for (h,k),c in cnt.most_common(30): print(f"{c:6d} {k:22s} {h}")
print('---')
for (h,s,k),c in stems.most_common(n): print(f"{c:6d} {k[9:20]:12s} {h} :: {s}")
