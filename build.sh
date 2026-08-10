#!/bin/bash
#
# Сборка ядра Lenovo A1000 (Linux 3.10.64, Spreadtrum SC7731G).
#
#   ./build.sh                    — собрать Image
#   ./build.sh clean              — собрать с нуля
#   CROSS_COMPILE=... ./build.sh  — свой тулчейн
#
# Тулчейн: AOSP arm-eabi-4.8 (GCC 4.8). Более новый GCC ядро 3.10 не собирает.
#
set -e

JOBS=${JOBS:-$(nproc)}
DEFCONFIG=${DEFCONFIG:-a1000_baton4iks_defconfig}

if [ -z "$CROSS_COMPILE" ]; then
    for p in ./toolchain/bin/arm-eabi- \
             ../arm-eabi-4.8-toolchain/bin/arm-eabi- \
             /opt/arm-eabi-4.8/bin/arm-eabi-; do
        [ -x "${p}gcc" ] && CROSS_COMPILE="$p" && break
    done
fi
if [ -z "$CROSS_COMPILE" ] || [ ! -x "${CROSS_COMPILE}gcc" ]; then
    echo "ОШИБКА: не найден тулчейн arm-eabi-4.8."
    echo "Задайте путь явно, например:"
    echo "  CROSS_COMPILE=\$HOME/arm-eabi-4.8/bin/arm-eabi- ./build.sh"
    exit 1
fi

export ARCH=arm
export CROSS_COMPILE
echo "=== тулчейн: $(${CROSS_COMPILE}gcc --version | head -1) ==="

if [ "$1" = "clean" ]; then
    make ARCH=arm clean
fi

# Конфиг берётся только если его ещё нет — чтобы не затирать ручные правки
[ -f .config ] || make ARCH=arm "$DEFCONFIG"

make -j"$JOBS" ARCH=arm Image

echo
echo "=== готово ==="
ls -la arch/arm/boot/Image
strings -a arch/arm/boot/Image | grep -m1 "Linux version" || true
echo
echo "Дальше — упаковать в boot.img (нужен образ, снятый с вашего аппарата):"
echo "  python3 tools/repack_boot.py boot_orig.img arch/arm/boot/Image boot_new.img"
