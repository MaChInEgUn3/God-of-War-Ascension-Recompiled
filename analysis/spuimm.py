import struct,collections,sys
d=open(sys.argv[1],"rb").read()
LO,HI=0x681880,0x7f3380
tx=0x10230-0x10000; n=0x617710//4
w=struct.unpack(">%dI"%n,d[tx:tx+n*4])
hits=collections.Counter()
for i,x in enumerate(w):
    if x>>26==15 and ((x>>16)&31)==0:  # lis rD,imm
        hi=x&0xffff
        if 0x68<=hi<=0x7f:
            rd=(x>>21)&31
            for j in range(i+1,min(i+10,n)):
                y=w[j]
                if y>>26==14 and ((y>>16)&31)==rd:   # addi
                    lo=y&0xffff; lo=lo-0x10000 if lo&0x8000 else lo
                    v=(hi<<16)+lo
                elif y>>26==24 and ((y>>21)&31)==rd:  # ori
                    v=(hi<<16)|(y&0xffff)
                else: continue
                if LO<=v<HI: hits[v]+=1
                break
print("distinct immediate targets:",len(hits))
t=sorted(hits)
print([ (hex(a),hits[a]) for a in t[:60]])
