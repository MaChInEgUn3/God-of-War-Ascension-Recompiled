"""Contact sheet: sheet.py "<glob of .ppm>" <out.png> -- up to 9 evenly spaced frames, 3x3, 426x240 each."""
import zlib,struct,sys,glob
allf=sorted(glob.glob(sys.argv[1])); files=[allf[round(i*(len(allf)-1)/8)] for i in range(9)] if len(allf)>9 else allf
TW,TH,C=426,240,3
W,H=TW*C,TH*((len(files)+C-1)//C)
canvas=bytearray(W*H*3)
for k,fn in enumerate(files):
    d=open(fn,'rb').read(); p=d.split(b'\n',3); w,h=map(int,p[1].split()); px=p[3]
    ox,oy=(k%C)*TW,(k//C)*TH
    for y in range(TH):
        sy=y*h//TH
        for x in range(TW):
            sx=x*w//TW; si=(sy*w+sx)*3; di=((oy+y)*W+ox+x)*3
            canvas[di:di+3]=px[si:si+3]
raw=b''.join(b'\x00'+bytes(canvas[y*W*3:(y+1)*W*3]) for y in range(H))
def chunk(t,b): return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
open(sys.argv[2],'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))
print([f.split('_')[-1] for f in files])
