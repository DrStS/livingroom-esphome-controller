#pragma once

#include "esphome/core/color.h"

#include <cstdint>

namespace esphome::fireplace_effect {

// Minimal interface implemented by addressable LED outputs that permit a
// deliberately gamma-bypassed pixel write. Normal ESPHome writes continue to
// use ESPColorView and ESPColorCorrection; only effects explicitly wired to
// this interface can write raw PWM bytes.
class RawPixelOutput {
 public:
  virtual ~RawPixelOutput() = default;
  virtual int32_t raw_pixel_count() const = 0;
  virtual void set_pixel_raw(int32_t index, const Color &color) = 0;
};

}  // namespace esphome::fireplace_effect
