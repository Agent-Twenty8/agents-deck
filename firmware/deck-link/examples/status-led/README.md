# status-led

One addressable RGB LED that tells you when an agent needs you.

| LED | meaning |
|---|---|
| off | nothing is running |
| white, slow pulse | agents are working |
| orange `#FF4D00`, solid | an agent is waiting for you |
| orange, quick blink | an agent stopped with an error |
| blue, slow pulse | the board isn't approved yet |
| short red flash every 2 s | the deck is offline |

It subscribes to `FIELDS_TILE` only and sends no commands, so the default `view` scope is enough.

## Parts and wiring

- ESP32-C3-DevKitM-1: the on-board WS2812 on GPIO8 is used, no wiring.
- Any other ESP32-C3 board: connect a WS2812/SK6812 LED's data pin to a GPIO, 5 V (or 3.3 V) and
  GND, and build with `-DLED_PIN=<gpio>`.

## Build and flash

```sh
pio run -d firmware/deck-link/examples/status-led                 # DevKitM-1 (USB-UART bridge)
pio run -d firmware/deck-link/examples/status-led -e esp32-c3-native-usb   # boards with native USB only
pio run -d firmware/deck-link/examples/status-led -t upload
```

Then register it with the deck:

```sh
agentctl link add <serial port>
agentctl devices approve status-led-<id>
```
