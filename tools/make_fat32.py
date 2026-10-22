#!/usr/bin/env python3
"""Build 64 MiB MBR/FAT32 USB image with EFI/BOOT/BOOTX64.EFI and BOOTIA32.EFI.
This is a boot compatibility PROBE. It does not launch the freestanding BIOS kernel.
Write image only to image file. NEVER automatically write removable or physical drives.
"""
import struct
import sys
from pathlib import Path
x64, ia32, output = [Path(arg) for arg in sys.argv[1:4]]
size_sectors=131072
first_lba=2048
partition_sectors=size_sectors-first_lba
reserved=32
fat_copies=2
sectors_per_cluster=1
fat_sectors=1009
cluster_count=partition_sectors-reserved-fat_copies*fat_sectors
assert cluster_count>65525
first_data=first_lba+reserved+fat_copies*fat_sectors
blob=bytearray(size_sectors*512)
def le16(x):return struct.pack('<H',x)
def le32(x):return struct.pack('<I',x)
def put(lba, byte_offset, data):
    start=lba*512+byte_offset
    blob[start:start+len(data)] = data
def put_cluster(number, data):
    start=(first_data+(number-2))*512
    blob[start:start+len(data)] = data
assert (len(x64.read_bytes())<=128*512 and len(ia32.read_bytes())<=128*512)
part=bytearray(16)
part[0]=0x00;part[1:4]=b'\xFE\xFF\xFF';part[4]=0x0c;part[5:8]=b'\xFE\xFF\xFF'
part[8:12]=le32(first_lba);part[12:16]=le32(partition_sectors)
put(0,446,part);put(0,510,b'\x55\xAA')
b=bytearray(512);b[0:3]=b'\xEB\x58\x90';b[3:11]=b'CLAWOS  '
b[11:13]=le16(512);b[13]=sectors_per_cluster;b[14:16]=le16(reserved)
b[16]=fat_copies;b[17:19]=le16(0);b[19:21]=le16(0);b[21]=0xf8;b[22:24]=le16(0)
b[24:26]=le16(63);b[26:28]=le16(255);b[28:32]=le32(first_lba)
b[32:36]=le32(partition_sectors);b[36:40]=le32(fat_sectors)
b[40:42]=le16(0);b[42:44]=le16(0);b[44:48]=le32(2)
b[48:50]=le16(1);b[50:52]=le16(6)
b[64]=0x80;b[66]=0x29;b[67:71]=le32(0xc1a00502)
b[71:82]=b'CLAWOS EFI ';b[82:90]=b'FAT32   ';b[510:512]=b'\x55\xAA'
put(first_lba,0,b);put(first_lba+6,0,b)
info=bytearray(512);info[0:4]=le32(0x41615252);info[484:488]=le32(0x61417272)
info[488:492]=le32(0xffffffff);info[492:496]=le32(0xffffffff);info[508:512]=le32(0xaa550000)
put(first_lba+1,0,info);put(first_lba+7,0,info)
next_cluster=5
fat={2:0x0fffffff,3:0x0fffffff,4:0x0fffffff}
def allocate(data):
    global next_cluster
    chunks=[data[i:i+512] for i in range(0,len(data),512)]
    if not chunks:chunks=[b'']
    start=next_cluster
    for i,c in enumerate(chunks):
        cluster=next_cluster;next_cluster+=1
        fat[cluster] = next_cluster if i+1<len(chunks) else 0x0fffffff
        put_cluster(cluster,c)
    return start
xstart=allocate(x64.read_bytes());istart=allocate(ia32.read_bytes())
def shortentry(name,cluster,byte_size,attr):
    assert len(name)==11
    e=bytearray(32);e[:11]=name.encode('ascii');e[11]=attr
    e[20:22]=le16((cluster>>16)&0xffff);e[26:28]=le16(cluster&0xffff)
    e[28:32]=le32(byte_size)
    return e
put_cluster(2,shortentry('EFI        ',3,0,0x10))
put_cluster(3,shortentry('.          ',3,0,0x10)+shortentry('..         ',2,0,0x10)+shortentry('BOOT       ',4,0,0x10))
put_cluster(4,shortentry('.          ',4,0,0x10)+shortentry('..         ',3,0,0x10)+
    shortentry('BOOTX64 EFI',xstart,len(x64.read_bytes()),0x20)+shortentry('BOOTIA32EFI',istart,len(ia32.read_bytes()),0x20))
fat_bytes=bytearray(fat_sectors*512)
for n,value in {0:0x0ffffff8,1:0x0fffffff,**fat}.items():
    fat_bytes[n*4:n*4+4]=le32(value)
for lba in (first_lba+reserved,first_lba+reserved+fat_sectors):put(lba,0,fat_bytes)
output.write_bytes(blob)
print(f'Built FAT32 EFI USB probe image: {output} ({len(blob)} bytes, BOOTX64+BOOTIA32)')
