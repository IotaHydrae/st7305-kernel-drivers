// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for display panels connected to a Sitronix st7305
 * display controller in SPI mode.
 *
 * This driver is inspired by:
 *   https://github.com/DuRuofu/esp-idf-st7305-Ink-screen
 *
 * Copyright (c) 2025 DuRuofu <duruofu@qq.com>
 * Copyright (c) 2025 Wooden Chair <hua.zheng@embeddedboys.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "dither.h"

#define DRV_NAME "st7305"

#define ST7305_MADCTL_MY BIT(7) // Page Address Order
#define ST7305_MADCTL_MX BIT(6) // Column Address Order
#define ST7305_MADCTL_MV BIT(5) // Page/Column Order
#define ST7305_MADCTL_DO BIT(4) // Data Order, using in MX=1
#define ST7305_MADCTL_GS BIT(3) // Data refresh Bottom to Top

struct st7305 {
	struct device *dev;
	struct mipi_dbi_dev *dbidev;
	struct mipi_dbi *dbi;
	struct drm_device *drm;

	/* TODO: support TE */
	struct gpio_desc *te;

	u8 dither_type;

	const struct st7305_panel_desc *desc;
};

struct st7305_panel_desc {
	const struct drm_display_mode *mode;

	u8 caset[2]; // column address start->end
	u8 raset[2]; // row address start->end

	u8 left_offset; // offset pixels from the left

	u8 page_size; // each page contains two rows
	u8 page_count;

	size_t bufsize;

	int (*init_seq)(struct st7305 *st7305);
};

static inline struct st7305 *dbi_to_st7305(struct mipi_dbi *dbi)
{
	return spi_get_drvdata(dbi->spi);
}

static inline struct st7305 *dbidev_to_st7305(struct mipi_dbi_dev *dbidev)
{
	return dbi_to_st7305(&dbidev->dbi);
}

/*
 * The device tree node may specify the wrong GPIO
 * active behavior, hard-coded as low active here
 */
static inline void st7305_reset(struct mipi_dbi *dbi)
{
	gpiod_set_raw_value(dbi->reset, 0);
	msleep(10);
	gpiod_set_raw_value(dbi->reset, 1);
	msleep(10);
}

static void st7305_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct st7305 *st7305 = dbi_to_st7305(dbi);
	u8 addr_mode;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	st7305_reset(dbi);

	mipi_dbi_command(dbi, 0xD1, 0x01); // Booster Enable
	mipi_dbi_command(dbi, 0xC0, 0x12, 0x0A); // Gate Voltage Setting

	mipi_dbi_command(dbi, 0xC1, 0x3C, 0x3E, 0x3C,
			 0x3C); // VSHP Setting (4.8V)
	mipi_dbi_command(dbi, 0xC2, 0x23, 0x21, 0x23,
			 0x23); // VSLP Setting (0.98V)
	mipi_dbi_command(dbi, 0xC4, 0x5A, 0x5C, 0x5A,
			 0x5A); // VSHN Setting (-3.6V)
	mipi_dbi_command(dbi, 0xC5, 0x37, 0x35, 0x37,
			 0x37); // VSLN Setting (0.22V)

	mipi_dbi_command(dbi, 0xD8, 0x80, 0xE9);

	mipi_dbi_command(dbi, 0xB2, 0x02); // Frame Rate Control

	// Update Period Gate EQ Control in HPM
	mipi_dbi_command(dbi, 0xB3, 0xE5, 0xF6, 0x17, 0x77, 0x77, 0x77, 0x77,
			 0x77, 0x77, 0x71);
	// Update Period Gate EQ Control in LPM
	mipi_dbi_command(dbi, 0xB4, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76,
			 0x45);
	mipi_dbi_command(dbi, 0x62, 0x32, 0x03, 0x1F); // Gate Timing Control

	mipi_dbi_command(dbi, 0xB7, 0x13); // Source EQ Enable

	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(120);

	mipi_dbi_command(dbi, 0xC9, 0x00); // Source Voltage Select

	switch (dbidev->rotation) {
	default:
		addr_mode = ST7305_MADCTL_MX | ST7305_MADCTL_GS;
		break;
	case 90:
	case 180:
	case 270:
		addr_mode = ST7305_MADCTL_MX | ST7305_MADCTL_GS;
		printk("%s, Rotation is not supported yet.", __func__);
		break;
	}
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT,
			 0x11); // 3 write for 24bit
	mipi_dbi_command(dbi, 0xB9, 0x20); // Gamma Mode Setting
	mipi_dbi_command(dbi, 0xB8, 0x29); // Panel Setting
	mipi_dbi_command(dbi, 0xD0, 0xFF); // Auto power down
	mipi_dbi_command(dbi, 0x38); // High Power Mode on
	mipi_dbi_command(dbi, 0xBB, 0x4F); // Enable Clear RAM
	mipi_dbi_command(dbi, MIPI_DCS_ENTER_INVERT_MODE);
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	st7305->desc->init_seq(st7305);

	drm_dev_exit(idx);
}

