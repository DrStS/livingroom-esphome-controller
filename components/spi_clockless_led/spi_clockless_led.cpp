#include "spi_clockless_led.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <cstring>

namespace esphome::spi_clockless_led {

static const char *const TAG = "spi_clockless_led";

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

// Bewaehrte 3-Bit-Kodierung aus Espressifs led_strip: jedes Datenbit wird als
// 3 SPI-Bits kodiert ("0" = 100, "1" = 110), ein Farbbyte -> 3 SPI-Bytes,
// MSB zuerst. Der Puffer muss vorher fuer diese 3 Bytes 0 sein.
static void spi_encode_byte(uint8_t data, uint8_t *buf) {
  buf[2] |= data & BIT(0) ? BIT(2) | BIT(1) : BIT(2);
  buf[2] |= data & BIT(1) ? BIT(5) | BIT(4) : BIT(5);
  buf[2] |= data & BIT(2) ? BIT(7) : 0x00;
  buf[1] |= BIT(0);
  buf[1] |= data & BIT(3) ? BIT(3) | BIT(2) : BIT(3);
  buf[1] |= data & BIT(4) ? BIT(6) | BIT(5) : BIT(6);
  buf[0] |= data & BIT(5) ? BIT(1) | BIT(0) : BIT(1);
  buf[0] |= data & BIT(6) ? BIT(4) | BIT(3) : BIT(4);
  buf[0] |= data & BIT(7) ? BIT(7) | BIT(6) : BIT(7);
}

void SPIClocklessLedStrip::setup() {
  // Farbkomponenten-Positionen aus rgb_order ableiten.
  switch (this->rgb_order_) {
    case ORDER_RGB: this->r_pos_ = 0; this->g_pos_ = 1; this->b_pos_ = 2; break;
    case ORDER_RBG: this->r_pos_ = 0; this->g_pos_ = 2; this->b_pos_ = 1; break;
    case ORDER_GRB: this->r_pos_ = 1; this->g_pos_ = 0; this->b_pos_ = 2; break;
    case ORDER_GBR: this->r_pos_ = 2; this->g_pos_ = 0; this->b_pos_ = 1; break;
    case ORDER_BGR: this->r_pos_ = 2; this->g_pos_ = 1; this->b_pos_ = 0; break;
    case ORDER_BRG: this->r_pos_ = 1; this->g_pos_ = 2; this->b_pos_ = 0; break;
  }
  this->w_pos_ = 3;

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

  this->spi_buf_ = static_cast<uint8_t *>(
      heap_caps_malloc(spi_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (this->spi_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate SPI DMA buffer!");
    this->mark_failed();
    return;
  }
  memset(this->spi_buf_, 0, spi_buffer_size);

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
  devcfg.clock_source = SPI_CLK_SRC_DEFAULT;
  devcfg.clock_speed_hz = static_cast<int>(this->clock_speed_);
  devcfg.mode = 0;
  devcfg.spics_io_num = -1;
  devcfg.queue_size = 4;
  if (spi_bus_add_device(host, &devcfg, &this->spi_dev_) != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed");
    this->mark_failed();
    return;
  }

  int actual_khz = 0;
  spi_device_get_actual_freq(this->spi_dev_, &actual_khz);
  ESP_LOGI(TAG, "SPI clockless LED ready (pin %u, %d kHz, %u LEDs)", this->pin_, actual_khz, this->num_leds_);
}

void SPIClocklessLedStrip::write_state(light::LightState *state) {
  if (this->is_failed() || this->spi_dev_ == nullptr)
    return;

  const uint32_t now = micros();
  const uint32_t rate = this->max_refresh_rate_.value_or(0);
  if (rate != 0 && (now - this->last_refresh_) < rate) {
    this->schedule_show();
    return;
  }
  this->last_refresh_ = now;
  this->mark_shown_();

  const uint8_t bpp = this->bytes_per_pixel_();
  for (int i = 0; i < this->num_leds_; i++) {
    const uint8_t *src = this->buf_ + (i * bpp);         // r,g,b(,w)
    uint8_t *dst = this->spi_buf_ + (i * bpp * 3);        // 3 SPI-Bytes je Farbe
    memset(dst, 0, bpp * 3);
    spi_encode_byte(src[0], dst + 3 * this->r_pos_);      // rot
    spi_encode_byte(src[1], dst + 3 * this->g_pos_);      // gruen
    spi_encode_byte(src[2], dst + 3 * this->b_pos_);      // blau
    if (this->is_rgbw_)
      spi_encode_byte(src[3], dst + 3 * this->w_pos_);    // weiss
  }

  spi_transaction_t trans;
  memset(&trans, 0, sizeof(trans));
  trans.length = this->get_spi_buffer_size_() * 8;  // Bits
  trans.tx_buffer = this->spi_buf_;

  const esp_err_t err = spi_device_transmit(this->spi_dev_, &trans);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SPI transmit failed: %d", err);
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
}

// buf_ speichert im RGB(W)-Standardformat (r,g,b,w); die Reihenfolge auf dem
// Draht macht write_state via r_pos_/g_pos_/b_pos_/w_pos_.
light::ESPColorView SPIClocklessLedStrip::get_view_internal(int32_t index) const {
  const uint8_t bpp = this->bytes_per_pixel_();
  uint8_t *base = this->buf_ + (index * bpp);
  return {base + 0,
          base + 1,
          base + 2,
          this->is_rgbw_ ? base + 3 : nullptr,
          &this->effect_data_[index],
          &this->correction_};
}

void SPIClocklessLedStrip::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SPI Clockless LED Strip (SK6812-getunt):\n"
                "  Pin (MOSI): %u\n"
                "  SPI host: SPI%u\n"
                "  Clock: %u Hz\n"
                "  RGBW: %s\n"
                "  Number of LEDs: %u",
                this->pin_, (this->spi_host_ == SPI_HOST_2) ? 2 : 3, this->clock_speed_, YESNO(this->is_rgbw_),
                this->num_leds_);
}

float SPIClocklessLedStrip::get_setup_priority() const { return setup_priority::HARDWARE; }

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
