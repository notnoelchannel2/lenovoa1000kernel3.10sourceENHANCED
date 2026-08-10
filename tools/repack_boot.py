"""Заменить ядро в готовом Android boot.img.

Правит kernel_size в заголовке и пересчитывает id[] (SHA1), иначе загрузчик
отвергнет образ. ramdisk, second и device tree копируются байт в байт —
это важно: ramdisk у A1000 состоит из ДВУХ gzip-потоков подряд, и любая
пересборка через cpio теряет второй (SELinux nonplat_*) и даёт чёрный экран.

    python3 tools/repack_boot.py <база.img> <Image> <результат.img>

База — образ, снятый с вашего аппарата:
    adb shell dd if=/dev/block/platform/sdio_emmc/by-name/boot of=/sdcard/boot.img
"""
import hashlib, struct, sys

if len(sys.argv) != 4:
    sys.exit(__doc__)

base, kernel, out = sys.argv[1], sys.argv[2], sys.argv[3]
orig = open(base, 'rb').read()
newk = open(kernel, 'rb').read()

ps = struct.unpack('<I', orig[0x24:0x28])[0]
ks, ka, rs, ra, ss, sa, ta, ps2, dts = struct.unpack('<9I', orig[8:44])
padlen = lambda n: (n + ps - 1) // ps * ps
pad = lambda b: b + b'\x00' * (padlen(len(b)) - len(b))

off = ps
k = orig[off:off + ks];  off += padlen(ks)
r = orig[off:off + rs];  off += padlen(rs)
s = orig[off:off + ss];  off += padlen(ss)
dt = orig[off:off + dts]

hdr = bytearray(orig[:ps])
cmdline = hdr[64:64 + 512].split(b'\x00')[0].decode()
struct.pack_into('<I', hdr, 8, len(newk))

sha = hashlib.sha1()
for blob, n in ((newk, len(newk)), (r, rs), (s, ss)):
    sha.update(blob); sha.update(struct.pack('<I', n))
if dts:
    sha.update(dt); sha.update(struct.pack('<I', dts))
struct.pack_into('20s', hdr, 0x240, sha.digest())

open(out, 'wb').write(bytes(hdr) + pad(newk) + pad(r) + pad(s) + pad(dt))
print("kernel_size = %d -> %d" % (ks, len(newk)))
print("ramdisk = %d, second = %d, dt = %d" % (rs, ss, dts))
print("cmdline = %s" % cmdline)
