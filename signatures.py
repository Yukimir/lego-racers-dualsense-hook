"""Generate checked prologues from the exact supported retail executable."""
import hashlib, pathlib, struct, sys
data = pathlib.Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest() == 'fe9e881cfa3ffc208db23ab8057ecc5697bad94d2752e8eccf6c79b10a5c1103'
pe = struct.unpack_from('<I', data, 0x3c)[0]
n, opt = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
sections = [struct.unpack_from('<8sIIII', data, pe+24+opt+40*i) for i in range(n)]
out=['#include <cstring>\nbool CheckSignatures() {']
for address in (0x430530,0x41fe60,0x420130,0x41bc60,0x4300a0,0x4694b0,0x435220,0x44efc0,0x41bb30,0x448430,0x4465c0,0x42a900):
    rva=address-0x400000
    for name,vs,va,rs,raw in sections:
        if va<=rva<va+vs:
            sig=data[raw+rva-va:raw+rva-va+12]
            out.append('  { const unsigned char s[] = {'+','.join(str(b) for b in sig)+'}; if(memcmp((void*)'+hex(address)+',s,sizeof(s))) return false; }')
            break
    else: raise ValueError(hex(address))
out.append('return true; }')
pathlib.Path(__file__).with_name('signatures.cpp').write_text('\n'.join(out))
