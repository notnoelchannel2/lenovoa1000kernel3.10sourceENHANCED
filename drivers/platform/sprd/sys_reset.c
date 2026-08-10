/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <soc/sprd/sys_reset.h>
#include <linux/string.h>
#include <asm/bitops.h>

void sprd_set_reboot_mode(const char *cmd)
{
	if(cmd)
		printk("sprd_set_reboot_mode:cmd=%s\n",cmd);
	if (cmd && !(strncmp(cmd, "recovery", 8))) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_RECOVERY);
	} else if (cmd && !strncmp(cmd, "alarm", 5)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_ALARM);
	} else if (cmd && !strncmp(cmd, "fastsleep", 9)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_SLEEP);
	} else if (cmd && !strncmp(cmd, "bootloader", 10)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_FASTBOOT);
	} else if (cmd && !strncmp(cmd, "panic", 5)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_PANIC);
	} else if (cmd && !strncmp(cmd, "special", 7)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_SPECIAL);
	} else if (cmd && !strncmp(cmd, "cftreboot", 9)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_CFTREBOOT);
	} else if (cmd && !strncmp(cmd, "autodloader", 11)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_AUTODLOADER);
	} else if (cmd && !strncmp(cmd, "iqmode", 6)) {
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_IQMODE);
	} else if(cmd){
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_NORMAL);
	}else{
		/* A1000: было HWRST_STATUS_SPECIAL. cmd == NULL — это обычная
		 * перезагрузка (machine_restart(NULL) из emergency_restart и из
		 * LINUX_REBOOT_CMD_RESTART), а 0x70 загрузчик разбирает как режим
		 * 10 и грузит систему с androidboot.mode=special. Пишем NORMAL. */
		sci_adi_raw_write(ANA_RST_STATUS, HWRST_STATUS_NORMAL);
	}
}

void sprd_turnon_watchdog(unsigned int ms)
{
	uint32_t cnt;

	/* A1000: множитель и делитель были перепутаны.
	 * Сторожевой таймер тикает на WDG_CLK = 32768 Гц, поэтому число тиков для
	 * задержки в ms миллисекунд — это ms * WDG_CLK / 1000. В исходном виде
	 * получалось (ms * 1000) / 32768, то есть для 50 мс — ОДИН тик (~30 мкс)
	 * вместо 1638. Таймер заряжался почти нулём, и сброс не происходил:
	 * перезагрузка и выключение зависали навсегда. */
	/* Считаем в 32 битах: 64-битное деление в ядре ARM требует do_div,
	 * иначе линковка падает на __aeabi_uldivmod. Разделение на целые
	 * секунды и остаток заодно исключает переполнение. */
	cnt = (ms / 1000) * WDG_CLK + ((ms % 1000) * WDG_CLK) / 1000;
	if (cnt == 0)
		cnt = 1;

	printk("A1000: watchdog cnt=%u для %u мс\n", (unsigned int)cnt, (unsigned int)ms);

	/*enable interface clk*/
	sci_adi_set(ANA_AGEN, (unsigned int)AGEN_WDG_EN);
	/*enable work clk*/
	sci_adi_set(ANA_RTC_CLK_EN, (unsigned int)AGEN_RTC_WDG_EN);
	sci_adi_raw_write(WDG_LOCK, WDG_UNLOCK_KEY);
	sci_adi_set(WDG_CTRL, WDG_NEW_VER_EN);
	WDG_LOAD_TIMER_VALUE(cnt);
	sci_adi_set(WDG_CTRL, WDG_CNT_EN_BIT | WDG_RST_EN_BIT);
	sci_adi_raw_write(WDG_LOCK, (uint16_t) (~WDG_UNLOCK_KEY));

	printk("A1000 wdg: после настройки CTRL=0x%04x AGEN=0x%04x RTC_EN=0x%04x "
	       "LOAD_H=0x%04x LOAD_L=0x%04x CNT_H=0x%04x CNT_L=0x%04x POR7S=0x%04x\n",
	       (unsigned int)sci_adi_read(WDG_CTRL), (unsigned int)sci_adi_read(ANA_AGEN),
	       (unsigned int)sci_adi_read(ANA_RTC_CLK_EN),
	       (unsigned int)sci_adi_read(WDG_LOAD_HIGH), (unsigned int)sci_adi_read(WDG_LOAD_LOW),
	       (unsigned int)sci_adi_read(WDG_CNT_HIGH), (unsigned int)sci_adi_read(WDG_CNT_LOW),
	       (unsigned int)sci_adi_read(ANA_REG_GLB_POR_7S_CTRL));
}