static void st7305_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;

	DRM_DEBUG_KMS("\n");

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
}

static inline void st7305_draw_pixel(u8 *dst, uint x, uint y, u8 left_offset,
				     u8 page_size, u8 gray)
{
	uint new_x = x + left_offset;
	u32 byte_index = ((y >> 1) * page_size) + (new_x >> 2);
	u32 bit_index = ((new_x & 3) << 1) | (y & 1);
	u8 mask = 1u << (7 - bit_index);
	u8 set = (gray >> 7) * mask;

	dst[byte_index] = (dst[byte_index] & ~mask) | set;
}

static void st7305_xrgb8888_to_mono(u8 *dst, void *vaddr,
				    struct drm_framebuffer *fb,
				    struct drm_rect *clip,
				    struct drm_format_conv_state *fmtcnv_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	size_t len = (clip->x2 - clip->x1) * (clip->y2 - clip->y1);
	struct st7305 *st7305 = dbidev_to_st7305(dbidev);
	struct iosys_map dst_map, vmap;
	u8 *src, *buf, *dither_buf;
	u8 offset, page_size;
	unsigned int x, y;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	offset = st7305->desc->left_offset;
	page_size = st7305->desc->page_size;

	iosys_map_set_vaddr(&dst_map, buf);
	iosys_map_set_vaddr(&vmap, vaddr);
	drm_fb_xrgb8888_to_gray8(&dst_map, NULL, &vmap, fb, clip, fmtcnv_state);
	src = buf;

	if (st7305->dither_type > 0) {
		dither_buf = kzalloc(fb->width * fb->height, GFP_KERNEL);
		if (!dither_buf)
			goto free_buf;

		dither_gray8_to_bw(st7305->dither_type, buf, dither_buf,
				   fb->width, fb->height);
		src = dither_buf;
	}

	for (y = clip->y1; y < clip->y2; y++)
		for (x = clip->x1; x < clip->x2; x++)
			st7305_draw_pixel(dst, x, y, offset, page_size, *src++);

	if (st7305->dither_type > 0) {
		kfree(dither_buf);
	}

free_buf:
	kfree(buf);
}

static int st7305_buf_copy(void *dst, struct iosys_map *src,
			   struct drm_framebuffer *fb, struct drm_rect *clip,
			   struct drm_format_conv_state *fmtcnv_state)
{
	int ret;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	st7305_xrgb8888_to_mono(dst, src->vaddr, fb, clip, fmtcnv_state);

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);

	return ret;
}

static void st7305_fb_dirty(struct iosys_map *src, struct drm_framebuffer *fb,
			    struct drm_rect *rect,
			    struct drm_format_conv_state *fmtcnv_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct st7305 *st7305 = dbi_to_st7305(dbi);
	const u8 *caset, *raset;
	size_t bufsize;
	int ret = 0;

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id,
		      DRM_RECT_ARG(rect));

	caset = st7305->desc->caset;
	raset = st7305->desc->raset;
	bufsize = st7305->desc->bufsize;

	ret = st7305_buf_copy(dbidev->tx_buf, src, fb, rect, fmtcnv_state);
	if (ret)
		goto err_msg;

	mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS, caset[0], caset[1]);

	mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS, raset[0], raset[1]);

	ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
				   (u8 *)dbidev->tx_buf, bufsize);
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n",
			     ret);
}

