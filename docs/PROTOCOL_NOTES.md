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

Differences to the component's handshake:

* The stock module sends a change as **three** packets with data byte 6 bit3 set
  (first one also with 0xAF), then keeps sending the steady state with bit3 cleared.
  It never uses the "no change" flag (data byte 11 bit3) the component sends.
* Some captures carry data byte 39 = 0x82 instead of 0x02 and, in a power-off packet,
  data byte 43 = 0x80. Meaning unknown; candidates for the "silent" flag on units that
  ignore data byte 40 bit0.
* Packets are spaced 300 ms apart regardless of the unit's reports.
