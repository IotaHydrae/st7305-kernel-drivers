# Kernel DRM drivers for TFT Displays based on ST7305

| Hardware Info  |                                                                             |
|----------------|-----------------------------------------------------------------------------|
| Dev Board      | Luckfox Pico Mini                                                           |
| Kernel Version | 5.10.160                                                                    |
| Distro         | Buildroot 2023.02.6                                                         |
| Displays       | [YDP154H008-V3](https://yuyinglcd.com/products/1/17/500) 1.54" Mono 200x200 |
| -              | [YDP213H001-V3](https://yuyinglcd.com/products/1/17/260) 2.13" Mono 122x250 |
| -              | [YDP290H001-V3](https://yuyinglcd.com/products/1/17/261) 2.90" Mono 168x384 |
| -              | [YDP420H001-V3](https://yuyinglcd.com/products/1/17/262) 4.20" Mono 300x400 |

![bmo](./assets/ydp154h008_v3_bmo.jpg)![stress](./assets/ydp213h001_v3_stress.jpg)![widgets](./assets/ydp290h001_v3_widgets.jpg)

![console](./assets/ydp420h001_v3_console.jpg)![widgets](./assets/ydp420h001_v3_widgets.jpg)



https://github.com/user-attachments/assets/9526318e-5c00-406e-a91f-2dd308e9b231



## Get Started

The fllowing steps assume you are using YDP290H001-V3 as the display.

### 1. Setup Luckfox Pico SDK

It's highly recommended take a look at the [SDK Compilation Guide](https://wiki.luckfox.com/zh/Luckfox-Pico-Plus-Mini/SDK-Image-Compilation) before proceeding.

```bash
mkdir -p ~/luckfox && cd ~/luckfox
git clone https://gitee.com/LuckfoxTECH/luckfox-pico.git pico
cd pico
```

According to the luckfox pico wiki, you need to install the following packages:
```bash
sudo apt-get install -y git ssh make gcc gcc-multilib g++-multilib module-assistant expect g++ gawk texinfo libssl-dev bison flex fakeroot cmake unzip gperf autoconf device-tree-compiler libncurses5-dev pkg-config bc python-is-python3 passwd openssl openssh-server openssh-client vim file cpio rsync curl
```

Then config the SDK with following steps:

```bash
❯ ./build.sh lunch
You're building on Linux
  Lunch menu...pick the Luckfox Pico hardware version:
  选择 Luckfox Pico 硬件版本:
                [0] RV1103_Luckfox_Pico
                [1] RV1103_Luckfox_Pico_Mini
                [2] RV1103_Luckfox_Pico_Plus
                [3] RV1103_Luckfox_Pico_WebBee
                [4] RV1106_Luckfox_Pico_Pro_Max
                [5] RV1106_Luckfox_Pico_Ultra
                [6] RV1106_Luckfox_Pico_Ultra_W
                [7] RV1106_Luckfox_Pico_Pi
                [8] RV1106_Luckfox_Pico_Pi_W
                [9] RV1106_Luckfox_Pico_86Panel
                [10] RV1106_Luckfox_Pico_86Panel_W
                [11] RV1106_Luckfox_Pico_Zero
                [12] custom
Which would you like? [0~12][default:0]: 1
  Lunch menu...pick the boot medium:
  选择启动媒介:
                [0] SD_CARD
                [1] SPI_NAND
Which would you like? [0~1][default:0]: 0
  Lunch menu...pick the system version:
  选择系统版本:
                [0] Buildroot
Which would you like? [0][default:0]: 0
[build.sh:info] Lunching for Default BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1103_Luckfox_Pico_Mini-IPC.mk boards...
[build.sh:info] switching to board: /home/developer/luckfox/pico/project/cfg/BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1103_Luckfox_Pico_Mini-IPC.mk
[build.sh:info] Running build_select_board succeeded.
```

Build the kernel and driver at least once:

```bash
./build.sh kernel
./build.sh driver
```

### 2. Replace the kernel dts

Clone this repo first
```bash
cd ~/luckfox
git clone https://github.com/IotaHydrae/st7305-kernel-drivers.git
cd st7305-kernel-drivers
```

if you are using other display, modify the compatible string in the dts file(`rv1103g-luckfox-pico-mini.dts`) first

```c
	tft: st7305@0 {
		...

		// compatible = "osptek,ydp154h008-v3";
		// compatible = "osptek,ydp213h001-v3";
		compatible = "osptek,ydp290h001-v3";
		// compatible = "osptek,ydp420h001-v3";

		...
	};
```

Also, if you want the framebuffer console feature, you’ll want to make sure to keep this DTS node:
```c
chosen {
		bootargs = "earlycon=uart8250,mmio32,0xff4c0000 console=tty0 console=ttyFIQ0 root=/dev/mmcblk1p7 rootwait snd_soc_core.prealloc_buffer_size_kbytes=16 coherent_pool=0";
	};
```

copy the dts file to luckfox pico sdk

```bash
cp rv1103g-luckfox-pico-mini.dts ~/luckfox/pico/sysdrv/source/kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dts
```

### 3. Build and flash the new kernel img to Luckfox Pico

go back to the luckfox pico sdk and build the new kernel img
```bash
cd ~/luckfox/pico
./build.sh kernel
```


reflash the new kernel img `output/image/boot.img` by running following commands:

```bash
adb push output/image/boot.img /tmp
adb shell 'dd if=/tmp/boot.img of=/dev/mmcblk1p4 bs=1M'
adb reboot
```

### 4. Build and test st7305 driver

```bash
cd ~/luckfox/st7305-kernel-drivers
make && adb push st7305_tinydrm.ko /tmp
adb shell 'insmod /tmp/st7305_tinydrm.ko'
```

#### 4.1 (TODO) Run lvgl demo

## References

1. [kernel 5.10.160 source](https://elixir.bootlin.com/linux/v5.10.160/source)
2. [Luckfox Pico Wiki - Pinout](https://wiki.luckfox.com/zh/Luckfox-Pico-Plus-Mini/Pinout)
3. [ST7305 datasheet](https://admin.osptek.com/uploads/ST_7305_V0_2_d0b99d9cdb.pdf)
4. [DuRuofu's st7305 drivers for esp32](https://github.com/DuRuofu/esp-idf-st7305-Ink-screen)