static void st7305_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct st7305 *st7305 = dbidev_to_st7305(dbidev);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state =
		to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect rect;
	int idx;

	if (!pipe->crtc.state->active)
		return;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	if (st7305->dither_type > 0) {
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = fb->width;
		rect.y2 = fb->height;
		st7305_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
				&shadow_plane_state->fmtcnv_state);
	} else {
		if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
			st7305_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
					&shadow_plane_state->fmtcnv_state);
		}
	}

	drm_dev_exit(idx);
}

static const u32 st7305_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_simple_display_pipe_funcs st7305_pipe_funcs = {
	.mode_valid = mipi_dbi_pipe_mode_valid,
	.enable = st7305_pipe_enable,
	.disable = st7305_pipe_disable,
	.update = st7305_pipe_update,
	.begin_fb_access = mipi_dbi_pipe_begin_fb_access,
	.end_fb_access = mipi_dbi_pipe_end_fb_access,
	.reset_plane = mipi_dbi_pipe_reset_plane,
	.duplicate_plane_state = mipi_dbi_pipe_duplicate_plane_state,
	.destroy_plane_state = mipi_dbi_pipe_destroy_plane_state,
};

static ssize_t dither_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct st7305 *st7305 = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", st7305->dither_type);
}

static ssize_t dither_type_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct st7305 *st7305 = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val >= DITHER_TYPE_MAX)
		return -EINVAL;

	st7305->dither_type = val;

	dev_info(dev, "set dither type to %lu\n", val);

	return count;
}

static DEVICE_ATTR_RW(dither_type);

static struct attribute *st7305_attrs[] = {
	&dev_attr_dither_type.attr,
	NULL,
};

static const struct attribute_group st7305_attr_group = {
	.name = "config",
	.attrs = st7305_attrs
};

static int ydp154h008_v3_init_seq(struct st7305 *st7305)
{
	struct mipi_dbi *dbi = st7305->dbi;

	mipi_dbi_command(dbi, 0xD6, 0x17, 0x02); // NVM Load Control
	mipi_dbi_command(dbi, 0xB0, 0x32); // Gate Line Setting: 200 line

	return 0;
}

static const struct drm_display_mode ydp154h008_v3_mode = {
	DRM_SIMPLE_MODE(200, 200, 28, 28),
};

static const struct st7305_panel_desc ydp154h008_v3_desc = {
	.mode = &ydp154h008_v3_mode,

	.caset[0] = 0x16,
	.caset[1] = 0x26,

	.raset[0] = 0x00,
	.raset[1] = 0x63,

	.left_offset = 4,

	.page_size = 51, // 200/8*2=50≈51 (3 bytes per write)
	.page_count = 100, // 200/2=100

	.bufsize = 51 * 100,

	.init_seq = ydp154h008_v3_init_seq,
};

static int ydp213h001_v3_init_seq(struct st7305 *st7305)
{
	struct mipi_dbi *dbi = st7305->dbi;

	mipi_dbi_command(dbi, 0xD6, 0x17, 0x02); // NVM Load Control
	mipi_dbi_command(dbi, 0xC0, 0x0E, 0x05); // Gate Voltage Setting
	// mipi_dbi_command(dbi, 0xB2, 0x15); // Frame Rate Control

	mipi_dbi_command(dbi, 0xB0, 0x3F); // Gate Line Setting: 252 line

	return 0;
}

static const struct drm_display_mode ydp213h001_v3_mode = {
	DRM_SIMPLE_MODE(122, 250, 24, 49),
};

