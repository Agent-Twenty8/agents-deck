#if defined(ARDUINO)

#include "DeckLink.h"

#include <stdio.h>

void DeckLink::begin(Stream &port, const DeckLinkConfig &cfg)
{
	port_ = &port;
	user_ = cfg.user;
	DeckLinkConfig c = cfg;
	c.write = writeCb;
	c.millis = millisCb;
	c.user = this;
	deck_link_init(&ctx_, &c);
	deck_link_begin(&ctx_);
}

void DeckLink::loop()
{
	if (!port_)
		return;
	uint8_t buf[128];
	// Bounded, so a flood of input can't starve the rest of loop().
	for (int rounds = 0; rounds < 32; rounds++) {
		int avail = port_->available();
		if (avail <= 0)
			break;
		size_t want = (size_t)avail < sizeof buf ? (size_t)avail : sizeof buf;
		size_t n = port_->readBytes(buf, want);
		if (n == 0)
			break;
		deck_link_feed(&ctx_, buf, n);
	}
	deck_link_poll(&ctx_);
}

size_t DeckLink::writeCb(const uint8_t *data, size_t len, void *self)
{
	return static_cast<DeckLink *>(self)->port_->write(data, len);
}

uint32_t DeckLink::millisCb(void *)
{
	return millis();
}

const char *DeckLink::chipId()
{
	static char id[18];
#if defined(ARDUINO_ARCH_ESP32)
	uint64_t mac = ESP.getEfuseMac(); // bytes in transmission order, lowest first
	snprintf(id, sizeof id, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(mac & 0xFF), (unsigned)((mac >> 8) & 0xFF),
		 (unsigned)((mac >> 16) & 0xFF), (unsigned)((mac >> 24) & 0xFF), (unsigned)((mac >> 32) & 0xFF),
		 (unsigned)((mac >> 40) & 0xFF));
#else
	snprintf(id, sizeof id, "unknown");
#endif
	return id;
}

#endif // ARDUINO
