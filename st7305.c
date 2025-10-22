// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for display panels connected to a Sitronix st7305
 * display controller in SPI mode.
 *
 * Copyright 2025 Wooden Chair <hua.zheng@embeddedboys.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_rect.h>

struct st7305 {
	struct mipi_dbi_dev *dbidev;
	struct drm_devie *drm;

	struct gpio_desc *dc;
	u8 *dgram;
};

struct st7305_panel_desc {
	const struct drm_display_mode *mode;

	int (*init_seq)(struct st7305 *st7305);
};

static const u8 dgram_lut[256] = {
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54, 0x01, 0x41, 0x11, 0x51,
	0x05, 0x45, 0x15, 0x55, 0x80, 0xC0, 0x90, 0xD0, 0x84, 0xC4, 0x94, 0xD4,
	0x81, 0xC1, 0x91, 0xD1, 0x85, 0xC5, 0x95, 0xD5, 0x20, 0x60, 0x30, 0x70,
	0x24, 0x64, 0x34, 0x74, 0x21, 0x61, 0x31, 0x71, 0x25, 0x65, 0x35, 0x75,
	0xA0, 0xE0, 0xB0, 0xF0, 0xA4, 0xE4, 0xB4, 0xF4, 0xA1, 0xE1, 0xB1, 0xF1,
	0xA5, 0xE5, 0xB5, 0xF5, 0x08, 0x48, 0x18, 0x58, 0x0C, 0x4C, 0x1C, 0x5C,
	0x09, 0x49, 0x19, 0x59, 0x0D, 0x4D, 0x1D, 0x5D, 0x88, 0xC8, 0x98, 0xD8,
	0x8C, 0xCC, 0x9C, 0xDC, 0x89, 0xC9, 0x99, 0xD9, 0x8D, 0xCD, 0x9D, 0xDD,
	0x28, 0x68, 0x38, 0x78, 0x2C, 0x6C, 0x3C, 0x7C, 0x29, 0x69, 0x39, 0x79,
	0x2D, 0x6D, 0x3D, 0x7D, 0xA8, 0xE8, 0xB8, 0xF8, 0xAC, 0xEC, 0xBC, 0xFC,
	0xA9, 0xE9, 0xB9, 0xF9, 0xAD, 0xED, 0xBD, 0xFD, 0x02, 0x42, 0x12, 0x52,
	0x06, 0x46, 0x16, 0x56, 0x03, 0x43, 0x13, 0x53, 0x07, 0x47, 0x17, 0x57,
	0x82, 0xC2, 0x92, 0xD2, 0x86, 0xC6, 0x96, 0xD6, 0x83, 0xC3, 0x93, 0xD3,
	0x87, 0xC7, 0x97, 0xD7, 0x22, 0x62, 0x32, 0x72, 0x26, 0x66, 0x36, 0x76,
	0x23, 0x63, 0x33, 0x73, 0x27, 0x67, 0x37, 0x77, 0xA2, 0xE2, 0xB2, 0xF2,
	0xA6, 0xE6, 0xB6, 0xF6, 0xA3, 0xE3, 0xB3, 0xF3, 0xA7, 0xE7, 0xB7, 0xF7,
	0x0A, 0x4A, 0x1A, 0x5A, 0x0E, 0x4E, 0x1E, 0x5E, 0x0B, 0x4B, 0x1B, 0x5B,
	0x0F, 0x4F, 0x1F, 0x5F, 0x8A, 0xCA, 0x9A, 0xDA, 0x8E, 0xCE, 0x9E, 0xDE,
	0x8B, 0xCB, 0x9B, 0xDB, 0x8F, 0xCF, 0x9F, 0xDF, 0x2A, 0x6A, 0x3A, 0x7A,
	0x2E, 0x6E, 0x3E, 0x7E, 0x2B, 0x6B, 0x3B, 0x7B, 0x2F, 0x6F, 0x3F, 0x7F,
	0xAA, 0xEA, 0xBA, 0xFA, 0xAE, 0xEE, 0xBE, 0xFE, 0xAB, 0xEB, 0xBB, 0xFB,
	0xAF, 0xEF, 0xBF, 0xFF
};

static u8 *g_st7305_dgram = NULL;

static void st7305_fb_dirty(struct drm_framebuffer *fb, struct drm_rect *rect);

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

static inline void st7305_set_orientation(struct mipi_dbi *dbi, u8 rotation)
{
}

