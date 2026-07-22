#include "spi_clockless_led.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <cstring>

namespace esphome::spi_clockless_led {

static const char *const TAG = "spi_clockless_led";

// 4-Bit-Kodierung pro Datenbit bei ~3,2 MHz SPI-Takt (0,3125 us pro SPI-Bit):
//   "0" -> 1000  (High 0,31 us / Low 0,94 us)
//   "1" -> 1100  (High 0,63 us / Low 0,63 us)
// Beide liegen sicher innerhalb der SK6812-/WS2812-Toleranz.
static const uint8_t NIBBLE_0 = 0b1000;
static const uint8_t NIBBLE_1 = 0b1100;

void SPIClocklessLedStrip::setup() {
  const size_t buffer_size = this->get_buffer_size_();
  const size_t spi_buffer_size = this->get_spi_buffer_size_();

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

  // DMA-faehiger Sendepuffer (intern, nicht PSRAM).
  this->dma_buf_ = static_cast<uint8_t *>(heap_caps_malloc(spi_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (this->dma_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate DMA buffer!");
    this->mark_failed();
    return;
  }
  memset(this->dma_buf_, 0, spi_buffer_size);

  const spi_host_device_t host = (this->spi_host_ == SPI_HOST_2) ? SPI2_HOST : SPI3_HOST;

  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(buscfg));
  buscfg.mosi_io_num = this->pin_;
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = static_cast<int>(spi_buffer_size);
  if (spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize failed");
    this->mark_failed();
    return;
  }

  spi_device_interface_config_t devcfg;
  memset(&devcfg, 0, sizeof(devcfg));
  devcfg.clock_speed_hz = static_cast<int>(this->clock_speed_);
  devcfg.mode = 0;
  devcfg.spics_io_num = -1;
  devcfg.queue_size = 1;
  devcfg.flags = SPI_DEVICE_NO_DUMMY;
  if (spi_bus_add_device(host, &devcfg, &this->spi_dev_) != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed");
    this->mark_failed();
    return;
  }
}

void SPIClocklessLedStrip::write_state(light::LightState *state) {
  if (this->is_failed())
    return;

  const uint32_t now = micros();
  const uint32_t rate = this->max_refresh_rate_.value_or(0);
  if (rate != 0 && (now - this->last_refresh_) < rate) {
    this->schedule_show();
    return;
  }
  this->last_refresh_ = now;
  this->mark_shown_();

  // Datenpuffer -> 4-Bit-SPI-Kodierung (2 Datenbits pro SPI-Byte).
  const size_t buffer_size = this->get_buffer_size_();
  uint8_t *dst = this->dma_buf_;
  for (size_t i = 0; i < buffer_size; i++) {
    const uint8_t b = this->buf_[i];
    for (int pair = 0; pair < 4; pair++) {
      const uint8_t hi = (b >> (7 - 2 * pair)) & 0x01;
      const uint8_t lo = (b >> (6 - 2 * pair)) & 0x01;
      const uint8_t nib_hi = hi ? NIBBLE_1 : NIBBLE_0;
      const uint8_t nib_lo = lo ? NIBBLE_1 : NIBBLE_0;
      *dst++ = static_cast<uint8_t>((nib_hi << 4) | nib_lo);
    }
  }

  spi_transaction_t trans;
  memset(&trans, 0, sizeof(trans));
  trans.length = this->get_spi_buffer_size_() * 8;  // in Bits
  trans.tx_buffer = this->dma_buf_;

  const esp_err_t err = spi_device_polling_transmit(this->spi_dev_, &trans);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI transmit failed: %d", err);
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
}

light::ESPColorView SPIClocklessLedStrip::get_view_internal(int32_t index) const {
  int32_t r = 0, g = 0, b = 0;
  switch (this->rgb_order_) {
    case ORDER_RGB:
      r = 0;
      g = 1;
      b = 2;
      break;
    case ORDER_RBG:
      r = 0;
      g = 2;
      b = 1;
      break;
    case ORDER_GRB:
      r = 1;
      g = 0;
      b = 2;
      break;
    case ORDER_GBR:
      r = 2;
      g = 0;
      b = 1;
      break;
    case ORDER_BGR:
      r = 2;
      g = 1;
      b = 0;
      break;
    case ORDER_BRG:
      r = 1;
      g = 2;
      b = 0;
      break;
  }
  const uint8_t multiplier = this->is_rgbw_ ? 4 : 3;
  return {this->buf_ + (index * multiplier) + r,
          this->buf_ + (index * multiplier) + g,
          this->buf_ + (index * multiplier) + b,
          this->is_rgbw_ ? this->buf_ + (index * multiplier) + 3 : nullptr,
          &this->effect_data_[index],
          &this->correction_};
}

void SPIClocklessLedStrip::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SPI Clockless LED Strip:\n"
                "  Pin (MOSI): %u\n"
                "  SPI host: SPI%u\n"
                "  Clock: %u Hz\n"
                "  RGBW: %s\n"
                "  Number of LEDs: %u",
                this->pin_, (this->spi_host_ == SPI_HOST_2) ? 2 : 3, this->clock_speed_,
                YESNO(this->is_rgbw_), this->num_leds_);
}

float SPIClocklessLedStrip::get_setup_priority() const { return setup_priority::HARDWARE; }

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
