#include "spi_clockless_led.h"

#ifdef USE_ESP32

#include "led_dma_sync.h"
#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <cstring>

namespace esphome::spi_clockless_led {

static const char *const TAG = "spi_clockless_led";

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

// SK6812-konforme 4-Bit-Kodierung:
//   Datenbit 0 -> 1000
//   Datenbit 1 -> 1100
// Bei 3,2 MHz dauert ein SPI-Bit 0,3125 us. Daraus ergeben sich:
//   0: T0H=0,3125 us, T0L=0,9375 us
//   1: T1H=0,6250 us, T1L=0,6250 us
// Ein LED-Datenbit dauert damit exakt 1,25 us (800 kbit/s).
// Ein Farbbyte wird zu vier SPI-Bytes, MSB zuerst.
static void spi_encode_byte(uint8_t data, uint8_t *buf) {
  uint32_t encoded = 0;
  for (int bit = 7; bit >= 0; --bit) {
    encoded <<= 4;
    encoded |= (data & (1u << bit)) != 0 ? 0xCu : 0x8u;
  }
  buf[0] = static_cast<uint8_t>(encoded >> 24);
  buf[1] = static_cast<uint8_t>(encoded >> 16);
  buf[2] = static_cast<uint8_t>(encoded >> 8);
  buf[3] = static_cast<uint8_t>(encoded);
}


void SPIClocklessLedStrip::setup() {
  init_led_dma_arbiter();
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

  const uint8_t bpp = this->bytes_per_pixel_();
  for (int i = 0; i < this->num_leds_; i++) {
    const uint8_t *src = this->buf_ + (i * bpp);         // r,g,b(,w)
    uint8_t *dst = this->spi_buf_ + (i * bpp * 4);        // 4 SPI-Bytes je Farbe
    spi_encode_byte(src[0], dst + 4 * this->r_pos_);      // rot
    spi_encode_byte(src[1], dst + 4 * this->g_pos_);      // gruen
    spi_encode_byte(src[2], dst + 4 * this->b_pos_);      // blau
    if (this->is_rgbw_)
      spi_encode_byte(src[3], dst + 4 * this->w_pos_);    // weiss
  }

  spi_transaction_t trans;
  memset(&trans, 0, sizeof(trans));
  trans.length = this->get_spi_buffer_size_() * 8;  // Bits
  trans.tx_buffer = this->spi_buf_;

  // Strict bidirectional serialization: the same token is taken by RMT before
  // rmt_transmit() and released only by the RMT TX-done ISR callback.
  if (!acquire_led_dma(pdMS_TO_TICKS(100))) {
    ESP_LOGW(TAG, "LED DMA arbiter timeout before SPI transfer");
    this->status_set_warning();
    this->schedule_show();
    return;
  }

  const esp_err_t err = spi_device_transmit(this->spi_dev_, &trans);
  release_led_dma();

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SPI transmit failed: %d", err);
    this->status_set_warning();
    // Frame was not sent -> retry on the next loop and do NOT mark it shown or
    // advance the refresh timestamp.
    this->schedule_show();
    return;
  }

  // Only now the frame has physically been transferred: mark it shown and start
  // the refresh-rate window. This prevents ESPHome from treating a failed frame
  // (arbiter timeout / SPI error) as displayed.
  this->last_refresh_ = now;
  this->mark_shown_();
  this->status_clear_warning();
}

// buf_ speichert im RGB(W)-Standardformat (r,g,b,w); die Reihenfolge auf dem
// Draht macht write_state via r_pos_/g_pos_/b_pos_/w_pos_.
void SPIClocklessLedStrip::set_pixel_raw(int32_t index, const Color &color) {
  if (index < 0 || index >= this->num_leds_ || this->buf_ == nullptr)
    return;
  const uint8_t bpp = this->bytes_per_pixel_();
  uint8_t *base = this->buf_ + (index * bpp);
  base[0] = color.r;
  base[1] = color.g;
  base[2] = color.b;
  if (this->is_rgbw_)
    base[3] = color.w;
}

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
