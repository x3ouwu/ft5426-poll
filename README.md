# ft5426-poll

Multi-touch for cheap **Raspberry Pi 7" DSI touchscreen clones** on boards
whose vendor kernel treats them as a one-finger mouse.

A small Linux kernel module (`ft5426_poll`) that drives the FocalTech FT5426
touch controller of these panels by polling, reports up to **5 fingers**, and
marks the device as a touchscreen, so pinch-zoom, two-finger scroll and other
gestures work in browsers and Wayland desktops.

Tested on a **Radxa ROCK 3C** (RK3566) with Radxa's Debian 12 image
(kernel `6.1.84-*-rk2410`). The driver itself is board-independent; only the
device tree overlay is ROCK 3C specific.

## The problem

Many 7" 800×480 DSI panels sold as "for Raspberry Pi" (under many brand names)
copy the official Raspberry Pi display: a Toshiba **TC358762** DSI bridge and
a FocalTech **FT5426** capacitive touch controller at I²C address `0x38`.
On these clones the touch controller's interrupt line is **not wired** to the
board, so the touch has to be polled.

Radxa's kernel drives the touch with `raspits_ft5426`, which:

- reads and reports **one** touch point (`MAX_TOUCH_POINTS 1`);
- registers its slots without `INPUT_MT_DIRECT`, so the device has no
  `INPUT_PROP_DIRECT` and libinput classifies it as a **touchpad**: the screen
  moves a pointer like a mouse instead of acting as a touchscreen;
- advertises axis ranges one pixel too large (0..800 and 0..480).

The mainline `edt-ft5x06` driver supports the FT5426, but in Linux 6.1 it
requires an interrupt line, which these panels do not have.

## What this driver does

- Reads the touch status and all 5 points in one I²C transfer every 16 ms.
- Reports multi-touch protocol B with `INPUT_MT_DIRECT`; slots that disappear
  from a frame are released automatically.
- Rotates the coordinates 180° by default, like the vendor driver, so touches
  line up with the image of these panels. Set `invert=0` if yours is the other
  way round.

It does **not** drive the display: the panel and bridge stay with the vendor
`raspits,tc358762` driver.

## Is this my panel?

With the vendor overlay `rock-3c-raspberrypi-7inch-touchscreen` enabled:

```sh
sudo dmesg | grep -E 'raspits|tc358762'   # "raspits-ft5426: ... address = 0x38", "find panel: raspits,tc358762"
cat /proc/bus/input/devices               # N: Name="fts_ts", and B: PROP=0 (no DIRECT flag)
```

If touch works but behaves like a single-finger mouse, this driver is for you.

## Install (Radxa ROCK 3C, Debian)

Requirements: kernel headers for the running kernel (Radxa images ship
`linux-headers-*`), `dkms`, `device-tree-compiler`.

```sh
sudo apt install dkms device-tree-compiler

# 1. Build and install the module with DKMS (rebuilt on kernel updates)
sudo mkdir -p /usr/src/ft5426-poll-1.0
sudo cp ft5426_poll.c Makefile dkms.conf /usr/src/ft5426-poll-1.0/
sudo dkms install ft5426-poll/1.0

# 2. Enable the vendor Raspberry Pi 7" overlay if it is not on yet
#    (rsetup -> Overlays, or rename the .dtbo.disabled file in /boot/dtbo)
ls /boot/dtbo/rock-3c-raspberrypi-7inch-touchscreen.dtbo

# 3. Compile and install this overlay
sudo dtc -q -@ -I dts -O dtb \
  -o /boot/dtbo/rock-3c-rpi-7inch-multitouch.dtbo \
  overlays/rock-3c-rpi-7inch-multitouch.dts
sudo u-boot-update
```

### Check before you reboot

A device tree overlay that U-Boot cannot apply makes the board boot the
rescue entry: no network, and with Radxa's locked root account no console
login either. Test the exact overlay chain that U-Boot will use first:

```sh
CHAIN=$(grep -m1 fdtoverlays /boot/extlinux/extlinux.conf | sed 's/^\s*fdtoverlays\s*//')
echo "$CHAIN"   # rpi-7inch-multitouch must come AFTER raspberrypi-7inch-touchscreen
fdtoverlay -i /usr/lib/linux-image-$(uname -r)/rockchip/rk3566-rock-3c.dtb \
  -o /tmp/chain.dtb $CHAIN \
  && fdtget /tmp/chain.dtb /i2c@fe5c0000/rockpi_ft5406@38 compatible
# expected: focaltech,ft5426-poll
```

