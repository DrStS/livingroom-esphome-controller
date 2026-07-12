#pragma once
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/preferences.h"
#include "esphome/components/output/float_output.h"
#include "core/lift_controller.h"

namespace esphome::living_room {

class LivingRoom final : public Component {
 public:
  void set_rpwm_output(output::FloatOutput *out) { rpwm_ = out; }
  void set_lpwm_output(output::FloatOutput *out) { lpwm_ = out; }
  void set_r_en_pin(GPIOPin *pin) { r_en_pin_ = pin; }
  void set_l_en_pin(GPIOPin *pin) { l_en_pin_ = pin; }
  void set_encoder_a_pin(GPIOPin *pin) { encoder_a_pin_ = pin; }
  void set_encoder_b_pin(GPIOPin *pin) { encoder_b_pin_ = pin; }
  void set_endstop_top_pin(GPIOPin *pin) { endstop_top_pin_ = pin; }
  void set_endstop_bottom_pin(GPIOPin *pin) { endstop_bottom_pin_ = pin; }
  void set_top_position_pulses(int32_t value) { cfg_.top_position_pulses = value; }
  void set_slow_zone_pulses(int32_t value) { cfg_.slow_zone_pulses = value; }
  void set_ramp_ms(uint32_t value) { cfg_.ramp_ms = value; }
  void set_cruise_duty_up(float value) { cfg_.cruise_duty_up = value; }
  void set_cruise_duty_down(float value) { cfg_.cruise_duty_down = value; }
  void set_homing_duty(float value) { cfg_.homing_duty = value; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void command_open();
  void command_close();
  void command_stop();
  void command_set_position(float position_0_to_1);
  void command_reference();
  void command_clear_fault();

  float cover_position() const;
  float position_percent() const;
  int32_t position_pulses() const;
  bool homed() const;
  bool faulted() const;

 private:
  struct PersistedState {
    uint32_t magic{0x534C5243};
    int32_t position_pulses{0};
    bool homed{false};
    bool was_moving{false};
  };

  void apply_motor(const lr::MotorCommand &cmd);
  void stop_motor_outputs();
  void load_persistence();
  void save_persistence(bool force);
  void update_encoder();
  bool read_top_endstop() const;
  bool read_bottom_endstop() const;

  output::FloatOutput *rpwm_{nullptr};
  output::FloatOutput *lpwm_{nullptr};
  GPIOPin *r_en_pin_{nullptr};
  GPIOPin *l_en_pin_{nullptr};
  GPIOPin *encoder_a_pin_{nullptr};
  GPIOPin *encoder_b_pin_{nullptr};
  GPIOPin *endstop_top_pin_{nullptr};
  GPIOPin *endstop_bottom_pin_{nullptr};
  lr::LiftConfig cfg_{};
  lr::LiftController lift_{};
  ESPPreferenceObject pref_;
  PersistedState persisted_{};
  uint32_t last_tick_ms_{0};
  uint32_t last_persist_ms_{0};
  bool last_enc_a_{false};
  bool last_enc_b_{false};
};

}  // namespace esphome::living_room
