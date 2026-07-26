#ifdef USE_ESP32

#include "led_dma_sync.h"

namespace esphome::spi_clockless_led {

// Kept in a real translation unit (not inline in the header) so the xtensa
// compiler places the l32r literal pool ahead of the IRAM code. See the
// declaration in led_dma_sync.h for the full rationale. IRAM_ATTR lives on the
// declaration only; repeating it here would conflict (duplicate .iram1.N
// section) and GCC would warn and drop the second attribute.
bool release_led_dma_from_isr() {
  if (led_dma_token == nullptr)
    return false;
  BaseType_t higher_priority_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(led_dma_token, &higher_priority_task_woken);
  return higher_priority_task_woken == pdTRUE;
}

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
