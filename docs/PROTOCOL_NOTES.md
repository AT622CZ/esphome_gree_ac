# Protocol notes from a sniffed stock WiFi module (Coolexpert ACH-09BI, Gree-based)

Captured on the UART between the stock Gree WiFi module and the indoor unit while
controlling the unit from the Gree+ app. Byte numbers below are *data* bytes, i.e.
after `7E 7E LEN CMD` and before the checksum (same numbering as `esppac_cnt.h`).

Confirmed against the component's byte map:

| field | data byte / mask | notes |
|---|---|---|
| change marker | 3 = 0xAF | first packet of a change only |
| power / mode / fan coarse | 4: bit7 power, bits 4-6 mode, bits 0-1 fan | 3-speed units: fan 0 auto, 1 low, 2 med, 3 high |
| sleep | 4 bit3 | |
| set temperature | 5 high nibble = temp - 16 | |
| turbo | 6 bit0 | echoed by the unit in reports |
| display on | 6 bit1 | |
| plasma / health | 6 bit2 | |
| Fahrenheit | 7 bit7, half degree 7 bit6 | set temp in F uses both |
| vertical swing | 8 high nibble | 1 full, 2-6 fixed, 7 lower third, 9 middle third, 0xB upper third |
| horizontal swing | 8 low nibble | 0-6 |
| display mode | 9 bits 4-5 | |
| save / 8 C heat | 11 bit6 | |
| quiet | 16 bit3 | app sends it together with fan low even on units whose remote has no Quiet |
| fan fine speed | 18 low nibble | app sends 1/2 or 3/5 for low/med/high, 3-speed units ignore it |
| room temperature (report) | 42 | Gree-based: `raw - 40` (0x36 = 14 C, 0x38 = 16 C); Sinclair: `(raw - 16) / 2` |
| I FEEL active (report) | 9 bit6 | set while I FEEL is on; cleared by I FEEL off or power off |
| I FEEL temperature (report) | 24 | whole °C measured by the remote (0x17 = 23 C); byte 42 then follows this value instead of the unit's own sensor |
| remote command received (report) | 37 bit7 | set for ~6 s after the unit accepted an IR command |

Differences to the component's handshake:

* The stock module sends a change as **three** packets with data byte 6 bit3 set
  (first one also with 0xAF), then keeps sending the steady state with bit3 cleared.
  It never uses the "no change" flag (data byte 11 bit3) the component sends.
* Some captures carry data byte 39 = 0x82 instead of 0x02 and, in a power-off packet,
  data byte 43 = 0x80. Meaning unknown; candidates for the "silent" flag on units that
  ignore data byte 40 bit0.
* Packets are spaced 300 ms apart regardless of the unit's reports.

Tested on the Coolexpert ACH-09BI (2026-09-16):

* SET packets carrying the I FEEL flag (byte 9 bit6) and a temperature (byte 24) are
  ignored, both as a plain update packet and as a 0xAF command. I FEEL cannot be driven
  over UART, only from the IR remote.
* A SET packet without the "no change" flag but without 0xAF is ignored as well; the unit
  applies packet contents only when 0xAF is present.

## I FEEL over IR

Since the unit takes I FEEL only from its IR receiver, the component can play the remote
(`ir_transmitter_id`). Frames, as used by Gree YAC/YAN remotes:

* Command: mark 9000 / space 4500, bytes 0-3 LSB first, 3 footer bits `010`, mark 620 /
  space 19980, bytes 4-7, end mark. Bit mark 620, zero space 540, one space 1600.
* Bytes: 0 = mode (bits 0-2), power (bit 3), fan 0-3 (bits 4-5), vertical swing active (bit 6),
  sleep (bit 7); 1 = set temperature - 16; 2 = turbo (bit 4), light (bit 5), health (bit 6),
  x-fan (bit 7); 3 = 0x50 plus Fahrenheit flags; 4 = vertical louver (low nibble, same codes as
  the UART report) and horizontal louver (bits 4-6); 5 = display mode (bits 0-1), **I FEEL
  (bit 2)**, constant 0x20; 7 = energy saving (bit 2), checksum (high nibble) = low nibbles of
  bytes 0-3 + high nibbles of bytes 4-6 + 0x0A.
* I FEEL temperature frame: mark 8200 / space 3800, temperature in whole C, 0xA5, end mark 650.
* Old remotes (Coolexpert ACH-09FC, 2011) send only bytes 0-3 and the footer, no second block
  and no checksum; that unit has no I FEEL.

Observed on the Coolexpert ACH-09BI: temperature frames alone are ignored while I FEEL is
inactive (report byte 9 bit6 = 0); the command with the I FEEL bit has to come first. The
position of the I FEEL bit (byte 5 bit 2) follows the IRremoteESP8266 documentation of the
protocol; HeatpumpIR puts it at byte 4 bit 3. To be confirmed against a captured remote frame.
