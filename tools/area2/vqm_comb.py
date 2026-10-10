#!/usr/bin/env python3
# vqm_comb.py file.vqm hier-substring [n]: lcell_comb output-name stems inside a hierarchy
import re,sys,collections
f=sys.argv[1]; sub=sys.argv[2]; n=int(sys.argv[3]) if len(sys.argv)>3 else 40
c=collections.Counter()
for l in open(f,errors='ignore'):
    m=re.match(r'^cyclonev_lcell_comb\s+\\?(\S+)',l)
    if not m: continue
    name=re.sub(r'_I$','',m.group(1))
    if sub not in name: continue
    sig=name.split('|')[-1]
    sig=re.sub(r'~\d+$','',sig); sig=re.sub(r'\[\d+\]','[]',sig); sig=re.sub(r'\d+','#',sig)
    c[sig]+=1
for s,k in c.most_common(n): print(f"{k:5d} {s}")
