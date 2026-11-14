# ft5426-poll

Multi-touch for cheap **Raspberry Pi 7" DSI touchscreen clones** on boards
whose vendor kernel treats them as a one-finger mouse.

A small Linux kernel module (`ft5426_poll`) that drives the FocalTech FT5426
touch controller of these panels by polling, reports up to **5 fingers**, and
marks the device as a touchscreen, so pinch-zoom, two-finger scroll and other
gestures work in browsers and Wayland desktops.

Tested on a **Radxa ROCK 3C** (RK3566) with Radxa's Debian 12 image
(kernel `6.1.84-*-rk2410`) and on mainline Linux 6.18. The driver itself is
board-independent; only the device tree overlays are ROCK 3C specific.

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

It does **not** drive the display. On the vendor kernel the panel and bridge
stay with the vendor `raspits,tc358762` driver; for mainline see
[Mainline kernels](#mainline-kernels).

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

Tested on **Linux 6.18.62** (NixOS 26.05) on the ROCK 3C: picture, backlight
and 5-point touch work. Three pieces are not in mainline:

1. **Device tree overlay** for the panel on the ROCK 3C:
   [`overlays/rock-3c-rpi-7inch-dsi-mainline.dts`](overlays/rock-3c-rpi-7inch-dsi-mainline.dts)
   (DSI host, bridge, panel, panel MCU, touch). It keeps `vcc_mipi` always on:
   otherwise the panel MCU loses power when the kernel switches off unused
   regulators.
2. **A patched `tc358762` bridge driver.** The in-tree driver asks the DSI host
   for sync pulses and HSE packets; with the RK3566 DSI host the picture is then
   shifted about 150 pixels sideways and wraps around. DSI porches have no
   effect on it. Burst mode, which Radxa's vendor driver uses, gives a correct
   picture: [`tc358762/tc358762-burst.patch`](tc358762/tc358762-burst.patch).
3. **This touch module** (`edt-ft5x06` still requires an IRQ line).

The in-tree pieces work unchanged: `dw-mipi-dsi-rockchip`, `phy-rockchip-inno-dsidphy`,
`rpi-panel-attiny-regulator` (this clone's MCU answers `0xc3`), panel-simple
`powertip,ph800480t013-idf02`.

### Install on a mainline distribution

Needs kernel headers, `dtc`, `curl`, `patch`. The board's boot loader must
apply the overlay (e.g. `fdtoverlays` in `extlinux.conf`, or merge it into the
board DTB with `fdtoverlay`).

```sh
# touch module (DKMS, as above)
sudo mkdir -p /usr/src/ft5426-poll-1.0
sudo cp ft5426_poll.c Makefile dkms.conf /usr/src/ft5426-poll-1.0/
sudo dkms install ft5426-poll/1.0

# bridge driver: kernel.org source of the running version + burst patch
cd tc358762 && ./get-source.sh && make
sudo install -Dm644 tc358762.ko /lib/modules/$(uname -r)/updates/tc358762.ko
sudo depmod -a      # updates/ is preferred over the in-tree module
cd ..

# overlay
dtc -q -@ -I dts -O dtb -o rock-3c-rpi-7inch-dsi.dtbo \
  overlays/rock-3c-rpi-7inch-dsi-mainline.dts
```

The bridge module has to be rebuilt for every new kernel (re-run the three
`tc358762` lines). The touch here is rotated 180° (`invert=1`, the default).

| Piece | Mainline status |
|-------|-----------------|
| ROCK 3C board | `rk3566-rock-3c.dts` since Linux 6.10; U-Boot `rock-3c-rk3566_defconfig` since v2024.10. DSI is not enabled in it. |
| Touch | A series by Marek Vasut (January 2026) adds polling to `edt-ft5x06` when no IRQ is given; not merged as of 7.4-rc1. |
| Wi-Fi / BT (AIC8800) | No in-tree driver; out-of-tree DKMS only (`radxa-pkg/aic8800`). |

## Plans

- Read size and orientation from the standard device tree properties
  (`touchscreen-size-x`, `touchscreen-inverted-x`, …) instead of module
  parameters.
- Upstream the bridge fix: let the device tree choose the DSI mode of
  `tc358762` instead of hard-coding it.
- A DKMS package for the bridge module.

## License

GPL-2.0-only, see [LICENSE](LICENSE).
