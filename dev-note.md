## DTS diff

```diff
diff --git a/sysdrv/source/kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dts b/sysdrv/source/kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dts
index 85ce5b81d..336d03515 100755
--- a/sysdrv/source/kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dts
+++ b/sysdrv/source/kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dts
@@ -12,6 +12,10 @@
 / {
        model = "Luckfox Pico Mini";
        compatible = "rockchip,rv1103g-38x38-ipc-v10", "rockchip,rv1103";
+
+       chosen {
+               bootargs = "earlycon=uart8250,mmio32,0xff4c0000 console=tty0 console=ttyFIQ0 root=/dev/mmcblk1p7 rootwait snd_soc_core.prealloc_buffer_size_kbytes=16 coherent_pool=0";
+       };
 };

 /**********SFC**********/
@@ -57,12 +61,52 @@ &usbdrd_dwc3 {
        dr_mode = "peripheral";
 };

+&pinctrl {
+       tft {
+               tft_pins: tft_pins {
+                       rockchip,pins =
+                               /* tft_dc */
+                               <1 RK_PC3 RK_FUNC_GPIO &pcfg_pull_none>,
+                               /* tft_reset */
+                               <1 RK_PC4 RK_FUNC_GPIO &pcfg_pull_none>;
+               };
+       };
+};
+
 /**********SPI**********/
 /* SPI0_M0 */
 &spi0 {
-       status = "disabled";
+       pinctrl-0 = <&spi0m0_clk &spi0m0_mosi &spi0m0_cs0>;
+       status = "okay";
+
        spidev@0 {
                spi-max-frequency = <50000000>;
+               status = "disabled";
+       };
+
+       fbtft@0 {
+               status = "disabled";
+       };
+
+       tft: st7305@0 {
+               #address-cells = <1>;
+               #size-cells = <1>;
+
+               pinctrl-names = "default";
+               pinctrl-0 = <&tft_pins>;
+
+               // compatible = "osptek,ydp154h008-v3";
+               // compatible = "osptek,ydp213h001-v3";
+               compatible = "osptek,ydp290h001-v3";
+               // compatible = "osptek,ydp420h001-v3";
+
+               spi-max-frequency = <50000000>;
+               reg = <0>;
+
+               reset-gpios = <&gpio1 RK_PC3 GPIO_ACTIVE_HIGH>;
+               dc-gpios = <&gpio1 RK_PC4 GPIO_ACTIVE_HIGH>;
+
+               status = "okay";
        };
 };
```

## Run time dts overlay

```bash
dtc -@ -Hepapr -I dts -O dtb -o st7305-overlay.dtbo st7305-overlay.dts
adb push st7305-overlay.dtbo /tmp
```

```bash
mkdir -p /sys/kernel/config/device-tree/overlays/st7305
cat st7305-overlay.dtbo > /sys/kernel/config/device-tree/overlays/st7305/dtbo
echo 1 > /sys/kernel/config/device-tree/overlays/st7305/status
```

```bash
cat /sys/firmware/devicetree/base/spi@ff500000/st7305@0/compatible
```

<!-- ```c
    spi@ff500000 {
        compatible = "rockchip,rv1106-spi", "rockchip,rk3066-spi";
        reg = <0xff500000 0x00001000>;
        interrupts = <0x00000000 0x00000017 0x00000004>;
        #address-cells = <0x00000001>;
        #size-cells = <0x00000000>;
        clocks = <0x00000002 0x000000cd 0x00000002 0x000000cc 0x00000002 0x000000ce>;
        clock-names = "spiclk", "apb_pclk", "sclk_in";
        dmas = <0x0000003f 0x00000001 0x0000003f 0x00000000>;
        dma-names = "tx", "rx";
        pinctrl-names = "default";
        pinctrl-0 = <0x00000048 0x00000049 0x0000004a>;
        status = "okay";
        st7305@0 {
            #address-cells = <0x00000001>;
            #size-cells = <0x00000001>;
            pinctrl-names = "default";
            pinctrl-0 = <0x0000004c>;
            compatible = "osptek,ydp290h001-v3";
            spi-max-frequency = <0x02faf080>;
            reg = <0x00000000>;
            reset-gpios = <0x00000036 0x00000013 0x00000000>;
            dc-gpios = <0x00000036 0x00000014 0x00000000>;
            status = "okay";
        };
    };

        tft {
            tft_pins {
                rockchip,pins = <0x00000001 0x00000013 0x00000000 0x00000063 0x00000001 0x00000014 0x00000000 0x00000063>;
                phandle = <0x0000004c>;
            };
        };
``` -->
