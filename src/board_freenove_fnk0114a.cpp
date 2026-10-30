/**
 * Board implementation: Freenove FNK0114A_2P4.
 *
 * Hardware values in this file and platformio.ini come from Freenove's
 * FNK0114A TFT_eSPI setup, 2.4" touch examples, and board schematic.  The
 * ST7789 uses HSPI.  The XPT2046-compatible touch controller is intentionally
 * bit-banged on its own four GPIOs, matching Freenove's TFT_Touch example;
 * it must not claim the display's HSPI peripheral.
 */

#if defined(BOARD_FREENOVE_FNK0114A)

#include "board.h"
#include "config.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft = TFT_eSPI();
static uint8_t touch_orientation = ORIENTATION_LANDSCAPE_RIGHT;

static const uint32_t LV_BUF_PX = DISPLAY_SHORT_SIDE * 10;
static lv_color_t lv_buf1[LV_BUF_PX];
static lv_color_t lv_buf2[LV_BUF_PX];

// Freenove's 2.4"/2.8" drawing example calibrates points 30 pixels in from
// each landscape edge, then maps them back to the same inset coordinates.
static const int32_t TOUCH_CAL_X_MIN = 527;
static const int32_t TOUCH_CAL_X_MAX = 3552;
static const int32_t TOUCH_CAL_Y_MIN = 683;
static const int32_t TOUCH_CAL_Y_MAX = 3464;
static const int32_t TOUCH_CAL_INSET = 30;
static const int32_t TOUCH_STABILITY_RAW = 10;

static void touch_clock_bit(bool output_bit)
{
    digitalWrite(PIN_TOUCH_MOSI, output_bit ? HIGH : LOW);
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    delayMicroseconds(1);
}

static void touch_write_command(uint8_t command)
{
    for (int bit = 7; bit >= 0; --bit) {
        touch_clock_bit((command >> bit) & 1u);
    }
}

static uint16_t touch_read_raw(uint8_t command)
{
    digitalWrite(PIN_TOUCH_CS, LOW);
    touch_write_command(command);

    // XPT2046 returns one null bit, 12 data bits, then three trailing bits.
    uint16_t value = 0;
    for (int bit = 0; bit < 16; ++bit) {
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        value = (uint16_t)((value << 1) | (digitalRead(PIN_TOUCH_MISO) ? 1u : 0u));
        digitalWrite(PIN_TOUCH_CLK, LOW);
        delayMicroseconds(1);
    }

    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    return value >> 3;
}

static bool touch_sample(uint16_t *raw_x, uint16_t *raw_y)
{
    // These commands and axis ordering match Freenove's TFT_Touch library:
    // 0x90 is the calibrated landscape X axis and 0xD0 is landscape Y.
    const uint16_t x1 = touch_read_raw(0x90);
    const uint16_t y1 = touch_read_raw(0xD0);
    delay(1);
    const uint16_t x2 = touch_read_raw(0x90);
    const uint16_t y2 = touch_read_raw(0xD0);
    delay(1);
    const uint16_t x3 = touch_read_raw(0x90);
    const uint16_t y3 = touch_read_raw(0xD0);

    if (x1 == 0 || x1 >= 4095 || y1 == 0 || y1 >= 4095
        || x2 == 0 || x2 >= 4095 || y2 == 0 || y2 >= 4095
        || x3 == 0 || x3 >= 4095 || y3 == 0 || y3 >= 4095) return false;
    if (abs((int)x1 - (int)x2) > TOUCH_STABILITY_RAW) return false;
    if (abs((int)x1 - (int)x3) > TOUCH_STABILITY_RAW) return false;
    if (abs((int)y1 - (int)y2) > TOUCH_STABILITY_RAW) return false;
    if (abs((int)y1 - (int)y3) > TOUCH_STABILITY_RAW) return false;

    // Freenove's TFT_Touch::Pressed() also requires three stable samples.
    *raw_x = (uint16_t)(((uint32_t)x1 + x2 + x3) / 3u);
    *raw_y = (uint16_t)(((uint32_t)y1 + y2 + y3) / 3u);
    return true;
}

static uint16_t calibrated_axis(uint16_t raw, int32_t raw_min, int32_t raw_max,
                                uint16_t size)
{
    const int32_t pixel_min = TOUCH_CAL_INSET;
    const int32_t pixel_max = (int32_t)size - TOUCH_CAL_INSET;
    int32_t pixel = pixel_min
                  + ((int32_t)raw - raw_min) * (pixel_max - pixel_min)
                    / (raw_max - raw_min);
    if (pixel < 0) pixel = 0;
    if (pixel >= size) pixel = size - 1;
    return (uint16_t)pixel;
}

