适用于 ST7305 反射式 TFT 的内核 DRM 驱动

[English](README.en.md)

## 特性

- 支持多个屏幕型号

| 硬件信息 |                                                                             |
| -------- | --------------------------------------------------------------------------- |
| 开发板   | Orange Pi One                                                               |
| 内核版本 | 6.12.58-current-sunxi                                                       |
| 发行版   | Armbian 25.11.2 noble                                                       |
| 显示屏   | [YDP154H008-V3](https://yuyinglcd.com/products/1/17/500) 1.54" Mono 200x200 |
| -        | [YDP213H001-V3](https://yuyinglcd.com/products/1/17/260) 2.13" Mono 122x250 |
| -        | [YDP290H001-V3](https://yuyinglcd.com/products/1/17/261) 2.90" Mono 168x384 |
| -        | W290HC019MONO-12Z 2.90" Mono 168x384                                        |
| -        | W420HC018MONO-12Z 4.20" Mono 300x400                                        |
| 驱动IC   | ST7305                                                                      |
| -        | -                                                                           |
| 显示屏   | [YDP420H001-V3](https://yuyinglcd.com/products/1/17/262) 4.20" Mono 300x400 |
| 驱动IC   | ST7306                                                                      |

![bmo](./assets/ydp154h008_v3_bmo.jpg)![stress](./assets/ydp213h001_v3_stress.jpg)
![widgets](./assets/ydp290h001_v3_widgets.jpg)![widgets](./assets/ydp420h001_v3_widgets.jpg)

## 快速上手

以下步骤假设您使用的是 YDP420H001-V3 显示屏。

| 屏幕引脚定义 | Orange Pi One 的引脚 |
| ------------ | -------------------- |
| GND          | GND                  |
| VCC          | 3.3V                 |
| SCL          | PC2 - SPI0 SCLK      |
| SDA          | PC0 - SPI0 MOSI      |
| RES          | PA2                  |
| DC           | PA3                  |
| CS           | PC3 - SPI0 CE0       |
| (TE)         | PD14                 |

您需要安装这些软件包才能在本地构建内核模块

```bash

```

克隆本仓库，编译驱动和设备树

```bash
git clone https://github.com/IotaHydrae/st7305-kernel-drivers.git -b orangepi-one-armbian
make
make dtb
```

将设备树改动保存到设备上

```bash
sudo cp sun8i-h3-spi-st7305-drm.dtbo /boot/dtb/overlay/
# 或者
make dtb_store
```

参考 `armbianEnv.txt` 文件内容中的 overlays 选项，在尾部追加 `spi-st7305-drm`，这是一个示例

```ini
overlays=cpu-clock-1.368GHz-1.3v i2c0 spi-st7305-drm
```

然后重启设备，使更改生效，等待重启完成后，切换到仓库目录执行

```bash
sudo insmod st7305-drmfb.ko
```

如果一切正常，屏幕应该显示 armbian 控制台

### 可能的特性

- [ ] 驱动层旋转支持

### 一些有用的命令

开关控制台光标闪烁

```
echo 0 | sudo tee /sys/class/graphics/fbcon/cursor_blink
echo 1 | sudo tee /sys/class/graphics/fbcon/cursor_blink
```

## 参考

1. [ST7305 datasheet](https://admin.osptek.com/uploads/ST_7305_V0_2_d0b99d9cdb.pdf)
2. [DuRuofu's st7305 drivers for esp32](https://github.com/DuRuofu/esp-idf-st7305-Ink-screen)

## Links

- [W290HC019MONO-12Z](https://item.taobao.com/item.htm?id=871831722804&mi_id=0000vBUbFkosMzENLINW0DNEpDu1mdlByTlb9U8Knb0Kg2E&skuId=5706172761739&spm=tbpc.boughtlist.suborder_itemtitle.1.16b02e8dATbioT)
- [W420HC018MONO-12Z](https://item.taobao.com/item.htm?id=871831722804&mi_id=0000vBUbFkosMzENLINW0DNEpJD3qW3wnoilcGBA0fK5Eus&skuId=5724504589973&spm=tbpc.boughtlist.suborder_itemtitle.1.6f4d2e8dbWO3RS)
