/*
 * linux/drivers/video/fb_sys_read.c - Generic file operations where
 * framebuffer is in system RAM
 *
 * Copyright (C) 2007 Antonino Daplas <adaplas@pol.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/export.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/uaccess.h>

ssize_t fb_sys_read(struct fb_info *info, char __user *buf, size_t count,
		    loff_t *ppos)
{
	unsigned long p = *ppos;
	void *src;
	int err = 0;
	unsigned long total_size, c;
	ssize_t ret;

	if (!(info->flags & FBINFO_VIRTFB))
		fb_warn_once(info, "Framebuffer is not in virtual address space.");

	if (!info->screen_buffer)
		return -ENODEV;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	src = info->screen_buffer + p;

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	c = copy_to_user(buf, src, count);
	if (c)
		err = -EFAULT;
	ret = count - c;

	*ppos += ret;

	return ret ? ret : err;
}
EXPORT_SYMBOL_GPL(fb_sys_read);

ssize_t fb_sys_write(struct fb_info *info, const char __user *buf,
		     size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	void *dst;
	int err = 0;
	unsigned long total_size, c;
	size_t ret;

	if (!(info->flags & FBINFO_VIRTFB))
		fb_warn_once(info, "Framebuffer is not in virtual address space.");

	if (!info->screen_buffer)
		return -ENODEV;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	dst = info->screen_buffer + p;

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	c = copy_from_user(dst, buf, count);
	if (c)
		err = -EFAULT;
	ret = count - c;

	*ppos += ret;

	return ret ? ret : err;
}
EXPORT_SYMBOL_GPL(fb_sys_write);

MODULE_AUTHOR("Antonino Daplas <adaplas@pol.net>");
MODULE_DESCRIPTION("Generic file read (fb in system RAM)");
MODULE_LICENSE("GPL");