static void touch_init()
{
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_CLK, OUTPUT);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    // PENIRQ is wired to GPIO36 on the Freenove board.  The official examples
    // poll the controller, so the firmware records the pin but does not depend
    // on the interrupt line for navigation.
    pinMode(PIN_TOUCH_IRQ, INPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    Serial.printf("[Touch] XPT2046 software SPI: cs=%d clk=%d mosi=%d miso=%d irq=%d\n",
                  PIN_TOUCH_CS, PIN_TOUCH_CLK, PIN_TOUCH_MOSI,
                  PIN_TOUCH_MISO, PIN_TOUCH_IRQ);
}

void board_display_init()
{
    tft.init();
    // Freenove's board-specific TFT_eSPI setup explicitly enables inversion.
    tft.invertDisplay(true);
    touch_init();
}

const char* board_display_id()
{
    return DISPLAY_ID;
}

const char* board_panel_id()
{
    return "st7789";
}

const char* board_panel_raw()
{
    // No register read is performed on this write-only panel connection.
    return "";
}

void board_set_rotation(uint8_t orientation)
{
    switch (orientation) {
        case ORIENTATION_LANDSCAPE_LEFT:
            tft.setRotation(3);
            SCREEN_WIDTH = DISPLAY_LONG_SIDE;
            SCREEN_HEIGHT = DISPLAY_SHORT_SIDE;
            break;
        case ORIENTATION_LANDSCAPE_RIGHT:
            // Freenove's documented landscape orientation.
            tft.setRotation(1);
            SCREEN_WIDTH = DISPLAY_LONG_SIDE;
            SCREEN_HEIGHT = DISPLAY_SHORT_SIDE;
            break;
        case ORIENTATION_PORTRAIT:
        default:
            tft.setRotation(0);
            SCREEN_WIDTH = DISPLAY_SHORT_SIDE;
            SCREEN_HEIGHT = DISPLAY_LONG_SIDE;
            break;
    }
    touch_orientation = orientation;
}

lv_display_rotation_t board_lvgl_rotation()
{
    return LV_DISPLAY_ROTATION_0;
}

void board_fill_black()
{
    tft.fillScreen(TFT_BLACK);
}

void board_flush(const lv_area_t *area, uint8_t *px_map)
{
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, width, height);
    tft.pushColors((uint16_t *)px_map, width * height, true);
    tft.endWrite();
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    uint16_t raw_x = 0, raw_y = 0;
    if (!touch_sample(&raw_x, &raw_y)) return false;

    // Base coordinates are Freenove rotation 1 (320x240 landscape).
    const uint16_t landscape_x = calibrated_axis(
        raw_x, TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, DISPLAY_LONG_SIDE);
    const uint16_t landscape_y = calibrated_axis(
        raw_y, TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX, DISPLAY_SHORT_SIDE);

    switch (touch_orientation) {
        case ORIENTATION_LANDSCAPE_LEFT:
            *x = DISPLAY_LONG_SIDE - 1 - landscape_x;
            *y = DISPLAY_SHORT_SIDE - 1 - landscape_y;
            break;
        case ORIENTATION_LANDSCAPE_RIGHT:
            *x = landscape_x;
            *y = landscape_y;
            break;
        case ORIENTATION_PORTRAIT:
        default:
            // Same transform as Freenove TFT_Touch setRotation(0), expressed
            // from its documented rotation-1 landscape coordinates.
            *x = DISPLAY_SHORT_SIDE - 1 - landscape_y;
            *y = landscape_x;
            break;
    }
    return true;
}

void board_lvgl_buffers(void **buf1, void **buf2, uint32_t *size_bytes)
{
    *buf1 = lv_buf1;
    *buf2 = lv_buf2;
    *size_bytes = sizeof(lv_buf1);
}

void board_backlight_init()
{
    ledcSetup(BACKLIGHT_LEDC_CHANNEL, BACKLIGHT_LEDC_FREQ_HZ,
              BACKLIGHT_LEDC_RES_BITS);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_LEDC_CHANNEL);
    ledcWrite(BACKLIGHT_LEDC_CHANNEL, 255); // GPIO21 is active-high.
    Serial.printf("[BL] Freenove active-high PWM: pin=%d ch=%d\n",
                  PIN_TFT_BL, BACKLIGHT_LEDC_CHANNEL);
}

void board_backlight_set_percent(uint8_t pct)
{
    if (pct < BRIGHTNESS_MIN_PERCENT) pct = BRIGHTNESS_MIN_PERCENT;
    if (pct > BRIGHTNESS_MAX_PERCENT) pct = BRIGHTNESS_MAX_PERCENT;
    ledcWrite(BACKLIGHT_LEDC_CHANNEL, (uint32_t)pct * 255u / 100u);
}

bool board_persists_config()
{
    return true;
}

uint32_t board_frame_glitches()
{
    return 0;
}

#endif // BOARD_FREENOVE_FNK0114A
