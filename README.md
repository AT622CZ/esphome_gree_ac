# ESPHOME component to support Gree/Sinclair AC units
This repository adds support for ESP32-based WiFi modules to interface with Gree/Sinclair AC units.
This generally replaces stock WiFi module, sometimes giving a little more advanced features for swing control than stock application or remote.

**USE AT YOUR OWN RISK!**

Work is still in progress!

Tested with Sinclair AC (MV-H09BIF), AYRTON AYL-12BIR and Coolexpert ACH-09BI.

Communication protocol is based on my own reverse-engineering.

ESPHome interface/binding based on:
* https://github.com/DomiStyle/esphome-panasonic-ac

**USAGE**
* Use at your own risk!
* See: https://github.com/piotrva/esphome_gree_ac/tree/main/examples
* Create configuration file: `ac-sinclair-main.yaml`
* Configure youe ESP `board`, `uart`, optionally `status_led`, check `wifi` settings (secrets)
* Create configuration(s) for your device(s): `ac-living-room.yaml`, `ac-bedroom.yaml`
* Configure deviceid and devicename, use proper `api` and `ota` keys
* Upload initial configuration to your ESP board using USB connection
* Disconnect completely power from your AC system, follow all safety procedures, desolder original WiFI unit
* Prepare a DIY adapter to connect ESP board to the AC unit, see table and representative schematic below
* Reconnect power to your AC system.
* Enjoy!

Generally stock WiFi module outputs UART with 3.3V signal levels and the AC unit outputs UART with 5V signal levels therefore a simple voltage divider on UART from AC unit towards ESP is usually suitable, considering very slow baudrate.
On some stock WiFi PCBs AC unit connector pins are marked on silkscreen.

| AC unit pin | Function | ESP connection        |
| ----------- | -------- | --------------------- |
| 1           | +5V      | VIN / 5V              |
| 2           | RX       | UART TX               |
| 3           | TX       | UART RX (via divider) |
| 4           | GND      | GND                   |

![Connection schematic](./images/schematic.png)

**UNIT CAPABILITIES**
* `fan_speeds: 5` (default, Sinclair MV-H09BIF: Low/Medium-Low/Medium/Medium-High/High) or `fan_speeds: 3` (most Gree-based units with Low/Medium/High, e.g. Coolexpert ACH-09BI). With 3 speeds the fan speed is carried only in the mode byte and the fine speed field is not sent.
* `quiet_mode: false` / `turbo_mode: false` hide the Quiet / Turbo fan modes (separate flags in the protocol, independent of the speed) if the unit does not have them.
* `current_temperature_formula: gree` if the room temperature shown in HA is off. Sinclair MV-H09BIF encodes it as `(raw - 16) / 2` (default `sinclair`), Gree-based units such as Coolexpert ACH-09BI as `raw - 40` (`gree`). Compare HA with a thermometer to pick the right one; both formulas agree at 24 °C.
* `beeper_byte` / `beeper_mask` (experimental, default 40 / 0x01): where the "execute silently" flag is placed in the SET packet. Units that keep beeping with `beeper_switch` off ignore the default flag; these let you try other bits without touching the code.
* `display_modes: [off, set_temperature, actual_temperature]` limits the options of `display_select` (default: all five - `off`, `auto`, `set_temperature`, `actual_temperature`, `outside_temperature`). `auto` is the unit's "no indication" state which shows the set temperature; a state reported by the unit that is not in the list is mapped to the closest one offered (`auto` -> `set_temperature`). Useful for units that cannot show the outside temperature.
* `horizontal_swing: false` for units without motorized horizontal louvers - the climate entity then offers only Off/Vertical swing and `horizontal_swing_select` is not allowed.

**OPTIONAL ENTITIES**
* `horizontal_swing_select`, `vertical_swing_select` - detailed louver positions
* `display_select`, `display_unit_select` - unit display mode and C/F
* `plasma_switch`, `sleep_switch`, `xfan_switch`, `save_switch` - unit features
* `beeper_switch` - ON (default) = unit beeps on every command from HA, OFF = commands are executed silently (IR remote still beeps). The unit does not report this flag, the switch keeps its own state (`restore_mode`, default `RESTORE_DEFAULT_ON`).
* `current_temperature_sensor` - id of an external sensor to use as current temperature instead of the unit's own reading

**NOTES**
* Packets are sent on a fixed 300 ms timer like the stock WiFi module. Sending right after each unit report (previous behaviour) made some units ignore commands, see [#2](https://github.com/piotrva/esphome_gree_ac/issues/2) and [#25](https://github.com/piotrva/esphome_gree_ac/issues/25).
* Climate state is published to HA only when it changes.
* It was reported [#1](https://github.com/piotrva/esphome_gree_ac/issues/1) that with some changes the code works with Lennox li024ci AC

**TODO**
* Support Timers - maybe unnecessray as timers can be managed by Home Assistant
* Support Time sync - maybe unnecessray as timers can be managed by Home Assistant
