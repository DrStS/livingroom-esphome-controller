#include "spi_clockless_led.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include <cstring>

namespace esphome::spi_clockless_led {

static const char *const TAG = "spi_clockless_led";

// RGB-Reihenfolge -> Positionen der Farbkomponenten auf dem Draht.
static void order_positions(RGBOrder o, uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (o) {
    case ORDER_RGB: r = 0; g = 1; b = 2; break;
    case ORDER_RBG: r = 0; g = 2; b = 1; break;
    case ORDER_GRB: r = 1; g = 0; b = 2; break;
    case ORDER_GBR: r = 2; g = 0; b = 1; break;
    case ORDER_BGR: r = 2; g = 1; b = 0; break;
    case ORDER_BRG: r = 1; g = 2; b = 0; break;
    default:        r = 1; g = 0; b = 2; break;  // GRB
  }
}

void SPIClocklessLedStrip::setup() {
  const size_t buffer_size = this->get_buffer_size_();

  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->buf_ = allocator.allocate(buffer_size);
  this->effect_data_ = allocator.allocate(this->num_leds_);
  if (this->buf_ == nullptr || this->effect_data_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate LED buffers!");
    this->mark_failed();
    return;
  }
  memset(this->buf_, 0, buffer_size);
  memset(this->effect_data_, 0, this->num_leds_);

  uint8_t rp, gp, bp;
  order_positions(this->rgb_order_, rp, gp, bp);

  led_color_component_format_t fmt;
  fmt.format_id = 0;
  fmt.format.r_pos = rp;
  fmt.format.g_pos = gp;
  fmt.format.b_pos = bp;
  fmt.format.w_pos = 3;
  fmt.format.reserved = 0;
  fmt.format.bytes_per_color = 1;
  fmt.format.num_components = this->is_rgbw_ ? 4 : 3;

  led_strip_config_t strip_config;
  memset(&strip_config, 0, sizeof(strip_config));
  strip_config.strip_gpio_num = this->pin_;
  strip_config.max_leds = this->num_leds_;
  strip_config.led_model = LED_MODEL_SK6812;
  strip_config.color_component_format = fmt;
  strip_config.flags.invert_out = this->inverted_ ? 1 : 0;

  led_strip_spi_config_t spi_config;
  memset(&spi_config, 0, sizeof(spi_config));
  spi_config.clk_src = SPI_CLK_SRC_DEFAULT;
  spi_config.spi_bus = (this->spi_host_ == SPI_HOST_2) ? SPI2_HOST : SPI3_HOST;
  spi_config.flags.with_dma = 1;

  esp_err_t err = led_strip_new_spi_device(&strip_config, &spi_config, &this->strip_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "led_strip_new_spi_device failed: %d", err);
    this->mark_failed();
    return;
  }
}

void SPIClocklessLedStrip::write_state(light::LightState *state) {
  if (this->is_failed() || this->strip_ == nullptr)
    return;

  const uint32_t now = micros();
  const uint32_t rate = this->max_refresh_rate_.value_or(0);
  if (rate != 0 && (now - this->last_refresh_) < rate) {
    this->schedule_show();
    return;
  }
  this->last_refresh_ = now;
  this->mark_shown_();

  const uint8_t mult = this->is_rgbw_ ? 4 : 3;
  for (int i = 0; i < this->num_leds_; i++) {
    const uint8_t *p = this->buf_ + (i * mult);
    if (this->is_rgbw_) {
      led_strip_set_pixel_rgbw(this->strip_, i, p[0], p[1], p[2], p[3]);
    } else {
      led_strip_set_pixel(this->strip_, i, p[0], p[1], p[2]);
    }
  }
  esp_err_t err = led_strip_refresh(this->strip_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "led_strip_refresh failed: %d", err);
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
}

// buf_ speichert im RGB(W)-Standardformat (r,g,b,w); die Reihenfolge auf dem
// Draht macht led_strip via color_component_format.
light::ESPColorView SPIClocklessLedStrip::get_view_internal(int32_t index) const {
  const uint8_t mult = this->is_rgbw_ ? 4 : 3;
  uint8_t *base = this->buf_ + (index * mult);
  return {base + 0,
          base + 1,
          base + 2,
          this->is_rgbw_ ? base + 3 : nullptr,
          &this->effect_data_[index],
          &this->correction_};
}

void SPIClocklessLedStrip::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SPI Clockless LED Strip (Espressif led_strip):\n"
                "  Pin (MOSI): %u\n"
                "  SPI host: SPI%u\n"
                "  RGBW: %s\n"
                "  Number of LEDs: %u",
                this->pin_, (this->spi_host_ == SPI_HOST_2) ? 2 : 3, YESNO(this->is_rgbw_), this->num_leds_);
}

float SPIClocklessLedStrip::get_setup_priority() const { return setup_priority::HARDWARE; }

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
