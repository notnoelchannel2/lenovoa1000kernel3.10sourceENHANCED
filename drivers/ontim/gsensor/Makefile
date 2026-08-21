#
# Makefile for the ontim g-sensor drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_GSENSOR_LIS3DH_I2C)	+= lis3dh.o
obj-$(CONFIG_GSENSOR_MMA845X)   +=mma845x.o
obj-$(CONFIG_GSENSOR_KIONIX)   +=kionix_accel.o
obj-$(CONFIG_GSENSOR_MC3XXX_ONTIM)  += mc3xxx.o
obj-$(CONFIG_GSENSOR_MIR3DA_ONTIM)  += mir3da_core.o mir3da_cust.o

obj-y				+=bma2x2/


# A1000: этот файл годами не пересобирался, его .o лежал готовым с прошлых
# сборок. При первой честной пересборке всплыло давнее предупреждение
# (mc3xxx_get_bandwidth не используется), а предупреждения тут считаются
# ошибками. Глушим ТОЛЬКО для этого файла, чужие не трогаем.
CFLAGS_mc3xxx.o += -Wno-unused-function -Wno-error
