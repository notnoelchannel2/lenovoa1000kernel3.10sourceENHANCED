#!/bin/bash
cd /a1000k || exit 9
export ARCH=arm
export CROSS_COMPILE=/toolchain48/bin/arm-eabi-
export PATH=/toolchain48/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# подпись уходит в UTS_VERSION, то есть в САМЫЙ конец строки версии
export KBUILD_BUILD_TIMESTAMP="$(date) by baton4iks"
echo "=== метка сборки: $KBUILD_BUILD_TIMESTAMP ==="
make -j4 ARCH=arm CROSS_COMPILE=/toolchain48/bin/arm-eabi- Image 2>&1 | tail -12
ls -la arch/arm/boot/Image
echo "=== module_layout (должен остаться 0xb9384d63) ==="
grep -w module_layout Module.symvers
echo "=== строка версии ==="
strings -a arch/arm/boot/Image | grep -m1 "Linux version"
echo "=== uaccess_with_memcpy собран? ==="
ls -la arch/arm/lib/uaccess_with_memcpy.o 2>&1 | head -1
