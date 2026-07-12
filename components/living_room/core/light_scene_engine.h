#pragma once
#include <cstdint>
#include <span>
namespace esphome::living_room::lr {
struct Rgbw { uint8_t r{}; uint8_t g{}; uint8_t b{}; uint8_t w{}; };
enum class SceneId : uint8_t { Manual, Cinema, Fireplace, Aurora, Sunset, GlassTrace, LiftShow, Maintenance, Fault };
struct LightFrame { std::span<Rgbw> sideboard; std::span<Rgbw> glass_edge; std::span<Rgbw> inner_diffuser; };
class LightSceneEngine final {
 public:
  void set_scene(SceneId scene) { scene_ = scene; }
  void set_intensity(float value_0_to_1) { intensity_ = value_0_to_1; }
  void set_speed(float value_0_to_1) { speed_ = value_0_to_1; }
  void set_lift_position(float percent) { lift_percent_ = percent; }
  void render(LightFrame frame, uint32_t now_ms) {
    switch (scene_) {
      case SceneId::Cinema: fill(frame.sideboard,{25,10,2,24}); fill(frame.glass_edge,{}); fill(frame.inner_diffuser,{18,7,1,18}); break;
      case SceneId::Maintenance: fill(frame.sideboard,{220,220,220,255}); fill(frame.glass_edge,{220,220,220,255}); fill(frame.inner_diffuser,{220,220,220,255}); break;
      case SceneId::Fault: pulse_red(frame.glass_edge, now_ms); fill(frame.sideboard,{}); fill(frame.inner_diffuser,{}); break;
      default: break;
    }
  }
 private:
  static void fill(std::span<Rgbw> s, Rgbw c) { for (auto &p : s) p = c; }
  static void pulse_red(std::span<Rgbw> s, uint32_t now_ms) { const uint8_t v = ((now_ms / 500) % 2) ? 80 : 5; for (auto &p : s) p = {v,0,0,0}; }
  SceneId scene_{SceneId::Manual}; float intensity_{0.6f}; float speed_{0.35f}; float lift_percent_{0.0f};
};
}  // namespace esphome::living_room::lr
