适用于 ST7305 反射式 TFT 的内核 DRM 驱动

[English](README.en.md)

| 硬件信息 |                                                                             |
| -------- | --------------------------------------------------------------------------- |
| 开发板   | Raspberry Pi Compute Model 4                                                |
| 内核版本 | 6.12.47+rpt-rpi-v8                                                          |
| 发行版   | Debian GNU/Linux 12 (bookworm)                                              |
| 显示屏   | [YDP154H008-V3](https://yuyinglcd.com/products/1/17/500) 1.54" Mono 200x200 |
| -        | [YDP213H001-V3](https://yuyinglcd.com/products/1/17/260) 2.13" Mono 122x250 |
| -        | [YDP290H001-V3](https://yuyinglcd.com/products/1/17/261) 2.90" Mono 168x384 |
| -        | [YDP420H001-V3](https://yuyinglcd.com/products/1/17/262) 4.20" Mono 300x400 |
| 驱动IC   | ST7305                                                                      |

![console](./assets/ydp154h008_v3_console.jpg)![bmo](./assets/ydp154h008_v3_bmo.jpg)![stress](./assets/ydp213h001_v3_stress.jpg)
![widgets](./assets/ydp290h001_v3_widgets.jpg)![console](./assets/ydp420h001_v3_console.jpg)![widgets](./assets/ydp420h001_v3_widgets.jpg)

https://github.com/user-attachments/assets/9526318e-5c00-406e-a91f-2dd308e9b231

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

```shell

```

克隆本仓库，编译驱动和设备树

```shell
git clone https://github.com/IotaHydrae/st7305-kernel-drivers.git
git checkout main
make
```

运行时加载设备树 overlay 和驱动

```shell
sudo dtoverlay ./st7305-drmfb-dtbo
sudo insmod ./st7305-drmfb.ko
```

如果一切正常，屏幕此时已显示内容

将设备树改动保存到设备上

```shell
sudo cp ./st7305-drmfb.dtbo /boot/firmware/overlays/
sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.bak
echo "dtoverlay=st7305-drmfb" | sudo tee -a /boot/firmware/config.txt
```

### 可能的特性

- [ ] 驱动层旋转支持

## 参考

1. [ST7305 datasheet](https://admin.osptek.com/uploads/ST_7305_V0_2_d0b99d9cdb.pdf)
2. [DuRuofu's st7305 drivers for esp32](https://github.com/DuRuofu/esp-idf-st7305-Ink-screen)
