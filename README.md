# Ядро Lenovo A1000 — Linux 3.10.64, доработанное

Ядро для планшета **Lenovo A1000** (Spreadtrum SC7731G, платформа `sc8830`,
плата `sp7731gea_hd`) под LineageOS 15.1.

База — [unlegacy-devices/android_kernel_lenovo_a1000](https://github.com/unlegacy-devices/android_kernel_lenovo_a1000),
поверх неё лежат правки, сделанные при доведении аппарата до рабочего состояния.

Собранное отсюда ядро проверено живьём: **#75**, аппарат загружается, Wi-Fi,
камера, графика и перезагрузка работают.

## Что изменено относительно базы

| область | файлы | суть |
|---|---|---|
| **binder** | `drivers/staging/android/binder.c`, `uapi/binder.h` | починена передача файловых дескрипторов между процессами — без этого не работала камера |
| **перезагрузка и выключение** | `drivers/platform/sprd/sys_reset.c`, `pm-scx35.c` | включён `CONFIG_SC_INTERNAL_WATCHDOG` (сброс на этом чипе делается только сторожевым таймером), верный расчёт тиков, режим сброса `NORMAL`, аварийный сброс удержанием питания 7 с, диагностика через `/proc/a1000_wdg` |
| **дисплей** | `drivers/video/sprdfb/*` | оверлеи DISPC, vsync, режимы питания панели, порядок каналов R/B |
| **подсветка** | `drivers/video/backlight/sprd_backlight.c` | не включалась после выхода из сна |
| **производительность** | `kernel/futex.c`, `fs/eventpoll.c`, `drivers/gpu/ion/ion.c` | 20 → 29 FPS в связке с настройками DVFS Mali |
| **тачскрин** | `drivers/ontim/touchscreen/mstar/msg22xx/*` | mstar msg22xx |
| **платформа** | `arch/arm/mach-sc/board-sp7731gea_hd.c`, `ontim_device.c`, DTS | плата, устройства ontim |
| **Mali-400** | `drivers/gpu/mali400/` (r4p1) | вендорный драйвер, в базовом репозитории отсутствовал |
| **конфиг** | `arch/arm/configs/a1000_baton4iks_defconfig` | `SC_INTERNAL_WATCHDOG=y`, `UACCESS_WITH_MEMCPY=y`, `PSTORE_RAM=y` (ram-console), `ANDROID_PARANOID_NETWORK=y` |

## Сборка

Нужен **AOSP arm-eabi-4.8** (GCC 4.8). Более новыми компиляторами ядро 3.10
не собирается. Подойдёт пребилт из AOSP
(`prebuilts/gcc/linux-x86/arm/arm-eabi-4.8`) или любая его копия на GitHub.

```sh
git clone https://github.com/Breead1337/lenovoa1000kernel3.10sourceENHANCED.git
cd lenovoa1000kernel3.10sourceENHANCED

# путь к своему тулчейну
export CROSS_COMPILE=$HOME/arm-eabi-4.8/bin/arm-eabi-

./build.sh
```

Готовое ядро — `arch/arm/boot/Image`. Полная сборка с нуля: `./build.sh clean`.

Собрать вручную, без скрипта:

```sh
export ARCH=arm CROSS_COMPILE=$HOME/arm-eabi-4.8/bin/arm-eabi-
make ARCH=arm a1000_baton4iks_defconfig
make -j$(nproc) ARCH=arm Image
```

## Упаковка boot.img

Нужен образ, снятый **со своего аппарата** — из него берутся ramdisk и device
tree:

```sh
adb root
adb shell dd if=/dev/block/platform/sdio_emmc/by-name/boot of=/sdcard/boot_orig.img
adb pull /sdcard/boot_orig.img

python3 tools/repack_boot.py boot_orig.img arch/arm/boot/Image boot_new.img
```

Пересобирать ramdisk через `cpio` **нельзя**: у этого аппарата он состоит из
двух gzip-потоков подряд, и `cpio` теряет второй (там контексты SELinux
`nonplat_*`) — получите чёрный экран. `repack_boot.py` копирует ramdisk байт
в байт и трогает только ядро.

## Прошивка

```sh
adb push boot_new.img /data/local/tmp/
adb shell "sync; dd if=/data/local/tmp/boot_new.img of=/dev/block/mmcblk0p16 bs=1048576; sync"
adb reboot
```

**Сначала сохраните текущий раздел** (`dd if=/dev/block/mmcblk0p16 of=...`) —
это единственный способ откатиться, если ядро не загрузится.

## Модули

`module_layout` = `0xb9384d63`, как в стоке, поэтому стоковые `sprdwl.ko`
(Wi-Fi), `mali.ko` (графика) и `trout_fm.ko` (FM) грузятся без пересборки.

**Не задавайте `CONFIG_LOCALVERSION`** — он меняет `UTS_RELEASE`, а тот входит
в `vermagic` модулей, и все три модуля отвалятся. Подпись в строке версии
делается через `KBUILD_BUILD_TIMESTAMP` — она попадает в `UTS_VERSION`, который
в `vermagic` не входит.

## Лицензия

GPL-2.0, как у ядра Linux.