static const struct st7305_panel_desc ydp213h001_v3_desc = {
	.mode = &ydp213h001_v3_mode,

	.caset[0] = 0x19,
	.caset[1] = 0x23,

	.raset[0] = 0x00,
	.raset[1] = 0x7C,

	.left_offset = 10,

	.page_size = 33, // 122/4=30.5≈33 (3 bytes per write)
	.page_count = 125, // 252/2=125

	.bufsize = 33 * 125,

	.init_seq = ydp213h001_v3_init_seq,
};

static int ydp290h001_v3_init_seq(struct st7305 *st7305)
{
	struct mipi_dbi *dbi = st7305->dbi;

	mipi_dbi_command(dbi, 0xD6, 0x13, 0x02); // NVM Load Control
	mipi_dbi_command(dbi, 0xB0, 0x60); // Gate Line Setting: 384 line

	return 0;
}

static const struct drm_display_mode ydp290h001_v3_mode = {
	DRM_SIMPLE_MODE(168, 384, 29, 67),
};

static const struct st7305_panel_desc ydp290h001_v3_desc = {
	.mode = &ydp290h001_v3_mode,

	.caset[0] = 0x17,
	.caset[1] = 0x24, // 0X24-0X17=14 // 14*4*3=168

	.raset[0] = 0x00,
	.raset[1] = 0xBF, // 192*2=384

	.left_offset = 0,

	.page_size = 42, // 168/8*2=42
	.page_count = 192,

	.bufsize = 42 * 192,

	.init_seq = ydp290h001_v3_init_seq,
};

static int ydp420h001_v3_init_seq(struct st7305 *st7305)
{
	struct mipi_dbi *dbi = st7305->dbi;

	mipi_dbi_command(dbi, 0xD6, 0x17, 0x02); // NVM Load Control
	mipi_dbi_command(dbi, 0xC0, 0x11, 0x04); // Gate Voltage Setting

	mipi_dbi_command(dbi, 0xC1, 0x37, 0x37, 0x37,
			 0x37); // VSHP Setting (4.8V)
	mipi_dbi_command(dbi, 0xC2, 0x19, 0x19, 0x19,
			 0x19); // VSLP Setting (0.5V)
	mipi_dbi_command(dbi, 0xC4, 0x41, 0x41, 0x41,
			 0x41); // VSHN Setting (-3.8V)
	mipi_dbi_command(dbi, 0xC5, 0x19, 0x19, 0x19,
			 0x19); // VSLN Setting (0.5V)

	mipi_dbi_command(dbi, 0x35, 0x00);
	mipi_dbi_command(dbi, 0xD8, 0xA6, 0xE9);
	mipi_dbi_command(dbi, 0xB0, 0x64); // Gate Line Setting: 400 line

	return 0;
}

static const struct drm_display_mode ydp420h001_v3_mode = {
	DRM_SIMPLE_MODE(300, 400, 64, 85),
};

static const struct st7305_panel_desc ydp420h001_v3_desc = {
	.mode = &ydp420h001_v3_mode,

	.caset[0] = 0x05,
	.caset[1] = 0x36,

	.raset[0] = 0x00,
	.raset[1] = 0xC7,

	.left_offset = 144,

	.page_size = 150,
	.page_count = 200,

	.bufsize = 150 * 200,

	.init_seq = ydp420h001_v3_init_seq,
};

DEFINE_DRM_GEM_DMA_FOPS(st7305_fops);

static struct drm_driver st7305_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &st7305_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.debugfs_init = mipi_dbi_debugfs_init,
	.name = "st7305",
	.desc = "Sitronix ST7305",
	.date = "20251022",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id st7305_of_match[] = {
	{ .compatible = "sitronix,st7305", .data = &ydp290h001_v3_desc },
	{ .compatible = "osptek,ydp154h008-v3", .data = &ydp154h008_v3_desc },
	{ .compatible = "osptek,ydp213h001-v3",
	  .data = &ydp213h001_v3_desc }, /* FIXME: display freezes after a few seconds */
	{ .compatible = "osptek,ydp290h001-v3", .data = &ydp290h001_v3_desc },
	{ .compatible = "osptek,ydp420h001-v3", .data = &ydp420h001_v3_desc },
	{ .compatible = "wlk,w420hc018mono-122", .data = &ydp420h001_v3_desc },

	{},
};
MODULE_DEVICE_TABLE(of, st7305_of_match);

