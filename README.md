适用于 ST7305 反射式 TFT 的内核 DRM 驱动

[English](README.en.md)

## 特性

- 支持多个屏幕型号


| 硬件信息 |                                                                             |
| -------- | --------------------------------------------------------------------------- |
| 开发板   | Raspberry Pi Compute Model 4                                                |
| 内核版本 | 6.12.47+rpt-rpi-v8                                                          |
| 发行版   | Debian GNU/Linux 12 (bookworm)                                              |
| 显示屏   | [YDP154H008-V3](https://yuyinglcd.com/products/1/17/500) 1.54" Mono 200x200 |
| -        | [YDP213H001-V3](https://yuyinglcd.com/products/1/17/260) 2.13" Mono 122x250 |
| -        | [YDP290H001-V3](https://yuyinglcd.com/products/1/17/261) 2.90" Mono 168x384 |
| -        | [YDP420H001-V3](https://yuyinglcd.com/products/1/17/262) 4.20" Mono 300x400 |
| - | [W420HC018MONO-12Z](https://item.taobao.com/item.htm?id=871831722804&mi_id=0000vBUbFkosMzENLINW0DNEpJD3qW3wnoilcGBA0fK5Eus&skuId=5724504589973&spm=tbpc.boughtlist.suborder_itemtitle.1.6f4d2e8dbWO3RS) 4.20" Mono 300x400 |
| 驱动IC   | ST7305                                                                      |


![bmo](./assets/ydp154h008_v3_bmo.jpg)![stress](./assets/ydp213h001_v3_stress.jpg)
![widgets](./assets/ydp290h001_v3_widgets.jpg)![widgets](./assets/ydp420h001_v3_widgets.jpg)

## 快速上手

以下步骤假设您使用的是 YDP420H001-V3 显示屏。

| 屏幕引脚定义 | 树莓派的引脚       |
| ------------ | ------------------ |
| GND          | GND                |
| VCC          | 3.3V               |
| SCL          | SPI0 SCLK - GPIO11 |
| SDA          | SPI0 MOSI - GPIO10 |
| RES          | GPIO25             |
| DC           | GPIO22             |
| CS           | SPI0 CE0 - GPIO8   |
| (TE)         | GPIO18             |

您需要安装这些软件包才能在本地构建内核模块

```bash

```

克隆本仓库，编译驱动和设备树

```bash
git clone https://github.com/IotaHydrae/st7305-kernel-drivers.git
git checkout main
make
make dtb
```

运行时加载设备树 overlay 和驱动

```bash
sudo dtoverlay ./st7305-drmfb-dtbo
sudo insmod ./st7305-drmfb.ko
```

如果一切正常，屏幕此时已显示内容

将设备树改动保存到设备上

```bash
sudo cp ./st7305-drmfb.dtbo /boot/firmware/overlays/
sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.bak
echo "dtoverlay=st7305-drmfb" | sudo tee -a /boot/firmware/config.txt
```

### 安装桌面环境

```bash
sudo apt install --no-install-recommends xserver-xorg xinit xfce4
sudo apt install dbus-x11 firefox fonts-wqy-zenhei
```

你需要从如下两种驱动方式选择其一

#### 1. xserver 使用 fbdev 驱动显示

```bash
sudo vim /usr/share/X11/xorg.conf.d/99-fbdev.conf

# 将如下内容复制到文件中
Section "Device"
    Identifier "ST7305"
    Driver "fbdev"
    Option "fbdev" "/dev/fb0"
EndSection
```

#### 2. xserver 使用 modesetting 驱动显示

```bash
sudo vim /usr/share/X11/xorg.conf.d/99-fbdev.conf

# 将如下内容复制到文件中
Section "Device"
    Identifier "TinyDRM Display"
    Driver "modesetting"
    Option "AccelMethod" "glamor"
    Option "kmsdev" "/dev/dri/card2"
    Option "SWcursor" "true"
EndSection
```

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
