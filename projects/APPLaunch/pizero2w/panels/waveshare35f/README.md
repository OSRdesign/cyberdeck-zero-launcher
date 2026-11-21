# Waveshare 3.5" RPi LCD (F) on an Allwinner H616 / H618 board (`install.sh --spi-panel waveshare35f`)

The files `install.sh --spi-panel waveshare35f [--soft-pwm-backlight]` puts on an Armbian board (an Orange Pi Zero 2W)
for the Waveshare 3.5" RPi LCD (F) (ST7796S 320x480 on SPI1, Goodix GT911 touch on I2C1, the backlight on header pin 12)
plugged on the 40-pin header. They are the board's hand-made, tested set-up of 2026-11-21 (Armbian 26.11, kernel
6.18.64-current-sunxi64). How and why: `projects/APPLaunch/pizero2w/README.md` (the bundle's `README.md`), "Orange Pi
Zero 2W: the 3.5" SPI panel".

| File | What | Origin, licence |
| --- | --- | --- |
| `st7796s.bin` | The panel's init sequence for the kernel's `panel-mipi-dbi` driver, installed as `/lib/firmware/st7796s.bin`; install.sh checks its sha256 `17204e39cce35fba857ad2dff14243e1d3a958c4dac00283f8df9b7ad5147cc7` | Waveshare's file for this panel (their wiki, "3.5inch RPi LCD (F)"); a register list, 117 bytes |
| `spi35-waveshare35f-gpio-backlight.dts` | The device-tree overlay with the on/off `gpio-backlight` (`backlight_gpio`): compiled with `dtc -@` to `/boot/overlay-user/spi35-st7796s.dtbo` | This project (MIT, `LICENSE`) |
| `spi35-waveshare35f-softpwm.dts` | The same with a software PWM backlight (`pwm-gpio` + `pwm-backlight`, node `backlight_pwm`, 10 levels at 2 kHz), for `--soft-pwm-backlight` | This project (MIT) |
| `dkms-drm-mipi-dbi-chunk/` | `Makefile`, `dkms.conf` and `chunk.patch` of the DKMS module `drm-mipi-dbi-chunk`: the kernel's `drivers/gpu/drm/drm_mipi_dbi.c`, fetched by install.sh for the running kernel's upstream tag, with each SPI transfer capped at 64 KiB (the H618's DMA cannot reach RAM above 4 GB, swiotlb takes 256 KiB at most, a frame is 307200 bytes). install.sh adds `BUILD_EXCLUSIVE_KERNEL` for that kernel series to `dkms.conf` | Makefile, dkms.conf, patch: this project (MIT); the patched `drm_mipi_dbi.c` is the kernel's (GPL-2.0-or-later), not shipped |
| `dkms-pwm-gpio/` | The DKMS module `pwm-gpio` (software PWM on a GPIO), for `--soft-pwm-backlight`: the Armbian kernel has `CONFIG_PWM_GPIO` off | `pwm-gpio.c`: Linux 6.18 `drivers/pwm/pwm-gpio.c`, GPL-2.0-only (its SPDX line and copyright notices kept; `LICENSES/GPL-2.0.txt`); Makefile, dkms.conf: this project (MIT) |

Line endings: these files must keep LF (`.gitattributes`): a CRLF `Makefile` or `dkms.conf` breaks the DKMS build and a
CRLF `chunk.patch` does not apply.