static const struct spi_device_id st7305_id[] = {
	{ "st7305" },	     { "ydp154h008-v3" }, { "ydp213h001-v3" },
	{ "ydp290h001-v3" }, { "ydp420h001-v3" }, {},
};
MODULE_DEVICE_TABLE(spi, st7305_id);

static int st7305_probe(struct spi_device *spi)
{
	const struct drm_display_mode *mode;
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct st7305 *st7305;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u16 width, height;
	u32 rotation = 0;
	size_t bufsize;
	int ret;

	st7305 = devm_kzalloc(dev, sizeof(*st7305), GFP_KERNEL);
	if (IS_ERR(st7305))
		return -ENOMEM;

	dbidev = devm_drm_dev_alloc(dev, &st7305_driver, struct mipi_dbi_dev,
				    drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	st7305->dev = dev;
	st7305->dbidev = dbidev;
	st7305->drm = drm;
	st7305->dbi = dbi;
	st7305->desc = of_device_get_match_data(dev);
	if (!st7305->desc)
		return -ENODEV;

	st7305->dither_type = DITHER_TYPE_NONE;
	// st7305->dither_type = DITHER_TYPE_BAYER_16X16;

	mode = st7305->desc->mode;
	width = mode->hdisplay;
	height = mode->vdisplay;
	bufsize = st7305->desc->bufsize;
	dev_info(dev, "bufsize: %zu (bytes)\n", bufsize);

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

	/*
	 * we are using 8-bit data, so we are not actually swapping anything,
	 * but setting mipi->swap_bytes makes mipi_dbi_typec3_command() do the
	 * right thing and not use 16-bit transfers (which results in swapped
	 * bytes on little-endian systems and causes out of order data to be
	 * sent to the display).
	 */
	dbi->swap_bytes = true;

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight)) {
		DRM_DEV_ERROR(dev, "Failed to get backlight device\n");
		return PTR_ERR(dbidev->backlight);
	}

	device_property_read_u32(dev, "rotation", &rotation);
	dev_info(dev, "rotation: %d\n", rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	/* SDO signal is not available on this panel. */
	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init_with_formats(dbidev, &st7305_pipe_funcs,
					     st7305_formats,
					     ARRAY_SIZE(st7305_formats), mode,
					     rotation, bufsize);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, st7305);
	dev_set_drvdata(dev, st7305);

	drm_fbdev_dma_setup(drm, 0);

	ret = sysfs_create_group(&dev->kobj, &st7305_attr_group);
	if (ret)
		dev_err(dev, "Failed to create device attrs\n");

	dev_info(dev, "%ux%u mipi-dbi@%uMHz - ready\n", width, height,
		 spi->max_speed_hz / 1000000);

	return 0;
}

static void st7305_remove(struct spi_device *spi)
{
	struct st7305 *st7305 = spi_get_drvdata(spi);
	struct drm_device *drm = st7305->drm;

	DRM_DEBUG_KMS("\n");

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	sysfs_remove_group(&st7305->dev->kobj, &st7305_attr_group);
}

static void st7305_shutdown(struct spi_device *spi)
{
	struct st7305 *st7305 = spi_get_drvdata(spi);
	drm_atomic_helper_shutdown(st7305->drm);
}

static struct spi_driver st7305_spi_driver = {
	.driver =
	{
		.name = DRV_NAME,
		.of_match_table = st7305_of_match,
	},
	.id_table = st7305_id,
	.probe = st7305_probe,
	.remove = st7305_remove,
	.shutdown = st7305_shutdown,
};
module_spi_driver(st7305_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7305 DRM driver");
MODULE_AUTHOR("Wooden Chair <hua.zheng@embeddedboys.com>");
MODULE_LICENSE("GPL");
