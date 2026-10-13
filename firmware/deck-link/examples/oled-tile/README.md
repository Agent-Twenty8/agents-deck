# oled-tile

A 128×64 OLED and one button. It lists your agents (title and status); when one is waiting for you it
shows that prompt and its options, the primary one highlighted.

- **Short press:** answer with the primary option (on an agent without a prompt: focus its terminal).
- **Long press:** go to the next agent.
- After 10 s without a press it jumps to the first agent that needs you.

It subscribes to `FIELDS_TILE | FIELDS_PROMPT`. Answering and focusing need scope **act**:

```sh
agentctl devices approve oled-tile-<id>
agentctl devices scope oled-tile-<id> act
```

Without it the screen says "needs scope act" after a press.

## Parts and wiring

- ESP32 dev board (`esp32dev`, e.g. ESP32-DevKitC)
- SSD1306 128×64 OLED, I²C, address 0x3C
- a push button (or use the board's BOOT button on GPIO0, which needs no wiring)

| OLED | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

Button: between GPIO0 and GND (internal pull-up). For another pin build with `-DBUTTON_PIN=<gpio>`.

## Build and flash

```sh
pio run -d firmware/deck-link/examples/oled-tile
pio run -d firmware/deck-link/examples/oled-tile -t upload
agentctl link add <serial port>
```
