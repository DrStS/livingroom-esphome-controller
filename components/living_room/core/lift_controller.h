#pragma once
#include <algorithm>
#include <cstdint>

namespace esphome::living_room::lr {

enum class Direction : uint8_t { Stop, Up, Down };
enum class LiftState : uint8_t { Boot, Idle, MovingUp, MovingDown, HomingDown, Fault };
enum class FaultCode : uint8_t { None, NeedsReference, Timeout, TopEndstop, BottomEndstop, EncoderMissing, InvalidCommand };

struct LiftConfig {
  int32_t top_position_pulses{940};
  int32_t slow_zone_pulses{80};
  uint32_t ramp_ms{800};
  float cruise_duty_up{0.72f};
  float cruise_duty_down{0.62f};
  float homing_duty{0.30f};
  uint32_t max_move_ms{180000};
  uint32_t encoder_missing_ms{2500};
};

struct TickInputs { uint32_t now_ms{}; bool top_endstop{}; bool bottom_endstop{}; };
struct MotorCommand { Direction direction{Direction::Stop}; float duty{0.0f}; };

class LiftController final {
 public:
  void configure(LiftConfig cfg);
  void restore(int32_t position_pulses, bool homed, bool unsafe_after_power_loss);
  void command_open();
  void command_close();
  void command_set_position(float position_0_to_1);
  void command_stop();
  void command_reference();
  void command_clear_fault();
  void on_encoder_delta(int delta);
  [[nodiscard]] MotorCommand tick(const TickInputs &in);
  [[nodiscard]] int32_t position_pulses() const { return position_pulses_; }
  [[nodiscard]] float position_percent() const;
  [[nodiscard]] bool homed() const { return homed_; }
  [[nodiscard]] bool faulted() const { return state_ == LiftState::Fault; }
  [[nodiscard]] bool is_moving() const;
  [[nodiscard]] bool was_persistence_dirty() const { return persistence_dirty_; }
  [[nodiscard]] FaultCode fault_code() const { return fault_code_; }
 private:
  void start_move_to(int32_t target);
  void set_fault(FaultCode code);
  void stop_clean();
  [[nodiscard]] float ramped_duty(uint32_t now_ms, float cruise_duty) const;
  [[nodiscard]] bool target_reached() const;
  LiftConfig cfg_{};
  LiftState state_{LiftState::Boot};
  FaultCode fault_code_{FaultCode::None};
  int32_t position_pulses_{0};
  int32_t target_pulses_{0};
  uint32_t move_start_ms_{0};
  uint32_t last_encoder_ms_{0};
  bool homed_{false};
  bool persistence_dirty_{false};
};
}  // namespace esphome::living_room::lr
