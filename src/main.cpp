#define LGFX_USE_V1
#include "Arduino.h"
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <string.h>
#include <Preferences.h>
#include "main.h"

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_CST816S _touch_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = SCLK;
      cfg.pin_mosi = MOSI;
      cfg.pin_miso = MISO;
      cfg.pin_dc = DC;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();

      cfg.pin_cs = CS;
      cfg.pin_rst = RST;
      cfg.pin_busy = -1;

      cfg.memory_width = WIDTH;
      cfg.memory_height = HEIGHT;
      cfg.panel_width = WIDTH;
      cfg.panel_height = HEIGHT;
      cfg.offset_x = OFFSET_X;
      cfg.offset_y = OFFSET_Y;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = RGB_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();

      cfg.pin_bl = BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 1;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {
      auto cfg = _touch_instance.config();

      cfg.x_min = 0;
      cfg.x_max = WIDTH;
      cfg.y_min = 0;
      cfg.y_max = HEIGHT;
      cfg.pin_int = TP_INT;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x15;
      cfg.pin_sda = I2C_SDA;
      cfg.pin_scl = I2C_SCL;
      cfg.freq = 400000;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};
LGFX tft;
Preferences prefs;

static const uint32_t screenWidth = WIDTH;
static const uint32_t screenHeight = HEIGHT;
lv_color_t black = lv_color_make(0, 0, 0);

const unsigned int lvBufferSize = screenWidth * 10;
uint8_t lvBuffer[2][lvBufferSize];

// accessory for dimming the screen after 10s of inactivity
const uint32_t SCREEN_DIM_TIMEOUT = 10000;
const uint8_t DIM_BRIGHTNESS = 10;
const uint8_t DEFAULT_BRIGHTNESS = 100;
uint32_t lastActivityTime = 0;
bool isScreenDimmed = false;   

void screenBrightness(uint8_t value)
{
  tft.setBrightness(value);
}
void checkScreenDimming() {
  if (!isScreenDimmed && (millis() - lastActivityTime > SCREEN_DIM_TIMEOUT)) {
    // Time to dim the screen
    screenBrightness(DIM_BRIGHTNESS);
    isScreenDimmed = true;
  }
}
void resetScreenBrightness() {
  if (isScreenDimmed) {
    screenBrightness(DEFAULT_BRIGHTNESS);
    isScreenDimmed = false;
  }
  lastActivityTime = millis();
}

// accessory for touch sensing
void my_disp_flush(lv_display_t *display, const lv_area_t *area, unsigned char *data)
{
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(data, w * h);

  if (tft.getStartCount() == 0)
  {
    tft.endWrite();
  }

  tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (uint16_t *)data);
  lv_disp_flush_ready(display);
}
void my_touchpad_read(lv_indev_t *indev_driver, lv_indev_data_t *data)
{
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);

  if (!touched)
  {
    data->state = LV_INDEV_STATE_REL;
  }
  else
  {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
    resetScreenBrightness();
  }
}

static lv_style_t style_unchecked;
static lv_style_t style_checked;

int glow_red;
int glow_green;
int glow_blue;
int power_state;

lv_color_t color_gray = lv_color_make(60, 60, 60);
const int picker_btn_size = 50;

// Screens
lv_obj_t *home_screen;
lv_obj_t *picker_screen;

// Objects
lv_obj_t *power_arc;
lv_obj_t *power_btn;
lv_obj_t *back_btn;

// move from home screen to color picker screen
void arc_click_event(lv_event_t *e) {
  lv_scr_load_anim(picker_screen, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 400, 0, false);
}

// move from color picker back to home_screen
void back_click_event(lv_event_t *e) {
  lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 400, 0, false);
}

// toggling the power button
void power_click_event(lv_event_t *e) {
  if (lv_obj_get_state(power_btn) & LV_STATE_CHECKED) {
    lv_obj_set_style_arc_color(power_arc, lv_color_make(glow_red, glow_green, glow_blue), LV_PART_MAIN);
    lv_obj_set_style_arc_color(power_arc, lv_color_make(glow_red, glow_green, glow_blue), LV_PART_INDICATOR);

    prefs.putInt("powerState", 1);
  } else {
    lv_obj_set_style_arc_color(power_arc, lv_color_make(40, 40, 40), LV_PART_MAIN); // Set to gray
    lv_obj_set_style_arc_color(power_arc, lv_color_make(40, 40, 40), LV_PART_INDICATOR);

    prefs.putInt("powerState", 0);
  }
}

