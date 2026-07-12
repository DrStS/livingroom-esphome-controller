#include "living_room.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome::living_room {
static const char *const TAG = "living_room";

void LivingRoom::setup() {
  if (r_en_pin_) { r_en_pin_->setup(); r_en_pin_->digital_write(false); }
  if (l_en_pin_) { l_en_pin_->setup(); l_en_pin_->digital_write(false); }
  if (encoder_a_pin_) encoder_a_pin_->setup();
  if (encoder_b_pin_) encoder_b_pin_->setup();
  if (endstop_top_pin_) endstop_top_pin_->setup();
  if (endstop_bottom_pin_) endstop_bottom_pin_->setup();
  stop_motor_outputs();
  cfg_.top_position_pulses = std::max<int32_t>(1, cfg_.top_position_pulses);
  lift_.configure(cfg_);
  pref_ = global_preferences->make_preference<PersistedState>(0x4C524331);
  load_persistence();
  last_enc_a_ = encoder_a_pin_ ? encoder_a_pin_->digital_read() : false;
  last_enc_b_ = encoder_b_pin_ ? encoder_b_pin_->digital_read() : false;
  ESP_LOGI(TAG, "LivingRoom setup: top=%ld homed=%s pos=%ld",
           static_cast<long>(cfg_.top_position_pulses), persisted_.homed ? "true" : "false",
           static_cast<long>(persisted_.position_pulses));
}

void LivingRoom::loop() {
  const auto now = millis();
  update_encoder();
  if (now - last_tick_ms_ < 10) return;
  last_tick_ms_ = now;
  const lr::TickInputs in{.now_ms = now, .top_endstop = read_top_endstop(), .bottom_endstop = read_bottom_endstop()};
  const auto cmd = lift_.tick(in);
  apply_motor(cmd);
  if (lift_.was_persistence_dirty() || (lift_.is_moving() && (now - last_persist_ms_ > 2000))) {
    save_persistence(lift_.was_persistence_dirty());
    last_persist_ms_ = now;
  }
}

void LivingRoom::command_open() { lift_.command_open(); }
void LivingRoom::command_close() { lift_.command_close(); }
void LivingRoom::command_stop() { lift_.command_stop(); }
void LivingRoom::command_set_position(float p) { lift_.command_set_position(p); }
void LivingRoom::command_reference() { lift_.command_reference(); }
void LivingRoom::command_clear_fault() { lift_.command_clear_fault(); }
float LivingRoom::cover_position() const { return position_percent() / 100.0f; }
float LivingRoom::position_percent() const { return lift_.position_percent(); }
int32_t LivingRoom::position_pulses() const { return lift_.position_pulses(); }
bool LivingRoom::homed() const { return lift_.homed(); }
bool LivingRoom::faulted() const { return lift_.faulted(); }

void LivingRoom::apply_motor(const lr::MotorCommand &cmd) {
  if (cmd.direction == lr::Direction::Stop || cmd.duty <= 0.0f) { stop_motor_outputs(); return; }
  const float duty = std::clamp(cmd.duty, 0.0f, 1.0f);
  if (r_en_pin_) r_en_pin_->digital_write(true);
  if (l_en_pin_) l_en_pin_->digital_write(true);
  if (cmd.direction == lr::Direction::Up) {
    if (rpwm_) rpwm_->set_level(duty);
    if (lpwm_) lpwm_->set_level(0.0f);
  } else {
    if (rpwm_) rpwm_->set_level(0.0f);
    if (lpwm_) lpwm_->set_level(duty);
  }
}

void LivingRoom::stop_motor_outputs() {
  if (rpwm_) rpwm_->set_level(0.0f);
  if (lpwm_) lpwm_->set_level(0.0f);
  if (r_en_pin_) r_en_pin_->digital_write(false);
  if (l_en_pin_) l_en_pin_->digital_write(false);
}

void LivingRoom::load_persistence() {
  PersistedState loaded{};
  if (pref_.load(&loaded) && loaded.magic == persisted_.magic) persisted_ = loaded;
  if (persisted_.was_moving) lift_.restore(persisted_.position_pulses, false, true);
  else lift_.restore(persisted_.position_pulses, persisted_.homed, false);
}

void LivingRoom::save_persistence(bool force) {
  PersistedState next{};
  next.position_pulses = lift_.position_pulses();
  next.homed = lift_.homed();
  next.was_moving = lift_.is_moving();
  if (!force && next.position_pulses == persisted_.position_pulses && next.homed == persisted_.homed && next.was_moving == persisted_.was_moving) return;
  persisted_ = next;
  pref_.save(&persisted_);
}

void LivingRoom::update_encoder() {
  if (!encoder_a_pin_ || !encoder_b_pin_) return;
  const bool a = encoder_a_pin_->digital_read();
  const bool b = encoder_b_pin_->digital_read();
  if (a == last_enc_a_ && b == last_enc_b_) return;
  const uint8_t prev = (static_cast<uint8_t>(last_enc_a_) << 1) | static_cast<uint8_t>(last_enc_b_);
  const uint8_t curr = (static_cast<uint8_t>(a) << 1) | static_cast<uint8_t>(b);
  const uint8_t transition = (prev << 2) | curr;
  switch (transition) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000: lift_.on_encoder_delta(+1); break;
    case 0b0010: case 0b1011: case 0b1101: case 0b0100: lift_.on_encoder_delta(-1); break;
    default: break;
  }
  last_enc_a_ = a;
  last_enc_b_ = b;
}

bool LivingRoom::read_top_endstop() const { return endstop_top_pin_ ? endstop_top_pin_->digital_read() : false; }
bool LivingRoom::read_bottom_endstop() const { return endstop_bottom_pin_ ? endstop_bottom_pin_->digital_read() : false; }
}  // namespace esphome::living_room
