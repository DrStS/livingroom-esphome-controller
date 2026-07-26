#pragma once

#include "raw_pixel_output.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/core/helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esphome::fireplace_effect {

enum FireplaceRole : uint8_t {
  FIREPLACE_ROLE_DIRECT = 0,
  FIREPLACE_ROLE_REFLECTION = 1,
};

struct FireColorStop {
  uint8_t level;
  Color color;
};

// All colors below are raw physical PWM starting values, not ESPHome pre-gamma
// color values. The companion "Fireplace Palette" effect shows these exact
// stops on the installed strip so they can be calibrated in one central table.
inline static const FireColorStop FIRE_PALETTE[] = {
    {0, Color(0, 0, 0, 0)},
    {24, Color(2, 0, 0, 0)},
    {48, Color(8, 0, 0, 0)},
    {80, Color(24, 1, 0, 0)},
    {112, Color(60, 4, 0, 0)},
    {144, Color(120, 12, 0, 0)},
    {176, Color(205, 38, 0, 0)},
    {208, Color(255, 92, 0, 0)},
    {232, Color(255, 150, 0, 3)},
    {255, Color(255, 205, 0, 12)},
};

inline constexpr size_t FIRE_PALETTE_SIZE = sizeof(FIRE_PALETTE) / sizeof(FIRE_PALETTE[0]);

inline uint8_t scale_u8(uint8_t value, float gain) {
  const int scaled = static_cast<int>(std::lround(static_cast<float>(value) * gain));
  return static_cast<uint8_t>(std::clamp(scaled, 0, 255));
}

inline Color lookup_fire_palette(uint8_t level) {
  if (level == 0)
    return FIRE_PALETTE[0].color;

  for (size_t i = 1; i < FIRE_PALETTE_SIZE; i++) {
    const FireColorStop &hi = FIRE_PALETTE[i];
    if (level > hi.level)
      continue;

    const FireColorStop &lo = FIRE_PALETTE[i - 1];
    const uint16_t span = static_cast<uint16_t>(hi.level - lo.level);
    const uint16_t pos = static_cast<uint16_t>(level - lo.level);
    const auto lerp = [span, pos](uint8_t a, uint8_t b) -> uint8_t {
      return static_cast<uint8_t>((static_cast<uint32_t>(a) * (span - pos) +
                                   static_cast<uint32_t>(b) * pos + span / 2U) /
                                  span);
    };
    return Color(lerp(lo.color.r, hi.color.r), lerp(lo.color.g, hi.color.g),
                 lerp(lo.color.b, hi.color.b), lerp(lo.color.w, hi.color.w));
  }
  return FIRE_PALETTE[FIRE_PALETTE_SIZE - 1].color;
}

class XorShift32 {
 public:
  explicit XorShift32(uint32_t seed = 0x6D2B79F5U) : state_(seed != 0 ? seed : 0x6D2B79F5U) {}

  uint32_t next() {
    uint32_t x = this->state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    this->state_ = x;
    return x;
  }

  float unit() { return static_cast<float>(this->next() & 0x00FFFFFFU) / 16777215.0f; }
  float range(float lo, float hi) { return lo + (hi - lo) * this->unit(); }

 private:
  uint32_t state_;
};

