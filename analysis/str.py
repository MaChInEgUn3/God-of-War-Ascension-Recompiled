import re,sys,collections
d=open(sys.argv[1],"rb").read()
pat=re.compile(rb"[ -~]{5,}")
hits=collections.OrderedDict()
kw=re.compile(rb"spu|spurs|job|edge|mlaa|jobchain|taskset|workload|cellSpurs|\.elf|\.self|\.sprx|gcm|rsx|vdec|bink|havok|wwise|fmod|lua|psarc|wad",re.I)
for m in pat.finditer(d):
    s=m.group()
    if kw.search(s): hits[s]=m.start()
print(len(hits))
for s,o in list(hits.items())[:150]: print("0x%x %s"%(o,s.decode()[:110]))
