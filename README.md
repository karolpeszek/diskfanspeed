# diskfanspeed

Temperature-based fan control for a DIY disk station: 4x 3.5" HDDs in a USB
enclosure, cooled by 2x Noctua 5V fans (Y-splitter) driven from an Arduino
Uno's PWM pin 9.

- `arduino/diskfan_controller/` - sketch that generates true 25kHz PWM on
  pin 9 (Noctua spec) and takes plaintext speed commands over USB serial.
- `daemon/diskfanspeedd` - Linux daemon. Polls `smartctl` for each
  configured disk every `poll_interval` seconds, takes the *highest* temp,
  maps it to a fan speed with a linear curve, and sends it to the Arduino.
  Exposes a control socket for the CLI.
- `cli/diskfanspeed` - talks to the daemon over its control socket.

## Wiring

- Both Noctua 5V fans' PWM wires join at a Y-splitter, single PWM signal
  into Arduino pin 9.
- Fan +5V/GND powered from a suitable 5V source (not necessarily the
  Arduino's own 5V rail if both fans draw more current than it can supply).
- Arduino connected to the host via USB (shows up as `/dev/ttyACM0`).

## Arduino setup

1. Open `arduino/diskfan_controller/diskfan_controller.ino` in the Arduino
   IDE (or `arduino-cli`).
2. Flash to the Uno.
3. Confirm it enumerates as `/dev/ttyACM0` on the host (`dmesg | tail`
   after plugging in).

Protocol: plaintext integer percent (`0`-`100`) + newline over serial sets
the duty cycle; `?` queries it. If the Arduino gets no command for 90
seconds (daemon crashed, USB unplugged, etc.) it fails safe to 100% -
same default it boots with before the daemon connects.

## Host install

Requires Python 3, `pyserial`, and `smartmontools`:

```
sudo apt install python3-serial smartmontools
```

Install the daemon and CLI:

```
sudo install -m 755 daemon/diskfanspeedd /usr/local/sbin/diskfanspeedd
sudo install -m 755 cli/diskfanspeed /usr/local/bin/diskfanspeed
sudo mkdir -p /etc/diskfanspeed
sudo install -m 644 config/diskfanspeedd.conf /etc/diskfanspeed/diskfanspeedd.conf
sudo install -m 644 systemd/diskfanspeedd.service /etc/systemd/system/diskfanspeedd.service
```

Create the socket group and add yourself to it if you want to run the CLI
without `sudo`:

```
sudo groupadd -f diskfanspeed
sudo usermod -aG diskfanspeed "$USER"
```
(log out/in for the group change to apply)

Edit `/etc/diskfanspeed/diskfanspeedd.conf`:

- `disks.devices` - the 4 disk device paths (check with `lsblk` /
  `ls /dev/disk/by-id/`; **use stable `/dev/disk/by-id/...` paths** if your
  enclosure doesn't guarantee `/dev/sdX` ordering across reboots).
- `disks.smart_device_type` - usually `sat` for USB-SATA bridges; if
  `smartctl -a -d sat /dev/sdX` doesn't return temps, try
  `smartctl --scan` or `-d sat,12` / `-d usbjmicron` variants and set a
  per-disk `[disk:/dev/sdX]` override.
- `fan_curve` - tune `min_temp_c`/`max_temp_c`/`min_pwm`/`max_pwm` to
  taste. `min_pwm` should stay high enough that the 5V Noctuas spin up
  reliably (they don't like very low duty cycles) - 20% is a reasonable
  starting floor, raise it if fans stall or hum at idle.

Enable and start:

```
sudo systemctl daemon-reload
sudo systemctl enable --now diskfanspeedd
```

## CLI usage

```
diskfanspeed status   # mode, current fan %, max temp, per-disk temps
diskfanspeed auto     # temperature-based control (default)
diskfanspeed full     # force fans to 100%
```

## Notes

- Two of the four disks are Jellyfin media disks (only spin up during
  streaming) and two are Time Machine targets (wake every couple of
  hours). The daemon checks disk power state (`smartctl -n standby`)
  before reading SMART data, so it won't wake a sleeping disk just to
  poll its temperature - it reuses the last known temperature for that
  disk instead. If every disk is asleep, the daemon holds the last fan
  speed for `fan_curve.idle_delay_s` (default 30 minutes) before falling
  back to `fan_curve.idle_pwm`, so a disk napping briefly doesn't spin
  the fans down and back up.
- State (last known temps) persists to `state.json` under
  `/var/lib/diskfanspeedd` so a daemon restart doesn't momentarily forget
  a sleeping disk's last reading.
