#!/usr/bin/env python3
# Makes tests/png_fixtures.h (the PNG decoder's test images): python3 tests/make_png_fixtures.py
# Writes tests/png_fixtures.h: small PNGs of every kind, with the RGBA each should decode to.
import os, zlib, struct, random
random.seed(7)
W,H=13,9
def chunk(t,b): return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
def pack_row(samples, depth):
    if depth>=8:
        out=bytearray()
        for s in samples: out+= (s.to_bytes(2,'big') if depth==16 else bytes([s]))
        return out
    out=bytearray(); acc=0; n=0
    for s in samples:
        acc=(acc<<depth)|s; n+=depth
        if n==8: out.append(acc); acc=0; n=0
    if n: out.append(acc<<(8-n))
    return out
def filt(rows, bpp):
    out=bytearray(); prev=None
    for i,row in enumerate(rows):
        f=(i*3+1)%5 if len(rows)>1 else 4
        cur=bytearray(len(row))
        for x in range(len(row)):
            a=row[x-bpp] if x>=bpp else 0; b=prev[x] if prev else 0; c=prev[x-bpp] if prev and x>=bpp else 0
            if f==0: p=0
            elif f==1: p=a
            elif f==2: p=b
            elif f==3: p=(a+b)//2
            else:
                pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
                p=a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            cur[x]=(row[x]-p)&255
        out.append(f); out+=cur; prev=row
    return out
def png(pix, ctype, depth, interlace=0, level=6, extra=b'', chunks_split=False):
    # pix: rows of tuples of samples (per channel)
    ch={0:1,2:3,3:1,4:2,6:4}[ctype]; bits=ch*depth; bpp=max(1,bits//8)
    def rows_of(xs,ys):
        return [pack_row([s for x in xs for s in pix[y][x]],depth) for y in ys]
    if not interlace:
        raw=filt(rows_of(range(W),range(H)),bpp)
    else:
        raw=bytearray()
        for sx,sy,dx,dy in [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]:
            xs=list(range(sx,W,dx)); ys=list(range(sy,H,dy))
            if xs and ys: raw+=filt(rows_of(xs,ys),bpp)
    z=zlib.compress(bytes(raw),level)
    ihdr=struct.pack('>IIBBBBB',W,H,depth,ctype,0,0,interlace)
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)+extra+chunk(b'tEXt',b'Comment\0made for the tests')
    if chunks_split:
        h=len(z)//2; data+=chunk(b'IDAT',z[:h])+chunk(b'IDAT',z[h:])
    else: data+=chunk(b'IDAT',z)
    return data+chunk(b'IEND',b'')
cases=[]
def scale(v,d): return {16:v>>8,8:v,4:v*17,2:v*85,1:v*255}[d]
# RGBA 8
pix=[[tuple(random.randrange(256) for _ in range(4)) for x in range(W)] for y in range(H)]
cases.append(('rgba8',png(pix,6,8),[[p for p in row] for row in pix]))
cases.append(('rgba8_interlaced',png(pix,6,8,1,9),pix))
cases.append(('rgba8_stored',png(pix,6,8,0,0,chunks_split=True),pix))
# RGB 16 with tRNS key
pix=[[tuple(random.randrange(65536) for _ in range(3)) for x in range(W)] for y in range(H)]
key=pix[2][3]
exp=[[(p[0]>>8,p[1]>>8,p[2]>>8,0 if p==key else 255) for p in row] for row in pix]
cases.append(('rgb16_key',png(pix,2,16,extra=chunk(b'tRNS',struct.pack('>HHH',*key))),exp))
# grey 1,2,4,8,16 (some interlaced)
for d in (1,2,4,8,16):
    pix=[[(random.randrange(1<<d),) for x in range(W)] for y in range(H)]
    exp=[[(scale(p[0],d),)*3+(255,) for p in row] for row in pix]
    cases.append(('grey%d'%d,png(pix,0,d,interlace=1 if d in (2,16) else 0),exp))
# grey+alpha 8
pix=[[(random.randrange(256),random.randrange(256)) for x in range(W)] for y in range(H)]
cases.append(('greyalpha8',png(pix,4,8),[[(p[0],p[0],p[0],p[1]) for p in row] for row in pix]))
# palette 1,4,8 with tRNS
for d in (1,4,8):
    n=1<<d if d<8 else 200
    pal=[tuple(random.randrange(256) for _ in range(3)) for _ in range(n)]
    alpha=[random.randrange(256) for _ in range(n//2)]
    pix=[[(random.randrange(n),) for x in range(W)] for y in range(H)]
    exp=[[pal[p[0]]+((alpha[p[0]] if p[0]<len(alpha) else 255),) for p in row] for row in pix]
    extra=chunk(b'PLTE',bytes(c for t in pal for c in t))+chunk(b'tRNS',bytes(alpha))
    cases.append(('palette%d'%d,png(pix,3,d,interlace=1 if d==4 else 0,extra=extra),exp))
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'png_fixtures.h'),'w') as f:
    f.write('/* PNG test images, made by a script with Python\'s zlib: every colour type and bit\n * depth, interlaced and not, with the RGBA each should decode to (13 x 9). */\n')
    f.write('#define PNG_FIX_W %d\n#define PNG_FIX_H %d\n'%(W,H))
    for name,data,exp in cases:
        f.write('static const unsigned char png_%s[%d] = {'%(name,len(data)))
        f.write(','.join(str(b) for b in data)); f.write('};\n')
        f.write('static const unsigned char png_%s_rgba[%d] = {'%(name,W*H*4))
        f.write(','.join(str(c) for row in exp for p in row for c in p)); f.write('};\n')
    f.write('typedef struct png_fixture { const char *name; const unsigned char *data; size_t size; const unsigned char *rgba; } png_fixture;\n')
    f.write('static const png_fixture png_fixtures[] = {\n')
    for name,data,exp in cases: f.write('    { "%s", png_%s, sizeof(png_%s), png_%s_rgba },\n'%(name,name,name,name))
    f.write('};\n')
print(len(cases),'cases')
