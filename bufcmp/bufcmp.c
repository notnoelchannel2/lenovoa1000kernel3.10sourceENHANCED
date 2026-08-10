/*
 * bufcmp.ko — читает ОБА буфера композитора по физическим адресам и сравнивает.
 *
 * Композитор ping-pong'ит 0x9ffcc000 / 0xa0143000 (480*800*4 = 0x177000).
 * Задача: понять, битый ли один из них и чем именно — пустой, частично
 * заполненный или с мусором. Пишет сырьё в /data/bufA.raw|bufB.raw и
 * статистику в kmsg.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

extern unsigned long g_dispc_base_addr;

#define W 480
#define H 800
#define FRAME (W * H * 4)

static unsigned long pa = 0x9ffcc000;
static unsigned long pb = 0xa0143000;
static int tag = 0;
module_param(pa, ulong, 0);
module_param(pb, ulong, 0);
module_param(tag, int, 0);

static void write_file(const char *path, const void *buf, unsigned len)
{
	struct file *f;
	loff_t po = 0;
	mm_segment_t old;

	f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) {
		printk(KERN_ERR "bufcmp: open %s failed\n", path);
		return;
	}
	old = get_fs();
	set_fs(KERNEL_DS);
	vfs_write(f, (const char __user *)buf, len, &po);
	set_fs(old);
	filp_close(f, NULL);
}

/* Дублируем сводку в файл: захват kmsg может оборваться раньше, чем мы снимем. */
static void append_txt(const char *line)
{
	struct file *f;
	loff_t po = 0;
	mm_segment_t old;

	f = filp_open("/data/bufcmp.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (IS_ERR(f))
		return;
	old = get_fs();
	set_fs(KERNEL_DS);
	vfs_write(f, (const char __user *)line, strlen(line), &po);
	set_fs(old);
	filp_close(f, NULL);
}

/* Статистика: сколько строк полностью нулевые, сколько уникальных значений
 * в разреженной выборке, первый пиксель. Этого хватает, чтобы отличить
 * "пустой" от "однотонный" от "нормальный кадр". */
static void stats(const char *name, const u32 *p, const char *path)
{
	unsigned y, x, blank_rows = 0, uniq = 0;
	u32 seen[32];
	u32 first = p[0];
	char map[H / 16 + 1];

	for (y = 0; y < H; y++) {
		unsigned nz = 0;
		for (x = 0; x < W; x += 8)
			if (p[y * W + x]) { nz = 1; break; }
		if (!nz) blank_rows++;
	}
	for (y = 0; y < H; y += 16) {
		unsigned nz = 0;
		for (x = 0; x < W; x += 8)
			if (p[y * W + x]) { nz = 1; break; }
		map[y / 16] = nz ? '#' : '.';
	}
	map[H / 16] = 0;

	for (y = 0; y < FRAME / 4; y += 4099) {
		unsigned i;
		u32 v = p[y];
		for (i = 0; i < uniq; i++)
			if (seen[i] == v) break;
		if (i == uniq && uniq < 32)
			seen[uniq++] = v;
	}

	printk(KERN_ERR "bufcmp[%d] %s: first=%08x blank_rows=%u/%u uniq(sample)=%u\n",
	       tag, name, first, blank_rows, H, uniq);
	printk(KERN_ERR "bufcmp[%d] %s rowmap: %s\n", tag, name, map);

	{
		char line[192];
		snprintf(line, sizeof(line),
			 "[%d] %s first=%08x blank_rows=%u/%u uniq=%u\n[%d] %s rowmap %s\n",
			 tag, name, first, blank_rows, H, uniq, tag, name, map);
		append_txt(line);
	}

	if (path)
		write_file(path, p, FRAME);
}

static int __init bufcmp_init(void)
{
	void __iomem *va, *vb;
	u32 live = 0;
	char path[64];

	if (g_dispc_base_addr)
		live = __raw_readl((void __iomem *)(g_dispc_base_addr + 0x0020)); /* DISPC_OSD_BASE_ADDR */

	printk(KERN_ERR "bufcmp[%d] ==== live OSD_BASE=%08x  A=%08lx B=%08lx ====\n",
	       tag, live, pa, pb);
	{
		char line[128];
		snprintf(line, sizeof(line), "==== tag=%d live OSD_BASE=%08x A=%08lx B=%08lx\n",
			 tag, live, pa, pb);
		append_txt(line);
	}

	va = ioremap(pa, FRAME);
	vb = ioremap(pb, FRAME);
	if (!va || !vb) {
		printk(KERN_ERR "bufcmp: ioremap failed (%p %p)\n", va, vb);
		goto out;
	}

	snprintf(path, sizeof(path), "/data/bufA_%d.raw", tag);
	stats("A", (const u32 *)va, path);
	snprintf(path, sizeof(path), "/data/bufB_%d.raw", tag);
	stats("B", (const u32 *)vb, path);

out:
	if (va) iounmap(va);
	if (vb) iounmap(vb);
	return -EINVAL;   /* не оставаться загруженным — можно insmod'ить повторно */
}

static void __exit bufcmp_exit(void) { }

module_init(bufcmp_init);
module_exit(bufcmp_exit);
MODULE_LICENSE("GPL");
