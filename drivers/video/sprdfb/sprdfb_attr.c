/*
 * Copyright (C) 2014 Spreadtrum Communications Inc.
 *
 * Author: Haibing.Yang <haibing.yang@spreadtrum.com>
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

#define pr_fmt(fmt)		"sprdfb: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include "sprdfb.h"
#include "sprdfb_panel.h"
#include "sprdfb_dispc_reg.h"
#include <linux/proc_fs.h>
#include <linux/uaccess.h>


struct attr_info {
	struct sprdfb_device *fb_dev;
	/* clock management */
	u32 origin_pclk;
	u32 curr_pclk;
	u32 origin_fps;
	u32 curr_fps;
	u32 origin_mipi_clk;
	u32 curr_mipi_clk;

	struct semaphore sem;
};

static ssize_t sysfs_rd_current_pclk(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t sysfs_write_pclk(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
static ssize_t sysfs_rd_current_fps(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t sysfs_write_fps(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
static ssize_t sysfs_rd_current_mipi_clk(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t sysfs_write_mipi_clk(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
static ssize_t sysfs_rd_current_frame_count(struct device *dev,
		struct device_attribute *attr, char *buf);

#ifdef CONFIG_FB_ESD_SUPPORT
static ssize_t sysfs_rd_current_esd(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t sysfs_write_esd(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
#endif

static DEVICE_ATTR(dynamic_pclk, S_IRUGO | S_IWUSR, sysfs_rd_current_pclk,
		sysfs_write_pclk);
static DEVICE_ATTR(dynamic_fps, S_IRUGO | S_IWUSR, sysfs_rd_current_fps,
		sysfs_write_fps);
static DEVICE_ATTR(dynamic_mipi_clk, S_IRUGO | S_IWUSR,
		sysfs_rd_current_mipi_clk, sysfs_write_mipi_clk);
static DEVICE_ATTR(dynamic_frame_count, S_IRUGO,
		sysfs_rd_current_frame_count, NULL);

#ifdef CONFIG_FB_ESD_SUPPORT
static DEVICE_ATTR(dynamic_esd, S_IRUGO | S_IWUSR,
		sysfs_rd_current_esd, sysfs_write_esd);
#endif

/*
 * One-shot probe for the "picture repeats / tears" artefact. Everything below is scanout
 * state, i.e. beneath SurfaceFlinger - screencap cannot see any of it. Reading this node
 * also re-arms the DISPC error interrupt, which dispc_isr disarms after the first underflow,
 * so consecutive reads answer "was there an underflow since the previous read".
 */
extern u32 sprdfb_underflow_cnt;

static const struct { const char *name; u32 off; } dispc_probe_regs[] = {
	{"CTRL",           DISPC_CTRL},
	{"SIZE_XY",        DISPC_SIZE_XY},
	{"BUF_THRES",      DISPC_BUF_THRES},
	{"STS",            DISPC_STS},
	{"BG_COLOR",       DISPC_BG_COLOR},
	{"OSD_CTRL",       DISPC_OSD_CTRL},
	{"OSD_BASE_ADDR",  DISPC_OSD_BASE_ADDR},
	{"OSD_SIZE_XY",    DISPC_OSD_SIZE_XY},
	{"OSD_PITCH",      DISPC_OSD_PITCH},
	{"OSD_DISP_XY",    DISPC_OSD_DISP_XY},
	{"OSD_ALPHA",      DISPC_OSD_ALPHA},
	{"INT_EN",         DISPC_INT_EN},
	{"INT_RAW",        DISPC_INT_RAW},
	{"DPI_CTRL",       DISPC_DPI_CTRL},
	{"DPI_H_TIMING",   DISPC_DPI_H_TIMING},
	{"DPI_V_TIMING",   DISPC_DPI_V_TIMING},
	{"DPI_STS0",       DISPC_DPI_STS0},
	{"DPI_STS1",       DISPC_DPI_STS1},
	{"SHDW_OSD_CTRL",  SHDW_OSD_CTRL},
	{"SHDW_OSD_BASE",  SHDW_OSD_BASE_ADDR},
	{"SHDW_OSD_SIZE",  SHDW_OSD_SIZE_XY},
	{"SHDW_OSD_PITCH", SHDW_OSD_PITCH},
	{"SHDW_DPI_H",     SHDW_DPI_H_TIMING},
	{"SHDW_DPI_V",     SHDW_DPI_V_TIMING},
};

static ssize_t sysfs_rd_dispc_dump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fb = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = fb ? fb->par : NULL;
	int i, n = 0;

	n += snprintf(buf + n, PAGE_SIZE - n, "panel: %s\n",
		(fb_dev && fb_dev->lcd_name) ? fb_dev->lcd_name : "(unset)");
	n += snprintf(buf + n, PAGE_SIZE - n, "underflow_cnt: %u\n", sprdfb_underflow_cnt);
	for (i = 0; i < ARRAY_SIZE(dispc_probe_regs); i++)
		n += snprintf(buf + n, PAGE_SIZE - n, "%-14s %08x\n",
			dispc_probe_regs[i].name, dispc_read(dispc_probe_regs[i].off));

	dispc_set_bits(DISPC_INT_ERR_MASK, DISPC_INT_EN);
	return n;
}

static DEVICE_ATTR(dispc_dump, S_IRUGO, sysfs_rd_dispc_dump, NULL);

static ssize_t sysfs_rd_osd_thumb(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 base  = dispc_read(SHDW_OSD_BASE_ADDR);
	u32 pitch = dispc_read(SHDW_OSD_PITCH) & 0xffff;
	u32 sz    = dispc_read(SHDW_OSD_SIZE_XY);
	u32 w = sz & 0xffff, h = (sz >> 16) & 0xffff;
	void __iomem *p;
	int n = 0, r, c;

	if (!base || !w || !h || !pitch)
		return snprintf(buf, PAGE_SIZE, "no scanout: base=%08x sz=%08x pitch=%u\n",
				base, sz, pitch);

	/* uncached on purpose: this must show what DISPC fetches, not what a CPU cache holds */
	p = ioremap_nocache(base, pitch * h * 4);
	if (!p)
		return snprintf(buf, PAGE_SIZE, "ioremap %08x failed\n", base);

	n += snprintf(buf + n, PAGE_SIZE - n, "base=%08x w=%u h=%u pitch=%u\n",
		      base, w, h, pitch);
	for (r = 0; r < 20; r++) {
		for (c = 0; c < 16; c++) {
			u32 px = readl(p + (((h * r) / 20) * pitch + (w * c) / 16) * 4);
			n += snprintf(buf + n, PAGE_SIZE - n, "%06x ", px & 0xffffff);
		}
		n += snprintf(buf + n, PAGE_SIZE - n, "\n");
	}
	iounmap(p);
	return n;
}

static DEVICE_ATTR(osd_thumb, S_IRUGO, sysfs_rd_osd_thumb, NULL);

/* ---- /proc/dispc_osd : raw dump of whatever DISPC is scanning out right now ---- */
struct osd_dump_ctx { void __iomem *p; size_t total; };

static int dispc_osd_open(struct inode *inode, struct file *file)
{
	struct osd_dump_ctx *c;
	u32 base  = dispc_read(SHDW_OSD_BASE_ADDR);
	u32 pitch = dispc_read(SHDW_OSD_PITCH) & 0xffff;
	u32 h     = (dispc_read(SHDW_OSD_SIZE_XY) >> 16) & 0xffff;

	if (!base || !pitch || !h)
		return -ENODEV;
	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	{
		size_t frame = (size_t)pitch * h * 4;

		/* start one frame below so the previous buffer is included; the caller can tell
		 * the three frames apart because the dump is a whole number of frames */
		if (base >= frame)
			base -= frame;
		c->total = frame * 3;
	}
	c->p = ioremap_nocache(base, c->total);
	if (!c->p) {
		kfree(c);
		return -EIO;
	}
	file->private_data = c;
	return 0;
}

static int dispc_osd_release(struct inode *inode, struct file *file)
{
	struct osd_dump_ctx *c = file->private_data;

	if (c) {
		if (c->p)
			iounmap(c->p);
		kfree(c);
	}
	return 0;
}

static ssize_t dispc_osd_read(struct file *file, char __user *ubuf,
			      size_t len, loff_t *ppos)
{
	struct osd_dump_ctx *c = file->private_data;
	size_t n;

	if (!c || *ppos >= (loff_t)c->total)
		return 0;
	n = min(len, c->total - (size_t)*ppos);
	if (copy_to_user(ubuf, (const void *)c->p + *ppos, n))
		return -EFAULT;
	*ppos += n;
	return n;
}

static const struct file_operations dispc_osd_fops = {
	.owner   = THIS_MODULE,
	.open    = dispc_osd_open,
	.read    = dispc_osd_read,
	.release = dispc_osd_release,
	.llseek  = default_llseek,
};

static struct attribute *sprdfb_fs_attrs[] = {
	&dev_attr_osd_thumb.attr,
	&dev_attr_dispc_dump.attr,
	&dev_attr_dynamic_pclk.attr,
	&dev_attr_dynamic_fps.attr,
	&dev_attr_dynamic_mipi_clk.attr,
	&dev_attr_dynamic_frame_count.attr,

#ifdef CONFIG_FB_ESD_SUPPORT
	&dev_attr_dynamic_esd.attr,
#endif
	NULL,
};

static struct attribute_group sprdfb_attrs_group = {
	.attrs = sprdfb_fs_attrs,
};

/**
 * @fb_dev - sprdfb specific device
 * @type: SPRDFB_DYNAMIC_PCLK, SPRDFB_DYNAMIC_FPS, SPRDFB_DYNAMIC_MIPI_CLK
 * @new_val: new required fps, dpi clock or mipi clock
 *
 * If clk is set unsuccessfully, this function returns minus.
 * It returns 0 if clk is set successfully.
 */
int sprdfb_chg_clk_intf(struct sprdfb_device *fb_dev,
			int type, u32 new_val)
{
	int ret;
	struct attr_info *attr = fb_dev->priv1;

	down(&attr->sem);
	ret = sprdfb_dispc_chg_clk(fb_dev, type, new_val);
	up(&attr->sem);

	return ret;
}

static ssize_t sysfs_rd_current_pclk(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;

	if (!fb_dev) {
		pr_err("fb_dev can't be found\n");
		return -ENXIO;
	}
	ret = snprintf(buf, PAGE_SIZE,
			"current dpi_clk: %u\nnew dpi_clk: %u\norigin dpi_clk: %u\n",
			fb_dev->dpi_clock, attr_info->curr_pclk,
			attr_info->origin_pclk);

	return ret;
}

static ssize_t sysfs_write_pclk(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	int divider;
	u32 dpi_clk_src, new_pclk;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;

	sscanf(buf, "%u,%d\n", &dpi_clk_src, &divider);

	if (divider == 0 && dpi_clk_src == 0) {
		new_pclk = attr_info->origin_pclk;
		goto DIRECT_GO;
	}

	if (divider < 1 || divider > 0xff || dpi_clk_src < 1) {
		pr_err("divider:[%d], clk_src:[%d] is invalid\n",
				divider, dpi_clk_src);
		return count;
	}

	if (dpi_clk_src < 1000)
		dpi_clk_src *= 1000000; /* MHz */

	new_pclk = dpi_clk_src / divider;

DIRECT_GO:
	if (new_pclk == fb_dev->dpi_clock) {
		/* Do nothing */
		pr_warn("new pclk is the same as current pclk\n");
		return count;
	}

	ret = sprdfb_chg_clk_intf(fb_dev, SPRDFB_DYNAMIC_PCLK, new_pclk);
	if (ret) {
		pr_err("%s: failed to change dpi clock. ret=%d\n",
				__func__, ret);
		return ret;
	}
	attr_info->curr_pclk = new_pclk;

	return count;
}

static ssize_t sysfs_rd_current_fps(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;
	struct panel_spec* panel = fb_dev->panel;

	if (!panel) {
		pr_err("panel doesn't exist.");
		return -ENXIO;
	}
	ret = snprintf(buf, PAGE_SIZE,
			"current fps: %u\nnew fps: %u\norigin fps: %u\n",
			panel->fps, attr_info->curr_fps, attr_info->origin_fps);

	return ret;
}

static ssize_t sysfs_write_fps(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret, fps;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;
	struct panel_spec* panel = fb_dev->panel;

	if (!panel) {
		pr_err("panel doesn't exist.");
		return -ENXIO;
	}
	ret = kstrtoint(buf, 10, &fps);

	fps = fps > 0 ? fps : attr_info->origin_fps;

	if (panel->fps == fps) {
		/* Do nothing */
		pr_warn("new fps is the same as current fps\n");
		return count;
	}

	ret = sprdfb_chg_clk_intf(fb_dev, SPRDFB_DYNAMIC_FPS, (u32)fps);
	if (ret) {
		pr_err("%s: failed to change dpi clock. ret=%d\n",
				__func__, ret);
		return ret;
	}
	attr_info->curr_fps = fps;

	return count;
}

static ssize_t sysfs_rd_current_mipi_clk(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;
	struct panel_spec* panel = fb_dev->panel;

	if (!panel) {
		pr_err("panel doesn't exist.");
		return -ENXIO;
	}
	if (panel->type != LCD_MODE_DSI) {
		pr_err("Current panel is not mipi dsi\n");
		ret = snprintf(buf, PAGE_SIZE,
				"Current panel is not mipi dsi\n");
		return ret;
	}

	ret = snprintf(buf, PAGE_SIZE,
			"current mipi d-phy frequency: %u\n"
			"new mipi d-phy frequency: %u\n"
			"origin mipi d-phy frequency: %u\n",
			panel->info.mipi->phy_feq,
			attr_info->curr_mipi_clk,
			attr_info->origin_mipi_clk);
	return ret;
}

static ssize_t sysfs_write_mipi_clk(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret = 0;
	u32 dphy_freq;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	struct attr_info *attr_info = fb_dev->priv1;
	struct panel_spec* panel = fb_dev->panel;

	if (!panel) {
		pr_err("panel doesn't exist.");
		return -ENXIO;
	}
	if (panel->type != LCD_MODE_DSI) {
		pr_err("sys write failure. Current panel is not mipi dsi\n");
		return count;
	}
	ret = kstrtoint(buf, 10, &dphy_freq);
	if (ret) {
		pr_err("Invalid input for dphy_freq\n");
		return -EINVAL;
	}

	if (dphy_freq > 0) {
		/*
		 * because of double edge trigger,
		 * the rule is actual freq * 10 / 2,
		 * Eg: Required freq is 500M
		 * Equation: 2500*2*1000/10=500*1000=2500*200=500M
		 */
		dphy_freq *= 200;
	} else
		dphy_freq = attr_info->origin_mipi_clk;

	/* dphy supported freq ranges is 90M-1500M*/
	pr_debug("input dphy_freq is %d\n", dphy_freq);

	if (dphy_freq == attr_info->curr_mipi_clk) {
		/* Do nothing */
		pr_warn("new dphy_freq is the same as current freq\n");
		return count;
	}

	if (dphy_freq <= 1500000 && dphy_freq >= 90000) {
		ret = sprdfb_chg_clk_intf(fb_dev, SPRDFB_DYNAMIC_MIPI_CLK, dphy_freq);
		if (ret) {
			pr_err("sprdfb_chg_clk_intf change d-phy freq fail.\n");
			return count;
		}
		attr_info->curr_mipi_clk = dphy_freq;
	} else {
		pr_warn("input mipi frequency:%d is out of range.\n",
				dphy_freq);
	}

	return count;
}

static ssize_t sysfs_rd_current_frame_count(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;

	if (!fb_dev) {
		pr_err("fb_dev can't be found\n");
		return -ENXIO;
	}
	ret = snprintf(buf, PAGE_SIZE,
			"current frame_count: %lld\n", fb_dev->frame_count);

	return ret;
}

#ifdef CONFIG_FB_ESD_SUPPORT
static ssize_t sysfs_rd_current_esd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
//	struct attr_info *attr_info = fb_dev->priv1;

	if (!fb_dev) {
		pr_err("fb_dev can't be found\n");
		return -ENXIO;
	}
	ret = snprintf(buf, PAGE_SIZE,
			"current esd: %u\n",fb_dev->ESD_work_start);

	return ret;
}

static ssize_t sysfs_write_esd(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret, esd;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
//	struct attr_info *attr_info = fb_dev->priv1;

	ret = kstrtoint(buf, 10, &esd);

	esd = (esd == 1) ? 1:0;

	if ((1 == esd) && (fb_dev->enable == 1)) {
		if (!fb_dev->ESD_work_start) {
			printk("sprdfb: schedule ESD work queue!\n");
			schedule_delayed_work(&fb_dev->ESD_work, msecs_to_jiffies(fb_dev->ESD_timeout_val));
			fb_dev->ESD_work_start = true;
		}
	} else {
		if (fb_dev->ESD_work_start == true) {
			printk("sprdfb: cancel ESD work queue\n");
			cancel_delayed_work_sync(&fb_dev->ESD_work);
			fb_dev->ESD_work_start = false;
		}
	}
	return count;
}
#endif

int sprdfb_create_sysfs(struct sprdfb_device *fb_dev)
{
	int rc;
	struct panel_spec* panel = fb_dev->panel;
	struct attr_info *attr;

	fb_dev->priv1 = kzalloc(sizeof(struct attr_info), GFP_KERNEL);
	if (!fb_dev->priv1) {
		pr_err("shortage of memory\n");
		return -ENOMEM;
	}
	attr = fb_dev->priv1;

	proc_create("dispc_osd", 0444, NULL, &dispc_osd_fops);
	rc = sysfs_create_group(&fb_dev->fb->dev->kobj, &sprdfb_attrs_group);
	if (rc)
		pr_err("sysfs group creation failed, rc=%d\n", rc);

	attr->origin_pclk = fb_dev->dpi_clock;
	if (panel) {
		attr->origin_fps = panel->fps;
		if (panel->type == LCD_MODE_DSI)
			attr->origin_mipi_clk = panel->info.mipi->phy_feq;
	}
	sema_init(&attr->sem, 1);

	return rc;
}

void sprdfb_remove_sysfs(struct sprdfb_device *fb_dev)
{
	struct attr_info *attr = fb_dev->priv1;
	sysfs_remove_group(&fb_dev->fb->dev->kobj, &sprdfb_attrs_group);

	if (attr)
		kfree(attr);
}