void sprd_turnoff_watchdog(void)
{
	sci_adi_raw_write(WDG_LOCK, WDG_UNLOCK_KEY);
	/*wdg counter stop*/
	sci_adi_clr(WDG_CTRL, WDG_CNT_EN_BIT);
	/*disable the reset mode*/
	sci_adi_clr(WDG_CTRL, WDG_RST_EN_BIT);
	sci_adi_raw_write(WDG_LOCK, (uint16_t) (~WDG_UNLOCK_KEY));
	/*stop the interface and work clk*/
	sci_adi_clr(ANA_AGEN, (unsigned int)AGEN_WDG_EN);
	sci_adi_clr(ANA_RTC_CLK_EN, (unsigned int)AGEN_RTC_WDG_EN);
}


/* A1000: диагностика сторожевого таймера через /proc/a1000_wdg.
 * Чтение печатает состояние регистров, запись числа заряжает таймер на
 * указанное число миллисекунд. Нужно, чтобы проверять сброс, не подвешивая
 * систему: даём таймеру несколько секунд, читаем логи и смотрим, сбросится ли
 * машина сама. */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static int a1000_wdg_show(struct seq_file *m, void *v)
{
	seq_printf(m, "CTRL   = 0x%04x\n", (unsigned int)sci_adi_read(WDG_CTRL));
	seq_printf(m, "AGEN   = 0x%04x (бит WDG_EN = 0x%04x)\n",
		   (unsigned int)sci_adi_read(ANA_AGEN), (unsigned int)AGEN_WDG_EN);
	seq_printf(m, "RTC_EN = 0x%04x (бит RTC_WDG_EN = 0x%04x)\n",
		   (unsigned int)sci_adi_read(ANA_RTC_CLK_EN), (unsigned int)AGEN_RTC_WDG_EN);
	seq_printf(m, "LOAD   = 0x%04x%04x\n",
		   (unsigned int)sci_adi_read(WDG_LOAD_HIGH), (unsigned int)sci_adi_read(WDG_LOAD_LOW));
	seq_printf(m, "CNT    = 0x%04x%04x\n",
		   (unsigned int)sci_adi_read(WDG_CNT_HIGH), (unsigned int)sci_adi_read(WDG_CNT_LOW));
	seq_printf(m, "INT_RAW= 0x%04x\n", (unsigned int)sci_adi_read(WDG_INT_RAW));
	seq_printf(m, "POR7S  = 0x%04x (бит запрета сброса = 0x%04x)\n",
		   (unsigned int)sci_adi_read(ANA_REG_GLB_POR_7S_CTRL), (unsigned int)BIT_PBINT_7S_RST_DISABLE);
	seq_printf(m, "RST_ST = 0x%04x\n", (unsigned int)sci_adi_read(ANA_RST_STATUS));
	return 0;
}

static int a1000_wdg_open(struct inode *inode, struct file *file)
{
	return single_open(file, a1000_wdg_show, NULL);
}

static ssize_t a1000_wdg_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	char tmp[16];
	unsigned long ms;

	if (count == 0 || count >= sizeof(tmp))
		return -EINVAL;
	if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	tmp[count] = '\0';
	if (kstrtoul(strim(tmp), 0, &ms))
		return -EINVAL;

	printk("A1000 wdg: заряжаю таймер на %lu мс\n", ms);
	/* Без записи режима загрузчик видит rst_mode=0 и разбирает его не как
	 * перезагрузку. Пишем то же, что и штатный путь, чтобы тест через
	 * /proc воспроизводил настоящий сброс. */
	sprd_set_reboot_mode(NULL);
	sprd_turnon_watchdog((unsigned int)ms);
	return count;
}

static const struct file_operations a1000_wdg_fops = {
	.owner		= THIS_MODULE,
	.open		= a1000_wdg_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= a1000_wdg_write,
};

static int __init a1000_wdg_proc_init(void)
{
	proc_create("a1000_wdg", 0644, NULL, &a1000_wdg_fops);
	return 0;
}
late_initcall(a1000_wdg_proc_init);