static void st7305_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct drm_framebuffer *fb = plane_state->fb;
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	st7305_reset(dbi);

	mipi_dbi_command(dbi, 0xD6, 0x13, 0x02); // NVM Load Control
	mipi_dbi_command(dbi, 0xD1, 0x01); // Booster Enable
	mipi_dbi_command(dbi, 0xC0, 0x08, 0x06); // Gate Voltage Setting

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
	mipi_dbi_command(dbi, 0xB0, 0x60); // Gate Line Setting: 384 line

	mipi_dbi_command(dbi, 0x11); // Sleep out
	msleep(120);

	mipi_dbi_command(dbi, 0xC9, 0x00); // Source Voltage Select
	mipi_dbi_command(dbi, 0x36, 0x00); // Memory Data Access Control
	mipi_dbi_command(dbi, 0x3A, 0x11); // Data Format Select
	mipi_dbi_command(dbi, 0xB9, 0x20); // Gamma Mode Setting
	mipi_dbi_command(dbi, 0xB8, 0x29); // Panel Setting
	mipi_dbi_command(dbi, 0x2A, 0x17, 0x24); // Column Address Setting
	mipi_dbi_command(dbi, 0x2B, 0x00, 0xBF); // Row Address Setting
	mipi_dbi_command(dbi, 0xD0, 0xFF); // Auto power down
	mipi_dbi_command(dbi, 0x38); // High Power Mode on
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	st7305_fb_dirty(fb, &rect);

	drm_dev_exit(idx);
}

static void st7305_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	DRM_DEBUG_KMS("\n");
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
}

static inline u8 __pack_byte(u8 b1, u8 b2)
{
	u8 mix = 0;
	mix |= ((b1 & 0x01) << 7) | ((b2 & 0x01) << 6);
	mix |= ((b1 & 0x02) << 4) | ((b2 & 0x02) << 3);
	mix |= ((b1 & 0x04) << 1) | ((b2 & 0x04) << 0);
	mix |= ((b1 & 0x08) >> 2) | ((b2 & 0x08) >> 3);
	return mix;
}

static void __maybe_unused st7305_convert_buffer(u8 *dst, void *vaddr,
						 struct drm_framebuffer *fb)
{
	u8 *buf8 = vaddr;
	int h = 384;
	u16 y, i, j, k = 0;
	u8 b1, b2;

	for (i = 0; i < h; i += 2) {
		// Convert 2 columns
		for (j = 0; j < 21; j += 3) {
			for (y = 0; y < 3; y++) {
				b1 = buf8[(j + y) * h + i];
				b2 = buf8[(j + y) * h + i + 1];

				// First 4 bits
				dst[k++] = __pack_byte(b1, b2);
				// Second 4 bits
				dst[k++] = __pack_byte(b1 >> 4, b2 >> 4);
			}
		}
	}
}

static void st7305_convert_buffer_lut(u8 *dst, void *vaddr,
				      struct drm_framebuffer *fb)
{
	u8 *buf8 = vaddr;
	int h = 384;
	u16 y, i, j, k = 0;
	u8 b1, b2;

	for (i = 0; i < h; i += 2) {
		// Convert 2 columns
		for (j = 0; j < 21; j += 3) {
			for (y = 0; y < 3; y++) {
				b1 = buf8[(j + y) * h + i];
				b2 = buf8[(j + y) * h + i + 1];

				dst[k++] = dgram_lut[((b1 & 0x0F) << 4) |
						     (b2 & 0x0F)];

				b1 >>= 4;
				b2 >>= 4;
				dst[k++] = dgram_lut[((b1 & 0x0F) << 4) |
						     (b2 & 0x0F)];
			}
		}
	}
}

static inline void st7305_put_pixel(int x, int y, u8 *dgram, u8 gray,
				    struct drm_framebuffer *fb, u8 rotation)
{
	int w = fb->width;
	int h = fb->height;
	u8 new_x = x, new_y = y;
	u16 byte_idx;
	u8 bit_mask;

	switch (rotation) {
	case 1: // 90 degrees clockwise
		new_x = h - y;
		new_y = x;
		break;
	case 2: // 180 degrees
		new_x = w - x;
		new_y = h - y;
		break;
	case 3: // 270 degrees clockwise
		new_x = y;
		new_y = w - x;
		break;
	default: // 0 degrees
		break;
	}

