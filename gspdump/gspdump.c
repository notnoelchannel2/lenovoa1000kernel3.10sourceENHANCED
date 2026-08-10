/*
 * gspdump.ko — dump kernel cmdline + DISPC scanout state + live OSD buffer.
 * insmod WHILE garbage is on screen. Multi-snapshot: 8 dumps @700ms in one insmod.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

extern unsigned long g_dispc_base_addr;

static void write_file(const char *path, const char *buf, int len)
{
	struct file *f; loff_t pos = 0; mm_segment_t old;
	f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) { printk(KERN_ERR "gspdump: open %s failed %ld\n", path, PTR_ERR(f)); return; }
	old = get_fs(); set_fs(KERNEL_DS);
	vfs_write(f, (const char __user *)buf, len, &pos);
	set_fs(old);
	filp_close(f, NULL);
}

static void dump_phys(unsigned long phys, unsigned len, const char *path)
{
	void __iomem *v; char *tmp;
	if (!phys || !len || len > 4*1024*1024) return;
	v = ioremap(phys, len);
	if (!v) { printk(KERN_ERR "gspdump: ioremap 0x%lx failed\n", phys); return; }
	tmp = vmalloc(len);
	if (tmp) { memcpy_fromio(tmp, v, len); write_file(path, tmp, len); vfree(tmp); }
	iounmap(v);
}

static const struct { int off; const char *name; } regs[] = {
	{0x000,"DISPC_CTRL"}, {0x004,"DISPC_SIZE_XY"}, {0x03c,"BG_COLOR"},
	{0x020,"IMG_CTRL"}, {0x024,"IMG_Y_BASE"}, {0x030,"IMG_SIZE_XY"}, {0x034,"IMG_PITCH"},
	{0x040,"OSD_CTRL"}, {0x044,"OSD_BASE"}, {0x048,"OSD_SIZE_XY"}, {0x04c,"OSD_PITCH"},
	{0x050,"OSD_DISP_XY"}, {0x054,"OSD_ALPHA"},
	{0x080,"DPI_CTRL"}, {0x084,"DPI_H_TIMING"}, {0x088,"DPI_V_TIMING"},
	{0x08c,"DPI_STS0"}, {0x090,"DPI_STS1"},
	{0x100,"SHDW_OSD_CTRL"}, {0x104,"SHDW_OSD_BASE"}, {0x108,"SHDW_OSD_SIZE_XY"},
	{0x10c,"SHDW_OSD_PITCH"}, {0x110,"SHDW_OSD_DISP_XY"},
};

static int __init gspdump_init(void)
{
	char *buf; int n, i, g_snap;
	unsigned long b = g_dispc_base_addr;
	uint32_t osd_base, osd_size, osd_pitch;

	printk(KERN_ERR "gspdump: base=0x%lx multi-snap start\n", b);
	buf = vmalloc(16384);
	if (!buf) return 0;

	for (g_snap = 0; g_snap < 8; g_snap++) {
		char pth[64];
		n = 0; osd_base = 0; osd_size = 0; osd_pitch = 0;
		n += scnprintf(buf + n, 16384 - n, "dispc_base=0x%lx snap=%d\n", b, g_snap);
		if (b) {
			for (i = 0; i < (int)(sizeof(regs)/sizeof(regs[0])); i++)
				n += scnprintf(buf + n, 16384 - n, "  +0x%03x %-16s = 0x%08x\n",
					regs[i].off, regs[i].name, readl((void __iomem *)(b + regs[i].off)));
			osd_base  = readl((void __iomem *)(b + 0x104));
			osd_size  = readl((void __iomem *)(b + 0x108));
			osd_pitch = readl((void __iomem *)(b + 0x10c));
		}
		snprintf(pth, 64, "/data/dispc_regs_%d.txt", g_snap);
		write_file(pth, buf, n);
		if (osd_base) {
			unsigned w = osd_pitch & 0xffff;
			unsigned h = (osd_size >> 16) & 0xffff;
			if (!w) w = osd_size & 0xffff;
			if (w && h && w <= 1024 && h <= 1280) {
				snprintf(pth, 64, "/data/gsp_live_%d.raw", g_snap);
				dump_phys(osd_base, w * h * 4, pth);
			}
		}
		msleep(700);
	}
	vfree(buf);
	printk(KERN_ERR "gspdump: done multi\n");
	return 0;
}

static void __exit gspdump_exit(void) {}
module_init(gspdump_init);
module_exit(gspdump_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("dump DISPC state + live OSD buffer, multi-snapshot");
