#pragma once

#ifdef USE_ESP32

// Shared, ISR-safe transfer token for the two LED DMA engines.
//
// IMPORTANT:
// - This is deliberately a binary/counting semaphore, not a mutex. The RMT
//   transaction starts in task context but finishes in ISR context, so its token
//   must be releasable with xSemaphoreGiveFromISR().
// - Never hold a critical section for an LED frame. A 4 ms critical section
//   would suppress the RMT/GDMA refill ISR and damage the frame.

#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace esphome::spi_clockless_led {

inline StaticSemaphore_t led_dma_token_storage{};
inline SemaphoreHandle_t led_dma_token{nullptr};

// Called from component setup, before an ISR can use the token.
inline void init_led_dma_arbiter() {
  if (led_dma_token == nullptr) {
    // Maximum count 1, initially available (count 1).
    led_dma_token = xSemaphoreCreateCountingStatic(1, 1, &led_dma_token_storage);
  }
}

inline bool acquire_led_dma(TickType_t timeout_ticks = portMAX_DELAY) {
  return led_dma_token != nullptr && xSemaphoreTake(led_dma_token, timeout_ticks) == pdTRUE;
}

inline void release_led_dma() {
  if (led_dma_token != nullptr)
    xSemaphoreGive(led_dma_token);
}

// RMT TX-done callback runs in interrupt context. Returning true asks the RMT
// driver to yield from its ISR when a higher-priority task was unblocked.
//
// Defined out-of-line in led_dma_sync.cpp (NOT inline in this header). An
// IRAM_ATTR function that is inlined from a header triggers the xtensa linker
// error "dangerous relocation: l32r: literal placed after use", because the
// literal pool is emitted behind the code in the .iram1 section while l32r can
// only reach backwards. Compiling it as a normal translation-unit symbol lets
// GCC place the literal pool correctly.
bool IRAM_ATTR release_led_dma_from_isr();

}  // namespace esphome::spi_clockless_led

#endif  // USE_ESP32
