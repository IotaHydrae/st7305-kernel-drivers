Run time dts overlay

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
