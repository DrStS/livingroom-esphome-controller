#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <driver/spi_master.h>
#include <esp_err.h>

namespace esphome::spi_clockless_led {

enum RGBOrder : uint8_t {
  ORDER_RGB,
  ORDER_RBG,
  ORDER_GRB,
  ORDER_GBR,
  ORDER_BGR,
  ORDER_BRG,
};

enum SPIHost : uint8_t {
  SPI_HOST_2,
  SPI_HOST_3,
};

// SK6812/WS2812-Ausgabe ueber SPI-DMA. Verwendet die bewaehrte 3-Bit-Kodierung
// von Espressifs led_strip ("0" = 100, "1" = 110), aber mit einem hoeheren
// SPI-Takt (Default 3,33 MHz) -> "1"-High = 0,6 us, "0"-High = 0,3 us, exakt
// SK6812-konform (led_strip selbst laeuft fix mit 2,5 MHz = 0,8 us "1", zu lang
// fuer SK6812). SPI-DMA ist interrupt-immun gegenueber dem W5500.
class SPIClocklessLedStrip final : public light::AddressableLight {
 public:
  void setup() override;
  void write_state(light::LightState *state) override;
  void dump_config() override;
  float get_setup_priority() const override;

  int32_t size() const override { return this->num_leds_; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->is_rgbw_) {
      traits.set_supported_color_modes({light::ColorMode::RGB_WHITE, light::ColorMode::WHITE});
    } else {
      traits.set_supported_color_modes({light::ColorMode::RGB});
    }
    return traits;
  }

  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_inverted(bool inverted) { this->inverted_ = inverted; }
  void set_num_leds(uint16_t num_leds) { this->num_leds_ = num_leds; }
  void set_is_rgbw(bool is_rgbw) { this->is_rgbw_ = is_rgbw; }
  void set_rgb_order(RGBOrder rgb_order) { this->rgb_order_ = rgb_order; }
  void set_spi_host(SPIHost host) { this->spi_host_ = host; }
  void set_clock_speed(uint32_t hz) { this->clock_speed_ = hz; }
  void set_max_refresh_rate(uint32_t interval_us) { this->max_refresh_rate_ = interval_us; }

  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

 protected:
  light::ESPColorView get_view_internal(int32_t index) const override;

  uint8_t bytes_per_pixel_() const { return this->is_rgbw_ ? 4 : 3; }
  size_t get_buffer_size_() const { return this->num_leds_ * this->bytes_per_pixel_(); }
  // 3 SPI-Bytes pro Farbbyte (3 SPI-Bits pro Datenbit).
  size_t get_spi_buffer_size_() const { return this->get_buffer_size_() * 3; }

  uint8_t *buf_{nullptr};       // Pixel im RGB(W)-Standardformat (r,g,b,w)
  uint8_t *effect_data_{nullptr};
  uint8_t *spi_buf_{nullptr};   // DMA-Sendepuffer (SPI-kodiert)

  spi_device_handle_t spi_dev_{nullptr};

  uint8_t pin_{0};
  uint16_t num_leds_{0};
  bool is_rgbw_{false};
  bool inverted_{false};
  uint32_t clock_speed_{3333333};
  RGBOrder rgb_order_{ORDER_GRB};
  SPIHost spi_host_{SPI_HOST_3};

  // Positionen der Farbkomponenten auf dem Draht (aus rgb_order, in setup gesetzt).
  uint8_t r_pos_{1}, g_pos_{0}, b_pos_{2}, w_pos_{3};

  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};
};

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
