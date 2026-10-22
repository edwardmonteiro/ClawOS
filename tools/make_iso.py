#!/usr/bin/env python3
"""Create ISO9660 / El Torito BIOS bootable ISO, using only Python stdlib.
No GRUB or Linux runtime appears on the resulting boot medium.
El Torito floppy emulation (1.44MB) works on compatible legacy BIOS/CSM.
UEFI-native Macs must NOT assume this ISO boots; use only on tested hardware.
"""
import struct
import sys
from pathlib import Path
BLOCK = 2048
PVD=16; BVD=17; TERMINATOR=18; PT_LE=19; PT_BE=20; ROOT=21; CATALOG=22; BOOT=23
boot_img = Path(sys.argv[1]).read_bytes()
assert len(boot_img) == 1474560 and boot_img[510:512] == b'\x55\xaa'
readme = ("ClawOS Native v0.2\r\n"
          "Original bare-metal x86 32-bit kernel. No Linux or macOS.\r\n"
          "Legacy BIOS/CSM El Torito floppy-emulation boot medium.\r\n"
          "Network, disks and OpenClaw gateway are NOT implemented.\r\n"
          "Use QEMU for first testing. Do not erase your Mac disk.\r\n").encode('ascii')
BOOT_BLOCKS = (len(boot_img) + BLOCK - 1)//BLOCK
README = BOOT + BOOT_BLOCKS
TOTAL = README + 1
img = bytearray(TOTAL*BLOCK)
def u16both(x):return struct.pack('<H',x)+struct.pack('>H',x)
def u32both(x):return struct.pack('<I',x)+struct.pack('>I',x)
def record(extent, size, flags, name):
    n=len(name)
    length=33+n+(1 if n%2==0 else 0)
    return (bytes([length,0])+u32both(extent)+u32both(size)+
        bytes([126,10,21,0,0,0,0])+bytes([flags,0,0])+u16both(1)+bytes([n])+name+
        (b'\0' if n%2==0 else b''))
rootrec = record(ROOT,BLOCK,2,b'\x00')
root = record(ROOT,BLOCK,2,b'\x00')+record(ROOT,BLOCK,2,b'\x01')+record(README,len(readme),0,b'README.TXT;1')
img[ROOT*BLOCK:ROOT*BLOCK+len(root)] = root
pt = bytes([1,0])+struct.pack('<I',ROOT)+struct.pack('<H',1)+b'\x00\x00'
pt_be = bytes([1,0])+struct.pack('>I',ROOT)+struct.pack('>H',1)+b'\x00\x00'
img[PT_LE*BLOCK:PT_LE*BLOCK+len(pt)]=pt
img[PT_BE*BLOCK:PT_BE*BLOCK+len(pt_be)]=pt_be
pvd = bytearray(BLOCK)
pvd[0:7]=b'\x01CD001\x01';pvd[8:40]=b'CLAWOS'.ljust(32,b' ')
pvd[40:72]=b'CLAWOS_NATIVE_02'.ljust(32,b' ')
pvd[80:88]=u32both(TOTAL);pvd[120:124]=u16both(1);pvd[124:128]=u16both(1)
pvd[128:132]=u16both(BLOCK);pvd[132:140]=u32both(len(pt))
pvd[140:144]=struct.pack('<I',PT_LE);pvd[148:152]=struct.pack('>I',PT_BE)
pvd[156:156+len(rootrec)]=rootrec
for offset in (813,830,847,864):pvd[offset:offset+17]=b'2026102100000000\x00'
pvd[881]=1
img[PVD*BLOCK:(PVD+1)*BLOCK]=pvd
bvd=bytearray(BLOCK);bvd[0:7]=b'\x00CD001\x01';bvd[7:39]=b'EL TORITO SPECIFICATION'.ljust(32,b' ')
bvd[71:75]=struct.pack('<I',CATALOG)
img[BVD*BLOCK:(BVD+1)*BLOCK]=bvd
term=bytearray(BLOCK);term[0:7]=b'\xffCD001\x01';img[TERMINATOR*BLOCK:(TERMINATOR+1)*BLOCK]=term
cat=bytearray(BLOCK)
cat[0]=1;cat[1]=0;cat[4:28]=b'CLAWOS NATIVE'.ljust(24,b' ');cat[30:32]=b'\x55\xaa'
checksum=(-sum(struct.unpack('<16H',cat[:32])))&0xffff
cat[28:30]=struct.pack('<H',checksum)
assert sum(struct.unpack('<16H',cat[:32]))&0xffff==0
cat[32]=0x88;cat[33]=0x02;cat[38:40]=struct.pack('<H',1)
cat[40:44]=struct.pack('<I',BOOT)
img[CATALOG*BLOCK:(CATALOG+1)*BLOCK]=cat
img[BOOT*BLOCK:BOOT*BLOCK+len(boot_img)] = boot_img
img[README*BLOCK:README*BLOCK+len(readme)]=readme
out=Path(sys.argv[2]);out.write_bytes(img)
assert out.stat().st_size == TOTAL*BLOCK
print(f'Built BIOS El Torito ISO: {out} ({TOTAL} sectors; {len(img)} bytes)')