Only if that prints `focaltech,ft5426-poll`:

```sh
sudo reboot
```

### Verify

```sh
sudo dmesg | grep ft5426_poll
# ft5426_poll 3-0038: FT5426 fw 0x0b, 5 points, polled every 16 ms
grep -A9 'FT5426 touchscreen' /proc/bus/input/devices | grep PROP
# B: PROP=2   (INPUT_PROP_DIRECT)
sudo evtest   # pick "FT5426 touchscreen" and put several fingers down
```

## Module parameters

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `width`   | 800  | Panel width in pixels |
| `height`  | 480  | Panel height in pixels |
| `poll_ms` | 16   | Poll interval in milliseconds (about 60 Hz) |
| `invert`  | 1    | Rotate touch coordinates 180° |

Set them in `/etc/modprobe.d/ft5426_poll.conf`, for example
`options ft5426_poll invert=0`, then reboot.

## Uninstall or recover

```sh
sudo rm /boot/dtbo/rock-3c-rpi-7inch-multitouch.dtbo
sudo u-boot-update
sudo dkms remove ft5426-poll/1.0 --all
sudo reboot
```

If the board already boots into rescue mode, put the SD card into another
Linux machine, mount its root partition (at `/mnt` here) and remove the
overlay from the boot configuration:

```sh
sudo rm /mnt/boot/dtbo/rock-3c-rpi-7inch-multitouch.dtbo
sudo sed -i 's# /boot/dtbo/rock-3c-rpi-7inch-multitouch.dtbo##' /mnt/boot/extlinux/extlinux.conf
```

## Limitations

- Polling costs one 31-byte I²C read every 16 ms, even when nobody touches
  the screen.
- Only the ROCK 3C overlay is provided. On another board, bind the driver to
  the controller at `0x38` with an overlay that sets
  `compatible = "focaltech,ft5426-poll"` on its I²C node.
- An on-screen keyboard and desktop gestures depend on the desktop, not on
  this driver.

## Mainline kernels

Not tested yet. What we found (November 2026), for anyone who wants to pick
this up:

| Piece | Mainline status |
|-------|-----------------|
| ROCK 3C board | `rk3566-rock-3c.dts` since Linux 6.10; U-Boot `rock-3c-rk3566_defconfig` since v2024.10. DSI is not enabled in it. |
| DSI host (RK3566/RK3568, VOP2) | `dw-mipi-dsi-rockchip` with RK3568 support since 6.1; `dsi1` at `fe070000` is the one this panel uses. |
| Panel power, backlight | `rpi-panel-attiny-regulator` (I²C `0x45`). It accepts only firmware IDs `0xde` and `0xc3`; our clone's MCU answers `0xc3` at register `0x80`, so it should work unchanged. |
| DSI bridge, panel | `toshiba,tc358762` bridge + panel-simple `powertip,ph800480t013-idf02`. The i.MX8MP overlay `imx8mp-venice-gw74xx-rpidsi.dtso` is a complete template for a non-Raspberry Pi SoC. |
| Touch | `edt-ft5x06` supports the FT5426 but still requires an IRQ. A series by Marek Vasut (January 2026) adds polling when no IRQ is given; it was not merged as of 7.4-rc1. This module is tested on 6.1 only; it switches to the single-argument `probe` on 6.3+ but has not been built there yet. |
| Wi-Fi / BT (AIC8800) | No in-tree driver; out-of-tree DKMS only (`radxa-pkg/aic8800`), which broke on 7.1 and 7.4 before being fixed. |

So a mainline setup needs a ROCK 3C DSI overlay (none exists yet, also not in
Armbian), either the `edt-ft5x06` polling series or this module, and the
out-of-tree AIC8800 driver or a USB Wi-Fi adapter with an in-tree driver.

## Plans

- Read size and orientation from the standard device tree properties
  (`touchscreen-size-x`, `touchscreen-inverted-x`, …) instead of module
  parameters.
- A mainline device tree overlay for the panel on the ROCK 3C.

## License

GPL-2.0-only, see [LICENSE](LICENSE).
