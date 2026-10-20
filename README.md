# bat_mon_w5500
PIC18F47K42 battery monitor with W5500 Ethernet, embedded C and a web interface.
# PIC18F47K42 Battery Monitor + W5500 Web Dashboard

Firmware for a 12 V lead-acid battery monitor and charger relay controller, running on a PIC18F47K42 (Curiosity Nano). It measures the battery voltage, switches a charger relay with fail-safe logic, shows status on two LEDs, and serves a live dashboard over Ethernet using a W5500 module.

Open `http://10.0.0.60/` in a browser to see the dashboard.

## Features

- Battery voltage measured on RA0 through a resistor divider, 64-sample ADC average plus an IIR filter and a deadband to keep the reading steady
- Charger relay on RB0 with fail-safe control (relay stays off unless every safety condition is met)
- State of charge estimate from voltage, using separate lookup tables for resting and on-charger readings, rounded to 5 %
- Voltage trend detection (rising / steady / falling) over 20 s windows
- "Full" detection and "no charger" detection (relay on but battery not charging)
- Overvoltage latch, no-sense fault, and 24-hour maximum charge time latch
- Two status LEDs (RA1 activity, RA2 full) with different flash patterns for each condition
- W5500 web server on port 80 with an auto-refreshing dashboard:
  - SOC ring gauge, large voltage reading, trend arrow, status badge
  - Status, relay, full and safety cards
  - "Virtual LEDs" that flash like the real RA1/RA2 LEDs
  - 1-hour voltage chart (120 samples, one every 30 s) drawn as inline SVG
- Page is streamed into the W5500 TX buffer in pieces, so no large RAM buffer is needed (700-byte scratch buffer only)
- Robust W5500 start-up: hard reset with `VERSIONR` check and retry, re-initialisation from the main loop if the chip was not found, and re-init if the chip loses its network settings
- LED lamp test at power-up and an LED polarity switch
- Optional serial status line for `battery_plot.py`

## Hardware

| Part | Notes |
|------|-------|
| MCU | PIC18F47K42 Curiosity Nano |
| Ethernet | W5500 mini module, SPI |
| Battery sense | RA0 (ANA0) through divider: 19.95 kΩ top, 5.62 kΩ bottom |
| Charger relay | RB0 (high = relay on) |
| LEDs | RA1 (activity), RA2 (full), each through a resistor |
| Serial | UART2 diagnostics |

### W5500 wiring

| Signal | Pin | Wire colour |
|--------|-----|-------------|
| CS | RD4 | Purple |
| SCK | RC6 | Yellow |
| MOSI | RC4 | Orange |
| MISO | RC5 | White |
| RESET | RD5 | Brown |
| 3.3 V | 3.3 V | Red |
| GND | GND | Green |
| INT | not connected | |

## Network settings

Set in the source (`cfg_mac`, `cfg_ip`, `cfg_sub`, `cfg_gw`):

| Setting | Value |
|---------|-------|
| IP address | 10.0.0.60 |
| Subnet mask | 255.255.255.0 |
| Gateway | 10.0.0.2 |
| MAC | 02:00:00:00:00:01 |
| HTTP port | 80 |

W5500 socket 0 is configured with 2 KB RX and 16 KB TX. Sockets 1 to 7 are set to 0 KB.

## Build

- MPLAB X IDE with the XC8 compiler (developed with XC8 3.10)
- MPLAB Code Configurator (MCC) generated drivers: `system`, `adc`, `uart/uart2`, and SPI1 (used by the W5500 code)
- Target: PIC18F47K42

Generate the MCC files first, then add `main.c` to the project.

## Options

At the top of `main.c`:

| Define | Default | Meaning |
|--------|---------|---------|
| `LED_ACTIVE_HIGH` | 1 | 1 = LED lights when the pin is high (pin → resistor → LED → GND). 0 = lights when the pin is low (3.3 V → resistor → LED → pin) |
| `SERIAL_STATUS_LINE` | 0 | 1 = print a status line every loop (needed by `battery_plot.py`) |