	byte_idx = (new_y >> 3) * h + new_x;
	bit_mask = BIT(new_y & 0x7);

	if (gray > 128)
		dgram[byte_idx] |= bit_mask;
	else
		dgram[byte_idx] &= ~bit_mask;
}

static void st7305_xrgb8888_to_monochrome(u8 *dst, void *vaddr,
					  struct drm_framebuffer *fb,
					  struct drm_rect *clip)
{
	size_t len = (clip->x2 - clip->x1) * (clip->y2 - clip->y1);
	unsigned int x, y;
	u8 *buf;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	drm_fb_xrgb8888_to_gray8(buf, vaddr, fb, clip);

	for (y = clip->y1; y < clip->y2; y++)
		for (x = clip->x1; x < clip->x2; x++)
			st7305_put_pixel(x, y, g_st7305_dgram, *buf++, fb, 3);

	st7305_convert_buffer_lut(dst, g_st7305_dgram, fb);

	kfree(buf);
}

static int st7305_buf_copy(void *dst, struct drm_framebuffer *fb,
			   struct drm_rect *clip)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct dma_buf_attachment *import_attach = cma_obj->base.import_attach;
	void *src = cma_obj->vaddr;
	int ret = 0;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	st7305_xrgb8888_to_monochrome(dst, src, fb, clip);

	if (import_attach)
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);

	return ret;
}

static void st7305_fb_dirty(struct drm_framebuffer *fb, struct drm_rect *rect)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 caset[] = { 0x17, 0x17 + 14 - 1 };
	u8 raset[] = { 0x00, 0x00 + 192 - 1 };
	int ret = 0;
	int idx;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id,
		      DRM_RECT_ARG(rect));

	ret = st7305_buf_copy(dbidev->tx_buf, fb, rect);
	if (ret)
		goto err_msg;

	mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS, caset[0], caset[1]);

	mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS, raset[0], raset[1]);

	ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
				   (u8 *)dbidev->tx_buf, (168 + 24) * 14 * 3);
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n",
			     ret);

	drm_dev_exit(idx);
}

static void st7305_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect rect;

	if (!pipe->crtc.state->active)
		return;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		st7305_fb_dirty(state->fb, &rect);
}

static const u32 st7305_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_simple_display_pipe_funcs st7305_pipe_funcs = {
	.enable = st7305_pipe_enable,
	.disable = st7305_pipe_disable,
	.update = st7305_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode st7305_mode = {
	DRM_SIMPLE_MODE(168, 384, 55, 90),
};

DEFINE_DRM_GEM_CMA_FOPS(st7305_fops);

static struct drm_driver st7305_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &st7305_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init = mipi_dbi_debugfs_init,
	.name = "st7305",
	.desc = "Sitronix ST7305",
	.date = "20251022",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id st7305_of_match[] = {
	{ .compatible = "sitronix,st7567" },
	{},
};
MODULE_DEVICE_TABLE(of, st7305_of_match);

static const struct spi_device_id st7305_id[] = {
	{ "st7305" },
	{},
};
MODULE_DEVICE_TABLE(spi, st7305_id);

static int st7305_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	size_t bufsize;
	int ret;

	// bufsize = (192) * 14 * 3;
	bufsize = (384 * 21);
	g_st7305_dgram = kzalloc(bufsize, GFP_KERNEL);
	if (!g_st7305_dgram) {
		dev_err(dev, "Failed to allocate memory for g_st7305_dgram\n");
		return -ENOMEM;
	}

	dbidev = devm_drm_dev_alloc(dev, &st7305_driver, struct mipi_dbi_dev,
				    drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	bufsize = (st7305_mode.vdisplay * st7305_mode.hdisplay * sizeof(u16));

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

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
	printk("rotation: %d\n", rotation);
	printk("spi max frequency: %d (Hz)\n", spi->max_speed_hz);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init_with_formats(dbidev, &st7305_pipe_funcs,
					     st7305_formats,
					     ARRAY_SIZE(st7305_formats),
					     &st7305_mode, rotation, bufsize);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int st7305_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	printk("%s\n", __func__);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	if (g_st7305_dgram) {
		kfree(g_st7305_dgram);
		g_st7305_dgram = NULL;
	}

	return 0;
}

static void st7305_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver st7305_spi_driver = {
	.driver =
	{
		.name = "st7305",
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
