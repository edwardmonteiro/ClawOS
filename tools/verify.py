#!/usr/bin/env python3
"""Structural verification only. Does NOT boot or execute the kernel."""
from pathlib import Path
import struct
p=Path('build')
boot=(p/'boot.bin').read_bytes();kernel=(p/'kernel.bin').read_bytes()
floppy=(p/'clawos-native-bios.img').read_bytes()
iso=(p/'clawos-native-bios.iso').read_bytes()
efi=(p/'clawos-efi-probe-usb.img').read_bytes()
assert len(boot)==512 and boot[-2:]==b'\x55\xaa'
assert len(kernel)<61440 and len(floppy)==1474560
assert floppy[:512]==boot and floppy[512:512+len(kernel)]==kernel
for lba,ident in ((16,b'\x01CD001\x01'),(17,b'\x00CD001\x01'),(18,b'\xffCD001\x01')):
    assert iso[lba*2048:lba*2048+7]==ident
cat=iso[22*2048:22*2048+64]
assert cat[0]==1 and cat[30:32]==b'\x55\xaa'
assert sum(struct.unpack('<16H',cat[:32]))&65535==0
assert cat[32]==0x88 and cat[33]==2 and struct.unpack('<I',cat[40:44])[0]==23
assert iso[23*2048:23*2048+len(floppy)]==floppy
assert len(efi)==67108864 and efi[510:512]==b'\x55\xaa'
start=struct.unpack_from('<I',efi,454)[0]
assert start==2048
volume=efi[start*512:]
assert volume[82:90]==b'FAT32   ' and volume[510:512]==b'\x55\xaa'
reserved=struct.unpack_from('<H',volume,14)[0]
fatsectors=struct.unpack_from('<I',volume,36)[0]
spc=volume[13];data_lba=start+reserved+2*fatsectors
cluster=lambda n: efi[(data_lba+(n-2)*spc)*512:(data_lba+(n-1)*spc)*512]
assert cluster(2)[:3]==b'EFI'
assert b'BOOT' in cluster(3)[:128]
assert b'BOOTX64 EFI' in cluster(4)[:128]
assert b'BOOTIA32EFI' in cluster(4)[:128]
for name,machine in (('BOOTX64.EFI',0x8664),('BOOTIA32.EFI',0x14c)):
    data=(p/name).read_bytes()
    assert data[:2]==b'MZ'
    coff=struct.unpack_from('<I',data,0x3c)[0]
    assert data[coff:coff+4]==b'PE\0\0'
    assert struct.unpack_from('<H',data,coff+4)[0]==machine
    assert struct.unpack_from('<H',data,coff+24+68)[0]==10
print('PASS: BIOS signature, kernel payload, ISO9660, El Torito catalog,')
print('      FAT32 boot entries, IA32+X64 EFI PE headers.')
print('NOT TESTED: QEMU boot, Mac mini boot, keyboard, or network.')