Calibration: `VREF_MV`, `R_TOP_OHM`, `R_BOT_OHM`, `CAL_GAIN_PERMILLE` and `CAL_OFFSET_MV` set how the ADC reading converts to battery volts. Adjust these against a meter.

## How it works

The main loop runs at about 10 Hz (100 ms delay per pass).

1. **Measure**: 64 ADC samples are averaged, filtered, and passed through a deadband, then scaled to battery millivolts.
2. **Safety latches**: set the fault state and force the relay off.
3. **Relay decision**: the relay is allowed on only when the sense voltage is valid and no latch is set. After start-up and after any fault there is a hold-off before it can switch on again.
4. **Trend and full detection**: voltage is compared every 20 s. The battery is "full" when the relay is on, voltage is at or above the full threshold, and the trend is not rising for two windows in a row.
5. **No charger detection**: relay on but voltage is stuck low and not rising, or falling for two windows in a row.
6. **SOC and status text** are calculated and published to the web page.
7. **Ethernet** is serviced every pass. The page is built only when a browser makes a request.

### Thresholds

| Item | Value |
|------|-------|
| Deep discharge below | 10.5 V |
| Low below | 11.8 V |
| Full at or above | 13.6 V (clears below 13.4 V) |
| Overvoltage latch | above 14.2 V for 3 s, or above 14.9 V immediately |
| No sense (fault) | below 6.0 V |
| No-charger voltage limit | 13.0 V |
| Start-up delay | 10 s |
| Relay minimum off time | 30 s |
| Maximum charge time | 24 h without reaching full |

### LED behaviour

| Condition | RA1 | RA2 |
|-----------|-----|-----|
| Voltage rising | fast flash | |
| Voltage falling | slow flash | |
| Voltage steady | off | |
| Fully charged | | on |
| No charger | slow flash, alternating with RA2 | slow flash, alternating with RA1 |
| Fault | fast flash, together with RA2 | fast flash, together with RA1 |

At power-up there is a lamp test: RA1, then RA2, then both, then off.

## Serial output

UART2 prints boot and diagnostic messages, for example:

```
LED test: RA1, RA2, both
W5500 ver=0x04 ok=1 cfg=1
Battery monitor boot
HTTP rx=... len=... txfree=... sent=1 stage=7
```

`ver=0x04` means the W5500 was found. The `stage` value in the HTTP line shows where a page send ended:

| Stage | Meaning |
|-------|---------|
| 1 | Waiting for enough free TX buffer |
| 2 | TX buffer too small (16 KB setting did not apply) |
| 3 | Building page and issuing SEND |
| 4 | SEND command did not clear |
| 5 | Waiting for SEND_OK or TIMEOUT |
| 6 | W5500 reported a transmit timeout |
| 7 | Success |
| 8 | No SEND_OK or TIMEOUT before software timeout |
| 9 | Page larger than free TX space |

With `SERIAL_STATUS_LINE` set to 1, each loop also prints a line with ADC value, pin voltage, battery voltage, SOC, state, relay, charger status and trend.

## Troubleshooting

- **"W5500 not found"**: check wiring and the 3.3 V supply. The firmware retries about every 3 s.
- **Page does not load**: check the PC is on the 10.0.0.x subnet and the link LED on the module is on.
- **Voltage reading is off**: check divider resistor values and `VREF_MV`, then trim with `CAL_GAIN_PERMILLE` / `CAL_OFFSET_MV`.
- **LEDs inverted**: change `LED_ACTIVE_HIGH`.

## Safety

This controls a mains-powered or high-current charger. The thresholds are set for a 12 V lead-acid battery and the relay logic is fail-safe, but verify the behaviour on your own hardware, use proper fusing, and do not leave it unattended until you have tested every fault condition.

## Licence

Generated driver files are © Microchip Technology Inc. and its subsidiaries and are subject to Microchip's licence terms. Add your own licence for the application code here.
