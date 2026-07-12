#pragma once
#include <cstdint>
namespace esphome::living_room::lr {
struct FanConfig { float off_c{32.0f}; float low_c{38.0f}; float mid_c{45.0f}; float high_c{52.0f}; float min_duty{0.20f}; };
struct FanState { float duty{0.0f}; bool fault{false}; };
class FanController final {
 public:
  void configure(FanConfig cfg) { cfg_ = cfg; }
  [[nodiscard]] FanState tick(float temp_c, float rpm) const {
    FanState out{};
    if (temp_c < cfg_.off_c) out.duty = 0.0f;
    else if (temp_c < cfg_.low_c) out.duty = cfg_.min_duty;
    else if (temp_c < cfg_.mid_c) out.duty = 0.45f;
    else if (temp_c < cfg_.high_c) out.duty = 0.70f;
    else out.duty = 1.0f;
    out.fault = out.duty > 0.25f && rpm < 100.0f;
    return out;
  }
 private:
  FanConfig cfg_{};
};
}  // namespace esphome::living_room::lr
