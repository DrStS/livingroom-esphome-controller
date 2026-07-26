#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/fireplace_effect/raw_pixel_output.h"

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

// SK6812-RGBW-Ausgabe ueber SPI-DMA. Jedes LED-Datenbit wird mit vier
// SPI-Bits kodiert (0=1000, 1=1100). Bei 3,2 MHz entstehen 800-kbit/s-Symbole
// mit SK6812-konformen High-/Low-Zeiten. SPI-DMA ist unempfindlich gegen
// normale Interrupt-Latenzen des W5500.
class SPIClocklessLedStrip final : public light::AddressableLight, public fireplace_effect::RawPixelOutput {
 public:
  void setup() override;
  void write_state(light::LightState *state) override;
  void dump_config() override;
  float get_setup_priority() const override;

  int32_t size() const override { return this->num_leds_; }
  int32_t raw_pixel_count() const override { return this->num_leds_; }
  void set_pixel_raw(int32_t index, const Color &color) override;

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
  // 4 SPI-Bytes pro Farbbyte (4 SPI-Bits pro Datenbit) plus mindestens
  // 80 us Low-Pegel fuer das SK6812-Latch. 40 Byte entsprechen selbst bei
  // 3,5 MHz noch gut 91 us.
  static constexpr size_t RESET_BYTES_{40};
  size_t get_spi_payload_size_() const { return this->get_buffer_size_() * 4; }
  size_t get_spi_buffer_size_() const { return this->get_spi_payload_size_() + RESET_BYTES_; }

  uint8_t *buf_{nullptr};       // Pixel im RGB(W)-Standardformat (r,g,b,w)
  uint8_t *effect_data_{nullptr};
  uint8_t *spi_buf_{nullptr};   // DMA-Sendepuffer (SPI-kodiert)

  spi_device_handle_t spi_dev_{nullptr};

  uint8_t pin_{0};
  uint16_t num_leds_{0};
  bool is_rgbw_{false};
  uint32_t clock_speed_{3200000};
  RGBOrder rgb_order_{ORDER_GRB};
  SPIHost spi_host_{SPI_HOST_3};

  // Positionen der Farbkomponenten auf dem Draht (aus rgb_order, in setup gesetzt).
  uint8_t r_pos_{1}, g_pos_{0}, b_pos_{2}, w_pos_{3};

  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};
};

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