// Color data structure
typedef struct {
  int red;
  int green;
  int blue;
} ColorData;

// click color buttons
void color_click_event(lv_event_t *e) {
  lv_obj_t *color_btn = (lv_obj_t *) lv_event_get_target(e);
  ColorData *data = (ColorData *)lv_event_get_user_data(e);
  int red = data->red;
  int green = data->green;
  int blue = data->blue;

  glow_red = red;
  glow_green = green;
  glow_blue = blue;

  prefs.putInt("glowRed", red);
  prefs.putInt("glowGreen", green);
  prefs.putInt("glowBlue", blue);

  if (prefs.getInt("powerState") == 1) {
    lv_obj_set_style_arc_color(power_arc, lv_color_make(red, green, blue), LV_PART_MAIN); // Change color to active
    lv_obj_set_style_arc_color(power_arc, lv_color_make(red, green, blue), LV_PART_INDICATOR); // Change color to active
  };
  lv_obj_set_style_text_color(back_btn, lv_color_make(glow_red, glow_green, glow_blue), LV_PART_MAIN); // match the back button color
}

// Elements for Home Screen
void make_arc() {
  lv_color_t stored_color = lv_color_make(glow_red, glow_green, glow_blue);

  // Create the arc
  power_arc = lv_arc_create(home_screen);
  lv_obj_set_size(power_arc, 200, 200);
  lv_obj_set_style_arc_width(power_arc, 20, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(power_arc, 20, LV_PART_MAIN);
  lv_arc_set_bg_angles(power_arc, 120, 60);
  lv_obj_remove_style(power_arc, NULL, LV_PART_KNOB); 
  lv_obj_set_style_arc_color(power_arc, prefs.getInt("powerState") == 1 ? stored_color : color_gray, LV_PART_MAIN);
  lv_obj_set_style_arc_color(power_arc, prefs.getInt("powerState") == 1 ? stored_color : color_gray, LV_PART_INDICATOR);
  lv_arc_set_range(power_arc, 0, 100);
  lv_arc_set_value(power_arc, 100);
  lv_obj_align(power_arc, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(power_arc, arc_click_event, LV_EVENT_CLICKED, NULL);
}

void make_power_btn() {
  // Create the power button
  power_btn = lv_btn_create(home_screen);
  lv_obj_align(power_btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(power_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(power_btn, 120, 120);
  lv_obj_set_style_radius(power_btn, 60, 0);
  lv_obj_set_style_border_width(power_btn, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(power_btn, 0, LV_PART_MAIN);
  
  static lv_style_t style_large_text;
  lv_style_init(&style_large_text);
  lv_style_set_text_font(&style_large_text, &lv_font_montserrat_40);

  // Create and set the label
  lv_obj_t * label = lv_label_create(power_btn);
  lv_label_set_text(label, LV_SYMBOL_POWER);
  lv_obj_center(label);

  lv_obj_add_style(label, &style_large_text, 0);

  // Initialize the styles for unchecked and checked states
  static lv_style_t style_unchecked;
  lv_style_init(&style_unchecked);
  lv_style_set_bg_color(&style_unchecked, lv_color_make(0, 0, 0)); // Black for OFF
  lv_style_set_text_color(&style_unchecked, lv_color_make(40, 40, 40)); // White text for OFF
  
  static lv_style_t style_checked;
  lv_style_init(&style_checked);
  lv_style_set_bg_color(&style_checked, lv_color_make(0, 0, 0)); // Black for ON
  lv_style_set_text_color(&style_checked, lv_color_white()); // White text for ON

  // Apply the styles to the button
  lv_obj_add_style(power_btn, &style_unchecked, 0); // Default to unchecked style
  lv_obj_add_style(power_btn, &style_checked, LV_STATE_CHECKED); // Apply checked style for checked state

  lv_obj_add_event_cb(power_btn, power_click_event, LV_EVENT_CLICKED, NULL); // Add event callback for button click

  lv_obj_add_state(power_btn, prefs.getInt("powerState") == 1 ? LV_STATE_CHECKED : LV_STATE_DEFAULT); // Set the initial state to checked
}

// Elements for Color Picker Screen
void make_color_btn(int left, int top, int red, int green, int blue) {
  lv_obj_t *color_btn = lv_btn_create(picker_screen);
  lv_obj_align(color_btn, LV_ALIGN_CENTER, left, top);
  lv_obj_set_size(color_btn, picker_btn_size, picker_btn_size);
  lv_obj_set_style_radius(color_btn, picker_btn_size / 2, 0);
  lv_obj_set_style_bg_color(color_btn, lv_color_make(red, green, blue), LV_PART_MAIN);
  lv_obj_set_style_border_width(color_btn, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(color_btn, 0, LV_PART_MAIN);

  ColorData *data = (ColorData *) malloc(sizeof(ColorData));
  data->red = red;
  data->green = green;
  data->blue = blue;

  lv_obj_add_event_cb(color_btn, color_click_event, LV_EVENT_CLICKED, data);
}

void make_color_btns() {
  make_color_btn(-57, 57, 255, 0, 0); // red
  make_color_btn(-80, 0, 255, 0, 255); // pink
  make_color_btn(-57, -57, 140, 0, 255); // purple
  make_color_btn(0, -80, 0, 255, 255); // cyan
  make_color_btn(57, -57, 0, 255, 0); // green
  make_color_btn(80, 0, 255, 255, 0); // yellow
  make_color_btn(57, 57, 255, 255, 255); // white
}

void make_back_btn() {
  back_btn = lv_btn_create(picker_screen);
  lv_obj_align(back_btn, LV_ALIGN_CENTER, 0, 80);
  lv_obj_set_size(back_btn, picker_btn_size, picker_btn_size);
  lv_obj_set_style_radius(back_btn, picker_btn_size / 2, 0);
  lv_obj_set_style_bg_color(back_btn, lv_color_make(0, 0, 0), LV_PART_MAIN);
  lv_obj_set_style_text_color(back_btn, lv_color_make(glow_red, glow_green, glow_blue), LV_PART_MAIN);
  lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);

  static lv_style_t style_back_text;
  lv_style_init(&style_back_text);
  lv_style_set_text_font(&style_back_text, &lv_font_montserrat_40);

  // Create and set the label
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_DOWN);
  lv_obj_center(back_label);
  lv_obj_add_style(back_label, &style_back_text, 0);

  lv_obj_add_event_cb(back_btn, back_click_event, LV_EVENT_CLICKED, NULL);
}

// Make Screens
void make_home_screen() {
  home_screen = lv_obj_create(NULL);  
  lv_obj_set_style_bg_color(home_screen, black, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(home_screen, LV_OPA_COVER, LV_PART_MAIN);

  make_arc();
  make_power_btn();
}

void make_picker_screen() {
  picker_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(picker_screen, black, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(picker_screen, LV_OPA_COVER, LV_PART_MAIN);

  make_back_btn();
  make_color_btns();
}

void setup()
{
  Serial.begin(115200);
  prefs.begin("glow-app", false);

  glow_red = prefs.getInt("glowRed", 140);
  glow_green = prefs.getInt("glowGreen", 0);
  glow_blue = prefs.getInt("glowBlue", 255);
  power_state = prefs.getInt("powerState", 1);

  int rt = prefs.getInt("rotate", 0);
  int br = prefs.getInt("brightness", 100);

  // inititalise screen
  tft.init();
  tft.initDMA();
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(rt);

  lv_init(); // initialise LVGL

  // setup screen
  static auto *lvDisplay = lv_display_create(screenWidth, screenHeight);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisplay, my_disp_flush);
  lv_display_set_buffers(lvDisplay, lvBuffer[0], lvBuffer[1], lvBufferSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

  // setup touch
  static auto *lvInput = lv_indev_create();
  lv_indev_set_type(lvInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvInput, my_touchpad_read);

  screenBrightness(br); // startup brightness

  make_home_screen();
  make_picker_screen();

  lv_scr_load(home_screen); 
 
  Serial.print("Setup finished");
}

void loop()
{
  static uint32_t lastTick = millis();
  uint32_t current = millis();
  lv_tick_inc(current - lastTick);
  lastTick = current;
  lv_timer_handler();

  // timeout for screen dimming
  checkScreenDimming();
  delay(5);
}