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

struct st7305 {
	struct mipi_dbi_dev *dbidev;
	struct drm_devie *drm;

	struct gpio_desc *dc;
};

struct st7305_panel_desc {
	const struct drm_display_mode *mode;

	int (*init_seq)(struct st7305 *st7305);
};

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
	struct mipi_dbi *dbi = &dbidev->dbi;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	st7305_reset(dbi);

	mipi_dbi_command(dbi, MIPI_DCS_SOFT_RESET);
	msleep(10);

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
	mipi_dbi_command(dbi, 0x36, BIT(6) | BIT(3)); // Memory Data Access Control
	mipi_dbi_command(dbi, 0x3A, 0x11); // Data Format Select
	mipi_dbi_command(dbi, 0xB9, 0x20); // Gamma Mode Setting
	mipi_dbi_command(dbi, 0xB8, 0x29); // Panel Setting
	mipi_dbi_command(dbi, 0x2A, 0x17, 0x24); // Column Address Setting
	mipi_dbi_command(dbi, 0x2B, 0x00, 0xBF); // Row Address Setting
	mipi_dbi_command(dbi, 0xD0, 0xFF); // Auto power down
	mipi_dbi_command(dbi, 0x38); // High Power Mode on
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	drm_dev_exit(idx);
}

static void st7305_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	DRM_DEBUG_KMS("\n");
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
}

static inline void st7305_draw_pixel(u8 *dst, uint x, uint y, u8 gray)
{
	u32 byte_index = ((y >> 1) * 42) + (x >> 2);
	u32 bit_index = ((x & 3) << 1) | (y & 1);
	u8 mask = 1u << (7 - bit_index);
	u8 set = (gray >> 7) * mask;

	dst[byte_index] = (dst[byte_index] & ~mask) | set;
}

static void st7305_xrgb8888_to_mono(u8 *dst, void *vaddr,
				    struct drm_framebuffer *fb,
				    struct drm_rect *clip,
				    struct drm_format_conv_state *fmtcnv_state)
{
	size_t len = (clip->x2 - clip->x1) * (clip->y2 - clip->y1);
	struct iosys_map dst_map, vmap;
	unsigned int x, y;
	u8 *src, *buf;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	iosys_map_set_vaddr(&dst_map, buf);
	iosys_map_set_vaddr(&vmap, vaddr);
	drm_fb_xrgb8888_to_gray8(&dst_map, NULL, &vmap, fb, clip, fmtcnv_state);
	src = buf;

	for (y = clip->y1; y < clip->y2; y++)
		for (x = clip->x1; x < clip->x2; x++)
			st7305_draw_pixel(dst, x, y, *src++);

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
	int ret = 0;

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id,
		      DRM_RECT_ARG(rect));

	ret = st7305_buf_copy(dbidev->tx_buf, src, fb, rect, fmtcnv_state);
	if (ret)
		goto err_msg;

	mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS, 0x17, 0x24);

	mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0xBF);

	ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
				   (u8 *)dbidev->tx_buf, (168 + 24) * 14 * 3);
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n",
			     ret);
}

static void st7305_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_state)
{
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

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		st7305_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
				&shadow_plane_state->fmtcnv_state);

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

static const struct drm_display_mode st7305_mode = {
	DRM_SIMPLE_MODE(168, 384, 55, 90),
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
	{ .compatible = "sitronix,st7305" },
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

	drm_fbdev_dma_setup(drm, 0);

	return 0;
}

static void st7305_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
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
