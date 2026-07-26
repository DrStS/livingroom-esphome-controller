#pragma once

#ifdef USE_ESP32

#include "esphome/components/esp32_rmt_led_strip/led_strip.h"
#include "esphome/components/spi_clockless_led/led_dma_sync.h"
#include "esphome/components/fireplace_effect/raw_pixel_output.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::serialized_rmt_led {

static const char *const TAG = "serialized_rmt_led";

// Ableitung von ESPHomes Stock esp32_rmt_led_strip mit EINFACHER, ISR-FREIER
// Serialisierung gegen den Sideboard-SPI-Strip.
//
// Warum: Sobald das Sideboard (SPI) einen dynamischen Effekt sendet, "dreht das
// Cabinet durch". Ursache ist gleichzeitiges Senden beider Strips (GDMA-/ISR-/
// Signal-Wechselwirkung auf den benachbarten Pins GPIO45/46). Fix: beide duerfen
// nie gleichzeitig senden -> gemeinsamer Semaphore-Token.
//
// WICHTIG (Boot-Stabilitaet): Der Token wird ausschliesslich im TASK-Kontext
// genommen/freigegeben. Eine fruehere Variante gab ihn im RMT-TX-Done-ISR frei
// (rmt_tx_register_event_callbacks) -> das crashte reproduzierbar den Boot
// (ESP-IDF-Rollback). Deshalb hier KEIN ISR-Callback, KEIN rmt_disable/enable.
class SerializedRMTLedStrip : public esp32_rmt_led_strip::ESP32RMTLEDStripLightOutput,
                              public fireplace_effect::RawPixelOutput {
 public:
  int32_t raw_pixel_count() const override { return this->num_leds_; }

  void set_pixel_raw(int32_t index, const Color &color) override {
    if (index < 0 || index >= this->num_leds_ || this->buf_ == nullptr)
      return;

    int32_t r = 0, g = 0, b = 0;
    switch (this->rgb_order_) {
      case esp32_rmt_led_strip::ORDER_RGB: r = 0; g = 1; b = 2; break;
      case esp32_rmt_led_strip::ORDER_RBG: r = 0; g = 2; b = 1; break;
      case esp32_rmt_led_strip::ORDER_GRB: r = 1; g = 0; b = 2; break;
      case esp32_rmt_led_strip::ORDER_GBR: r = 2; g = 0; b = 1; break;
      case esp32_rmt_led_strip::ORDER_BGR: r = 2; g = 1; b = 0; break;
      case esp32_rmt_led_strip::ORDER_BRG: r = 1; g = 2; b = 0; break;
    }

    const uint8_t multiplier = this->is_rgbw_ || this->is_wrgb_ ? 4 : 3;
    uint8_t *base = this->buf_ + index * multiplier;
    const uint8_t rgb_offset = this->is_wrgb_ ? 1 : 0;
    base[r + rgb_offset] = color.r;
    base[g + rgb_offset] = color.g;
    base[b + rgb_offset] = color.b;
    if (this->is_rgbw_ || this->is_wrgb_)
      base[this->is_wrgb_ ? 0 : 3] = color.w;
  }

  void setup() override {
    // Nur den gemeinsamen Token anlegen (idempotent) und das Stock-Setup laufen
    // lassen. Kein weiterer RMT-Eingriff -> boot-stabil.
    spi_clockless_led::init_led_dma_arbiter();
    esp32_rmt_led_strip::ESP32RMTLEDStripLightOutput::setup();
  }

  void write_state(light::LightState *state) override {
    if (this->is_failed() || this->channel_ == nullptr)
      return;

    // Gleiche Refresh-Rate-Begrenzung wie Stock.
    const uint32_t now = micros();
    const uint32_t rate = this->max_refresh_rate_.value_or(0);
    if (rate != 0 && (now - this->last_refresh_) < rate) {
      this->schedule_show();
      return;
    }

    // Vorherigen Transfer abschliessen (Stock-Verhalten) + bewusste 50-us-Pause.
    esp_err_t err = rmt_tx_wait_all_done(this->channel_, 1000);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "RMT TX timeout: %s", esp_err_to_name(err));
      this->status_set_warning();
      return;
    }
    delayMicroseconds(50);
    memcpy(this->rmt_buf_, this->buf_, this->get_buffer_size_());

    // Serialisierung: Token nehmen, damit das Sideboard-SPI NICHT gleichzeitig
    // sendet. Der Token wird erst freigegeben, nachdem der RMT-Frame physisch
    // komplett auf der Leitung ist. Alles im Task-Kontext (kein ISR).
    if (!spi_clockless_led::acquire_led_dma(pdMS_TO_TICKS(100))) {
      ESP_LOGW(TAG, "LED bus token timeout before RMT transfer");
      this->status_set_warning();
      this->schedule_show();
      return;
    }

    rmt_transmit_config_t config{};
    err = rmt_transmit(this->channel_, this->encoder_, this->rmt_buf_, this->get_buffer_size_(), &config);
    if (err == ESP_OK) {
      // Blockierend bis der Frame komplett gesendet ist.
      err = rmt_tx_wait_all_done(this->channel_, 1000);
      // Danach das SK6812-Latch-/Reset-Fenster (>80 us) ABWARTEN, BEVOR der
      // Token freigegeben wird. Sonst beginnt das Sideboard sofort auf dem
      // Nachbarpin GPIO45 zu senden, waehrend die Cabinet-LEDs noch latchen ->
      // Stoerung genau der zuletzt uebertragenen (obersten) LEDs = "Tail flackert".
      delayMicroseconds(300);
    }
    spi_clockless_led::release_led_dma();

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "RMT TX failed: %s", esp_err_to_name(err));
      this->status_set_warning();
      this->schedule_show();
      return;
    }

    this->last_refresh_ = now;
    this->mark_shown_();
    this->status_clear_warning();
  }
};

}  // namespace esphome::serialized_rmt_led

#endif  // USE_ESP32
