// t4s3-panel: the agents deck on a LilyGo T4-S3 (2.41" AMOLED, touch), over
// Deck Link on its USB port. Two FreeRTOS tasks: Deck Link (link.c) and the
// UI (LVGL, here and in ui.c); they share the agents state (agents.c).
#include "agents.h"
#include "debug.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "lvgl.h"
#include "touch.h"
#include "ui.h"

#define BUF_LINES 60 // per draw buffer: 450 x 60 pixels, twice, in internal RAM
#define BRIGHTNESS 200

static const char *TAG = "panel";
static bool touch_ok;

static uint32_t tick(void) { return now_ms(); }

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
	lv_draw_sw_rgb565_swap(px, lv_area_get_size(area)); // the panel wants big-endian
	display_draw(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)px);
	lv_display_flush_ready(disp);
}

// The panel takes windows that start on an even pixel and are an even
// number of pixels wide and high.
static void round_area(lv_event_t *e)
{
	lv_area_t *a = lv_event_get_param(e);
	a->x1 &= ~1;
	a->y1 &= ~1;
	a->x2 |= 1;
	a->y2 |= 1;
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
	(void)indev;
	int x, y;
	bool down = touch_ok && touch_read(&x, &y);
#ifdef PANEL_DEBUG
	down = down || debug_touch(&x, &y);
#endif
	if (down) {
		data->point.x = x;
		data->point.y = y;
		data->state = LV_INDEV_STATE_PRESSED;
	} else {
		data->state = LV_INDEV_STATE_RELEASED;
	}
}

static void ui_task(void *arg)
{
	(void)arg;
	bool lit = false;
	for (;;) {
		ui_update();
		uint32_t wait = lv_timer_handler();
#ifdef PANEL_DEBUG
		debug_poll();
#endif
		if (!lit) { // light the panel once the first frame is on it
			display_brightness(BRIGHTNESS);
			lit = true;
		}
		vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 5 : wait > 30 ? 30 : wait));
	}
}

void app_main(void)
{
	agents_init();
	display_init();
	touch_ok = touch_init();

	lv_init();
	lv_tick_set_cb(tick);
	lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
	size_t size = DISPLAY_W * BUF_LINES * 2;
	void *b1 = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	void *b2 = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	lv_display_set_buffers(disp, b1, b2, size, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(disp, flush);
	lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
	lv_indev_t *indev = lv_indev_create();
	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, read_touch);

	ui_init();
	link_start();
	xTaskCreatePinnedToCore(ui_task, "ui", 16384, NULL, 4, NULL, 1);
	ESP_LOGI(TAG, "running (touch %s)", touch_ok ? "ok" : "missing");
}
