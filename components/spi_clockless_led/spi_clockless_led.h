#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "led_strip.h"

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

// SK6812/WS2812-Ausgabe ueber das erprobte Espressif-led_strip-SPI-Backend
// (SPI-DMA). Interrupt-immun (W5500) und korrektes Timing -- die eigentliche
// Signalerzeugung uebernimmt der IDF-Treiber. Diese Klasse implementiert nur
// die ESPHome-AddressableLight-Schnittstelle (Puffer + Effekte).
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
  void set_max_refresh_rate(uint32_t interval_us) { this->max_refresh_rate_ = interval_us; }

  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

 protected:
  light::ESPColorView get_view_internal(int32_t index) const override;

  size_t get_buffer_size_() const { return this->num_leds_ * (this->is_rgbw_ ? 4 : 3); }

  uint8_t *buf_{nullptr};          // Pixel im RGB(W)-Standardformat; Reihenfolge macht led_strip
  uint8_t *effect_data_{nullptr};

  led_strip_handle_t strip_{nullptr};

  uint8_t pin_{0};
  uint16_t num_leds_{0};
  bool is_rgbw_{false};
  bool inverted_{false};
  RGBOrder rgb_order_{ORDER_GRB};
  SPIHost spi_host_{SPI_HOST_3};

  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};
};

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
