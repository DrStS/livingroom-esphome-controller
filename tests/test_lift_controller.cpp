#include "living_room/core/lift_controller.h"
#include <cassert>
#include <iostream>
using namespace esphome::living_room::lr;
int main() {
  LiftController lift; LiftConfig cfg; cfg.top_position_pulses = 940; cfg.ramp_ms = 0; lift.configure(cfg); lift.restore(0, true, false);
  lift.command_set_position(0.5f);
  auto cmd = lift.tick({.now_ms = 1, .top_endstop = false, .bottom_endstop = false});
  assert(cmd.direction == Direction::Up);
  for (int i=0; i<470; ++i) lift.on_encoder_delta(+1);
  cmd = lift.tick({.now_ms = 1000, .top_endstop = false, .bottom_endstop = false});
  assert(lift.position_pulses() == 470);
  lift.command_stop();
  cmd = lift.tick({.now_ms = 1010, .top_endstop = false, .bottom_endstop = false});
  assert(cmd.direction == Direction::Stop);
  std::cout << "LiftController smoke test passed\n";
  return 0;
}
