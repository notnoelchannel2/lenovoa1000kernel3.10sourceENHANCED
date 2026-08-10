# Ядро Lenovo A1000 — Linux 3.10.64, доработанное для Android 8.1 
Для сборки только gcc 4.8
## Что изменено относительно базы 

| область | файлы | суть |
|---|---|---|
| **binder** | `drivers/staging/android/binder.c`, `uapi/binder.h` | Бэкпортирован, починена передача файловых дескрипторов между процессами — без этого не работала камера |
| **перезагрузка и выключение** | `drivers/platform/sprd/sys_reset.c`, `pm-scx35.c` | включён `CONFIG_SC_INTERNAL_WATCHDOG` (сброс на этом чипе делается только сторожевым таймером), верный расчёт тиков, режим сброса `NORMAL`, аварийный сброс удержанием питания 7 с, диагностика через `/proc/a1000_wdg` |
| **дисплей** | `drivers/video/sprdfb/*` | оверлеи DISPC, vsync, режимы питания панели, порядок каналов R/B |
| **подсветка** | `drivers/video/backlight/sprd_backlight.c` | не включалась после выхода из сна |
| **производительность** | `kernel/futex.c`, `fs/eventpoll.c`, `drivers/gpu/ion/ion.c` | 20 → 29 FPS в связке с настройками DVFS Mali |
| **тачскрин** | `drivers/ontim/touchscreen/mstar/msg22xx/*` | mstar msg22xx |
| **платформа** | `arch/arm/mach-sc/board-sp7731gea_hd.c`, `ontim_device.c`, DTS | плата `sp7731gea_hd`, устройства ontim |
| **Mali-400** | `drivers/gpu/mali400/` (r4p1) | вендорный драйвер, в базовом репозитории отсутствовал |
| **сборка** | `scripts/dtc/dtc-lexer.l` | собирается современным GCC (у GCC ≥ 10 по умолчанию `-fno-common`, и `yylloc` ломал линковку `dtc`) |
| **конфиг** | `arch/arm/configs/a1000_baton4iks_defconfig` | `SC_INTERNAL_WATCHDOG=y`, `UACCESS_WITH_MEMCPY=y`, `PSTORE_RAM=y`, `ANDROID_PARANOID_NETWORK=y` |