// One global temporal signal keeps the fast/medium/slow flicker coherent when
// both strips run the effect. It is a visual signal model only: no combustion,
// fluid or heat simulation.
class SharedFireSignal {
 public:
  void update(uint32_t now_ms) {
    if (this->last_step_ms_ == 0) {
      this->last_step_ms_ = now_ms;
      this->next_medium_ms_ = now_ms + 120;
      this->next_slow_ms_ = now_ms + 900;
      this->next_frequency_ms_ = now_ms + 1000;
      return;
    }

    uint32_t elapsed = now_ms - this->last_step_ms_;
    uint8_t steps = 0;
    while (elapsed >= 10U && steps < 6U) {
      this->last_step_ms_ += 10U;
      elapsed -= 10U;
      steps++;

      if (static_cast<int32_t>(this->last_step_ms_ - this->next_frequency_ms_) >= 0) {
        this->frequency_target_hz_ = this->rng_.range(9.2f, 12.8f);
        this->next_frequency_ms_ = this->last_step_ms_ + 700U + (this->rng_.next() % 900U);
      }
      this->frequency_hz_ += (this->frequency_target_hz_ - this->frequency_hz_) * 0.018f;

      if (static_cast<int32_t>(this->last_step_ms_ - this->next_medium_ms_) >= 0) {
        this->medium_target_ = this->rng_.range(0.20f, 0.95f);
        this->next_medium_ms_ = this->last_step_ms_ + 90U + (this->rng_.next() % 180U);
      }
      if (static_cast<int32_t>(this->last_step_ms_ - this->next_slow_ms_) >= 0) {
        this->slow_target_ = this->rng_.range(0.30f, 0.90f);
        this->next_slow_ms_ = this->last_step_ms_ + 700U + (this->rng_.next() % 1200U);
      }

      this->medium_ += (this->medium_target_ - this->medium_) * 0.085f;
      this->slow_ += (this->slow_target_ - this->slow_) * 0.012f;
      this->jitter_ += (this->rng_.range(0.0f, 1.0f) - this->jitter_) * 0.32f;

      this->phase_fast_ += 6.28318530718f * this->frequency_hz_ * 0.010f;
      this->phase_harmonic_ += 6.28318530718f * (this->frequency_hz_ * 1.43f + 0.7f) * 0.010f;
      if (this->phase_fast_ > 6.28318530718f)
        this->phase_fast_ -= 6.28318530718f;
      if (this->phase_harmonic_ > 6.28318530718f)
        this->phase_harmonic_ -= 6.28318530718f;

      const float sine = 0.5f + 0.5f * std::sin(this->phase_fast_);
      const float asymmetric = sine * sine * (3.0f - 2.0f * sine);
      const float harmonic = 0.5f + 0.5f * std::sin(this->phase_harmonic_ + 0.7f);
      // Weniger reine Sinus-Anteile (wirken zu regelmaessig), dafuer mehr
      // Jitter -> das schnelle Flackern wird unregelmaessiger/natuerlicher.
      this->fast_ = std::clamp(0.40f * asymmetric + 0.08f * harmonic + 0.52f * this->jitter_, 0.0f, 1.0f);
    }

    // Do not run an unbounded catch-up loop after a long scheduler pause.
    if (elapsed >= 10U)
      this->last_step_ms_ = now_ms;
  }

  float fast() const { return this->fast_; }
  float medium() const { return this->medium_; }
  float slow() const { return this->slow_; }
  float phase() const { return this->phase_fast_; }

 protected:
  XorShift32 rng_{0xC001CAFEU};
  uint32_t last_step_ms_{0};
  uint32_t next_medium_ms_{0};
  uint32_t next_slow_ms_{0};
  uint32_t next_frequency_ms_{0};
  float phase_fast_{0.0f};
  float phase_harmonic_{0.0f};
  float frequency_hz_{10.7f};
  float frequency_target_hz_{10.7f};
  float fast_{0.5f};
  float medium_{0.55f};
  float medium_target_{0.55f};
  float slow_{0.60f};
  float slow_target_{0.60f};
  float jitter_{0.5f};
};

inline SharedFireSignal SHARED_FIRE_SIGNAL;

class RawFireEffectBase : public light::AddressableLightEffect {
 public:
  explicit RawFireEffectBase(const char *name) : light::AddressableLightEffect(name) {}

  void set_raw_output(RawPixelOutput *output) { this->raw_output_ = output; }
  void set_role(FireplaceRole role) { this->role_ = role; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ms_ = interval_ms; }
  void set_intensity(float intensity) { this->intensity_ = std::clamp(intensity, 0.0f, 1.0f); }
  void set_output_gain(float gain) { this->output_gain_ = std::max(gain, 0.0f); }
  void set_green_gain(float gain) { this->green_gain_ = std::max(gain, 0.0f); }
  void set_white_gain(float gain) { this->white_gain_ = std::max(gain, 0.0f); }

  void stop() override {
    auto *addressable = this->get_addressable_();
    addressable->set_effect_active(false);
    // Immediately re-render through the normal ESPHome correction path so no
    // raw fire frame remains visible after switching to "None" or a static color.
    addressable->update_state(this->state_);
  }

 protected:
  float slider_gain_() const {
    const float logical_brightness = std::clamp(
        this->state_->current_values.get_state() * this->state_->current_values.get_brightness(), 0.0f, 1.0f);
    // The fire palette is already in physical/raw PWM space. Apply only the
    // configured ESPHome gamma curve of the HA brightness slider as one global
    // gain; do not gamma-correct the individual fire channels again.
    return this->state_->gamma_correct_lut(logical_brightness);
  }

