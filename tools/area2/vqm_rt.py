#!/usr/bin/env python3
# route-through LUTs: lcell_comb with a single data input whose lut_mask is a buffer, feeding a dffeas .d
import re,sys,collections
txt=open(sys.argv[1],errors='ignore').read()
depth=int(sys.argv[2]) if len(sys.argv)>2 else 4
cells=re.findall(r'cyclonev_lcell_comb\s+(\S+)\s*\((.*?)\);\s*(defparam.*?)(?=\n\n)',txt,re.S)
dff_d=set(re.findall(r'dffeas\s+\S+\s*\(.*?\.d\(([^)]*)\)',txt,re.S))
dff_d={x.strip() for x in dff_d}
cnt=collections.Counter(); tot=0; stems=collections.Counter()
for name,ports,defs in cells:
    ins=re.findall(r'\.(data[a-g]|cin|sharein)\(',ports)
    outs=re.findall(r'\.(combout|sumout|cout|shareout)\(([^)]*)\)',ports)
    if len(ins)==1 and outs and outs[0][0]=='combout' and outs[0][1].strip() in dff_d:
        m=re.search(r'lut_mask = "([0-9A-F]+)"',defs)
        parts=re.sub(r'(_I|~I)$','',name.lstrip('\\')).split('|')
        h='|'.join(x.split(':')[-1] for x in parts[:-1][:depth])
        stem=re.sub(r'\d+','#',re.sub(r'\[\d+\]','[]',parts[-1]))
        cnt[h]+=1; tot+=1; stems[(h,stem)]+=1
print("route-through (single-input, feeds a dff):",tot)
for h,c in cnt.most_common(25): print(f"{c:6d} {h}")
print("--- by stem")
for (h,s),c in stems.most_common(40): print(f"{c:6d} {h} :: {s}")
