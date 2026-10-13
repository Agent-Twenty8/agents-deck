// status-led: one addressable RGB LED that tells you when an agent needs you.
//
//   off             nothing is running
//   white, pulsing  agents are working
//   orange, solid   an agent is waiting for you (#FF4D00)
//   orange, blink   an agent stopped with an error
//   blue, pulsing   not approved yet: agentctl devices approve <name>
//   red flash       the deck is offline
//
// Deck Link runs on Serial; nothing else is printed there.
#include <Arduino.h>
#include <DeckLink.h>

#ifndef LED_PIN
#define LED_PIN 8 // ESP32-C3-DevKitM-1 on-board WS2812
#endif
#ifndef BRIGHTNESS
#define BRIGHTNESS 255 // scales every colour (0..255)
#endif

static DeckLink deck;
static deck_tiles_t tiles;

static void onFrame(deck_link_t *, const deck_frame_t *f)
{
	deck_tiles_handle(&tiles, f);
}

static void setLed(uint8_t r, uint8_t g, uint8_t b)
{
	static uint32_t last = 0xFFFFFFFF;
	r = (uint8_t)(r * BRIGHTNESS / 255);
	g = (uint8_t)(g * BRIGHTNESS / 255);
	b = (uint8_t)(b * BRIGHTNESS / 255);
	uint32_t rgb = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
	if (rgb == last)
		return;
	last = rgb;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
	rgbLedWrite(LED_PIN, r, g, b);
#else
	neopixelWrite(LED_PIN, r, g, b);
#endif
}

// A triangle wave 0..255 with the given period.
static uint8_t pulse(uint32_t now, uint32_t period)
{
	uint32_t t = now % period;
	uint32_t half = period / 2;
	return (uint8_t)(t < half ? t * 255 / half : (period - t) * 255 / half);
}

static void show(uint32_t now)
{
	if (deck.state() == DECK_LINK_OFFLINE) {
		bool on = now % 2000 < 80;
		setLed(on ? 60 : 0, 0, 0);
		return;
	}
	if (!deck.online())
		return setLed(0, 0, 0); // still connecting
	if (!deck.approved()) {
		uint8_t p = pulse(now, 3000);
		return setLed(0, 0, (uint8_t)(p / 4));
	}
	if (deck_tiles_count_status(&tiles, agentsdeck_link_v1_Status_STATUS_WAITING) > 0)
		return setLed(0xFF, 0x4D, 0x00);
	if (deck_tiles_count_status(&tiles, agentsdeck_link_v1_Status_STATUS_ERROR) > 0) {
		bool on = now % 400 < 200;
		return setLed(on ? 0xFF : 0, on ? 0x4D : 0, 0);
	}
	if (deck_tiles_count_status(&tiles, agentsdeck_link_v1_Status_STATUS_RUNNING) +
		deck_tiles_count_status(&tiles, agentsdeck_link_v1_Status_STATUS_STARTING) >
	    0) {
		uint8_t p = (uint8_t)(8 + pulse(now, 2400) * 3 / 4);
		return setLed(p, p, p);
	}
	setLed(0, 0, 0);
}

void setup()
{
	Serial.begin(115200);
	setLed(0, 0, 0);
	deck_tiles_init(&tiles);

	DeckLinkConfig cfg = {};
	cfg.board_id = DeckLink::chipId();
	cfg.type = "status-led";
	cfg.firmware = "0.1.0";
	cfg.fields = agentsdeck_link_v1_FieldGroup_FIELDS_TILE;
	cfg.on_frame = onFrame;
	deck.begin(Serial, cfg);
}

void loop()
{
	deck.loop();
	show(millis());
	delay(5);
}