  Color profile_color_(Color color, float slider_gain) const {
    float role_gain = this->output_gain_;
    float red_gain = 1.0f;
    float green_gain = this->green_gain_;
    float white_gain = this->white_gain_;
    if (this->role_ == FIREPLACE_ROLE_REFLECTION) {
      role_gain *= 0.68f;
      red_gain *= 0.82f;
      green_gain *= 0.92f;
      white_gain *= 1.20f;
    }
    const float total = role_gain * slider_gain;
    return Color(scale_u8(color.r, total * red_gain), scale_u8(color.g, total * green_gain),
                 scale_u8(color.b, total), scale_u8(color.w, total * white_gain));
  }

  // Role-neutral variant: applies only the configured gains and the global
  // slider, but NOT the REFLECTION colour/level profile. The calibration
  // palette uses this so both strips show the exact central FIRE_PALETTE raw
  // stops, independent of a strip's direct/reflection role.
  Color raw_color_(Color color, float slider_gain) const {
    const float total = this->output_gain_ * slider_gain;
    return Color(scale_u8(color.r, total), scale_u8(color.g, total * this->green_gain_),
                 scale_u8(color.b, total), scale_u8(color.w, total * this->white_gain_));
  }

  RawPixelOutput *raw_output_{nullptr};
  FireplaceRole role_{FIREPLACE_ROLE_DIRECT};
  uint32_t update_interval_ms_{20};
  float intensity_{1.0f};
  float output_gain_{1.0f};
  float green_gain_{1.0f};
  float white_gain_{1.0f};
  uint32_t last_run_ms_{0};
};

class FireplaceEffect : public RawFireEffectBase {
 public:
  explicit FireplaceEffect(const char *name) : RawFireEffectBase(name) {}

  void start() override {
    this->last_run_ms_ = 0;
    this->initialized_ = false;
  }

  void apply(light::AddressableLight &it, const Color &current_color) override {
    (void) current_color;
    if (this->raw_output_ == nullptr)
      return;

    const uint32_t now = millis();
    if (this->last_run_ms_ != 0 && now - this->last_run_ms_ < this->update_interval_ms_)
      return;
    this->last_run_ms_ = now;

    const int32_t count = std::min<int32_t>(it.size(), this->raw_output_->raw_pixel_count());
    if (count <= 0)
      return;

    SHARED_FIRE_SIGNAL.update(now);
    if (!this->initialized_)
      this->initialize_zones_(count, now);
    this->update_zones_(count, now);

    const float slider_gain = this->slider_gain_();
    const float fast = SHARED_FIRE_SIGNAL.fast();
    const float medium = SHARED_FIRE_SIGNAL.medium();
    const float slow = SHARED_FIRE_SIGNAL.slow();
    // Schneller Anteil (fast) mit deutlich geringerem Gewicht; die fehlende
    // Energie geht auf die ruhigen slow/medium-Anteile -> Gesamthelligkeit
    // bleibt aehnlich, aber das hochfrequente Zappeln wird schwaecher.
    const float global = std::clamp(0.30f + 0.28f * slow + 0.24f * medium + 0.16f * fast, 0.0f, 1.0f);

    for (int32_t i = 0; i < count; i++) {
      float local = 0.0f;
      for (uint8_t z = 0; z < this->zone_count_; z++) {
        const Zone &zone = this->zones_[z];
        const float distance = std::fabs(static_cast<float>(i) - zone.center);
        if (distance >= zone.width)
          continue;
        float envelope = 1.0f - distance / zone.width;
        envelope *= envelope;
        const float wave = 0.78f + 0.22f * std::sin(SHARED_FIRE_SIGNAL.phase() * 0.72f + zone.phase +
                                                     static_cast<float>(i) * 0.075f);
        local += zone.level * envelope * wave;
      }
      local = std::clamp(local, 0.0f, 1.25f);

      float visual_level;
      if (this->role_ == FIREPLACE_ROLE_REFLECTION) {
        // Reflected room light follows the same fire but is smoother and has
        // less local contrast than the direct strip.
        visual_level = 0.10f + this->intensity_ * (0.62f * global + 0.22f * std::min(local, 1.0f));
      } else {
        visual_level = 0.055f + this->intensity_ * (0.30f * global + 0.70f * std::min(local, 1.0f));
        // Direkte HF-Helligkeitsmodulation halbiert (war 0.86 + 0.14*fast):
        // weniger sichtbares schnelles Blitzen auf dem direkten Strip.
        visual_level *= 0.93f + 0.07f * fast;
      }
      visual_level = std::clamp(visual_level, 0.0f, 1.0f);

      const uint8_t level = static_cast<uint8_t>(std::lround(visual_level * 255.0f));
      this->raw_output_->set_pixel_raw(i, this->profile_color_(lookup_fire_palette(level), slider_gain));
    }
    for (int32_t i = count; i < this->raw_output_->raw_pixel_count(); i++)
      this->raw_output_->set_pixel_raw(i, Color::BLACK);

    it.schedule_show();
  }

