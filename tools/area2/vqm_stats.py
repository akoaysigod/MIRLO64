#!/usr/bin/env python3
# vqm_stats.py file.vqm [depth]: aggregate dffeas (by hierarchy + reg stem) and lcell_comb (by hierarchy + kind)
import re,sys,collections
f=sys.argv[1]; depth=int(sys.argv[2]) if len(sys.argv)>2 else 5
regs=collections.Counter(); comb=collections.Counter(); combk=collections.Counter()
pat=re.compile(r'^(cyclonev_lcell_comb|dffeas|cyclonev_ram_block|cyclonev_mac|cyclonev_mlab_cell)\s+\\?(\S+)\s*\(')
def split(n):
    n=n.rstrip()
    # remove trailing _I / ~I
    n=re.sub(r'(_I|~I)$','',n)
    parts=n.split('|')
    return parts
for l in open(f,errors='ignore'):
    m=pat.match(l)
    if not m: continue
    kind,name=m.groups()
    parts=split(name)
    sig=parts[-1]; hier=parts[:-1]
    h='|'.join(x.split(':')[-1] for x in hier[:depth])
    stem=re.sub(r'\[\d+\]','[]',re.sub(r'~\d+$','',sig))
    stem=re.sub(r'\d+','#',stem)
    if kind=='dffeas': regs[(h,stem)]+=1
    elif kind=='cyclonev_lcell_comb':
        comb[h]+=1
        k=re.sub(r'\d+','#',re.sub(r'\[\d+\]','[]',sig)) 
        combk[(h,k.split('~')[0])]+=1
print("== registers by (hier, stem)")
for (h,s),c in regs.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 60): print(f"{c:6d} {h} :: {s}")
print("== lcell_comb by hier")
for h,c in comb.most_common(40): print(f"{c:6d} {h}")
