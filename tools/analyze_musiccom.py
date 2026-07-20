"""Disassemble MUSIC.COM and inventory PC-98 I/O accesses for compatibility work."""
from pathlib import Path
import argparse
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

ap=argparse.ArgumentParser();ap.add_argument("binary",type=Path);ap.add_argument("-o","--output",type=Path)
a=ap.parse_args();data=a.binary.read_bytes();md=Cs(CS_ARCH_X86,CS_MODE_16);md.detail=True
lines=[]
for i in md.disasm(data,0x100):
    mark=""
    if i.mnemonic in ("in","out"): mark=" ; I/O"
    lines.append(f"{i.address:04x}: {i.bytes.hex():<16} {i.mnemonic:<7} {i.op_str}{mark}")
text="\n".join(lines)+"\n"
if a.output:a.output.write_text(text,encoding="utf-8")
else:print(text,end="")