 protected:
  struct Zone {
    float center{0.0f};
    float width{8.0f};
    float level{0.5f};
    float target{0.5f};
    float phase{0.0f};
    float drift{0.0f};
    uint32_t next_target_ms{0};
  };

  void initialize_zones_(int32_t count, uint32_t now) {
    this->zone_count_ = static_cast<uint8_t>(std::clamp<int32_t>(count / 15, 5, MAX_ZONES));
    const float spacing = static_cast<float>(count) / static_cast<float>(this->zone_count_);
    for (uint8_t i = 0; i < this->zone_count_; i++) {
      Zone &zone = this->zones_[i];
      zone.center = (static_cast<float>(i) + 0.5f) * spacing + this->rng_.range(-0.22f, 0.22f) * spacing;
      zone.width = spacing * this->rng_.range(0.75f, 1.35f);
      zone.level = this->rng_.range(0.35f, 0.85f);
      zone.target = this->rng_.range(0.35f, 1.0f);
      zone.phase = this->rng_.range(0.0f, 6.28318530718f);
      zone.drift = this->rng_.range(-0.018f, 0.018f);
      zone.next_target_ms = now + 80U + (this->rng_.next() % 240U);
    }
    this->initialized_ = true;
  }

  void update_zones_(int32_t count, uint32_t now) {
    for (uint8_t i = 0; i < this->zone_count_; i++) {
      Zone &zone = this->zones_[i];
      if (static_cast<int32_t>(now - zone.next_target_ms) >= 0) {
        zone.target = this->rng_.range(0.25f, 1.0f);
        zone.drift += this->rng_.range(-0.010f, 0.010f);
        zone.drift = std::clamp(zone.drift, -0.045f, 0.045f);
        zone.next_target_ms = now + 70U + (this->rng_.next() % 260U);
      }
      zone.level += (zone.target - zone.level) * 0.16f;
      zone.center += zone.drift;
      if (zone.center < 0.0f) {
        zone.center = 0.0f;
        zone.drift = std::fabs(zone.drift);
      } else if (zone.center > static_cast<float>(count - 1)) {
        zone.center = static_cast<float>(count - 1);
        zone.drift = -std::fabs(zone.drift);
      }
    }
  }

  static constexpr uint8_t MAX_ZONES = 10;
  Zone zones_[MAX_ZONES]{};
  uint8_t zone_count_{0};
  bool initialized_{false};
  XorShift32 rng_{0x51C8F17EU};
};

class FireplacePaletteEffect : public RawFireEffectBase {
 public:
  explicit FireplacePaletteEffect(const char *name) : RawFireEffectBase(name) {}

  void start() override { this->last_run_ms_ = 0; }

  void apply(light::AddressableLight &it, const Color &current_color) override {
    (void) current_color;
    if (this->raw_output_ == nullptr)
      return;

    const uint32_t now = millis();
    if (this->last_run_ms_ != 0 && now - this->last_run_ms_ < 100U)
      return;
    this->last_run_ms_ = now;

    const int32_t count = std::min<int32_t>(it.size(), this->raw_output_->raw_pixel_count());
    const float slider_gain = this->slider_gain_();
    for (int32_t i = 0; i < count; i++) {
      const size_t stop = std::min<size_t>((static_cast<size_t>(i) * FIRE_PALETTE_SIZE) /
                                               static_cast<size_t>(std::max<int32_t>(count, 1)),
                                           FIRE_PALETTE_SIZE - 1);
      // raw_color_ (not profile_color_): show the untransformed central palette
      // on every strip, so this stays a true calibration reference.
      this->raw_output_->set_pixel_raw(i, this->raw_color_(FIRE_PALETTE[stop].color, slider_gain));
    }
    // Clear any pixels beyond the shared range (parity with the dynamic effect;
    // no-op while output and light size match, but keeps the contract explicit).
    for (int32_t i = count; i < this->raw_output_->raw_pixel_count(); i++)
      this->raw_output_->set_pixel_raw(i, Color::BLACK);
    it.schedule_show();
  }
};

}  // namespace esphome::fireplace_effect
