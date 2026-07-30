#pragma once

// Produktionsregelung fuer den TV-Lift auf einem eigenen 1-ms-FreeRTOS-Task.
// Richtungskonvention (logisch, keine Aussage ueber die reale Verdrahtung):
// positive Encoderposition und LPWM = Oeffnen/Aufwaerts; 0 = geschlossen/unten;
// max_position = offen/oben. Die reale Motorzuordnung wurde beim manuellen
// Richtungstest bestaetigt; invert_direction bleibt fuer den Encoder false.
//
// REGELKONZEPT (encoderbasierte Geschwindigkeitsregelung):
// Der Duty wird nicht mehr direkt aus dem Positionsfehler gerechnet. Stattdessen
// fuehrt ein Trapezprofil einen Geschwindigkeits-Sollwert v_cmd (Encoder-Counts
// pro Sekunde), und ein PI-Regler stellt den Duty so, dass die gemessene
// Encodergeschwindigkeit diesem Sollwert folgt. Der I-Anteil kompensiert
// Gewicht, Reibung und Fahrtrichtung selbsttaetig -- Duty ist nur Stellgroesse.
// Nur die Feinpositionierung im letzten Count-Bereich nutzt weiterhin den
// bewaehrten Pulsbetrieb, da eine Geschwindigkeitsregelung dort sinnlos ist.

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/output/binary_output.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/pcnt_quadrature/pcnt_quadrature.h"

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome::lift_motor {

/** Im NVS gesicherter Referenzzustand.
 *
 * Zweck: nach einem Stromausfall oder OTA soll der Lift seine Position kennen,
 * ohne neu referenzieren zu muessen. Das ist zulaessig, weil die Mechanik
 * selbsthemmend ist (Getriebe 48:1 auf Trapezgewinde): ohne Strom bleibt der
 * Lift stehen, die Position aendert sich also nicht. Bei Handverstellung im
 * spannungslosen Zustand ist der Wert falsch -- dafuer gibt es den Schalter
 * "Lift Referenz" zum Verwerfen.
 *
 * AUFBAU BEWUSST MIT FESTER GROESSE UND VERSIONSFELD:
 * ESPHome vergleicht beim Laden die Datensatzlaenge und verwirft alles, was
 * nicht exakt passt. Ein Datensatz variabler Groesse verliert deshalb bei jeder
 * Erweiterung die gespeicherte Referenz -- genau das ist beim Ergaenzen von
 * "moving" passiert. Mit fester Groesse plus reserve[] bleibt die Laenge auch
 * beim Hinzufuegen kuenftiger Felder gleich; die Version sagt, welche Felder
 * gueltig sind. Kuenftige Erweiterungen kosten damit keine Referenz mehr:
 * neues Feld aus reserve[] entnehmen und LIFT_PERSIST_VERSION erhoehen.
 */
struct LiftPersistedState {
  /** Kennung des Datensatztyps. Bleibt ueber alle Versionen KONSTANT. */
  uint32_t magic;
  /** Layoutversion. Aeltere Versionen werden gelesen, fehlende Felder bekommen
   * ihren Standardwert. Neuere als die bekannte Version werden abgelehnt. */
  uint16_t version;
  /** Encoderstand in Counts (ab Version 1). */
  int64_t position;
  /** War der Lift referenziert? (ab Version 1) */
  bool homed;
  /** War beim letzten Schreiben eine Fahrt aktiv? (ab Version 1)
   *
   * Waehrend der Fahrt wird nicht gespeichert, der Eintrag stammt dann vom
   * Fahrtbeginn. Ist dieses Flag beim Start gesetzt, wurde die Fahrt
   * unterbrochen und der Stand ist veraltet -- die Referenz wird dann bewusst
   * verworfen statt eine falsche Position zu uebernehmen.
   */
  bool moving;
  /** Platz fuer kuenftige Felder, damit die Datensatzgroesse stabil bleibt.
   * Wird beim Schreiben immer genullt. */
  uint8_t reserved[16];
} __attribute__((packed));

enum LiftState : uint8_t {
  LIFT_IDLE = 0,
  LIFT_MOVING = 1,
  LIFT_REACHED = 2,
  LIFT_STALL = 3,
  LIFT_TIMEOUT = 4,
  LIFT_FAULT = 5,
  LIFT_LIMIT = 6,
  LIFT_MANUAL = 7,
};

class LiftMotor : public Component {
 public:
  void set_rpwm(output::FloatOutput *out) { this->rpwm_ = out; }
  void set_lpwm(output::FloatOutput *out) { this->lpwm_ = out; }
  void set_enable(output::BinaryOutput *out) { this->enable_ = out; }
  void set_encoder(pcnt_quadrature::PcntQuadratureSensor *encoder) { this->encoder_ = encoder; }
  void set_min_position(int64_t value) { this->min_position_ = value; }
  void set_max_position(int64_t value) { this->max_position_ = value; }

  void set_period_ms(uint32_t v) { this->period_ms_ = v; }
  void set_task_core(int v) { this->task_core_ = v; }
  void set_task_priority(uint32_t v) { this->task_priority_ = v; }
  void set_tolerance(int32_t v) { this->tolerance_ = v; }
  void set_duty_min_up(float v) { this->duty_min_up_ = v; }
  void set_duty_min_down(float v) { this->duty_min_down_ = v; }
  void set_duty_max(float v) { this->duty_max_ = v; }
  void set_error_full(float v) { this->error_full_ = v; }
  void set_fine_window(int32_t v) { this->fine_window_ = v; }
  void set_pulse_period(uint32_t v) { this->pulse_period_ = v; }
  void set_brake_ms(uint32_t v) { this->brake_ms_ = v; }
  void set_stall_ms(uint32_t v) { this->stall_ms_ = v; }
  void set_timeout_ms(uint32_t v) { this->timeout_ms_ = v; }
  void set_manual_timeout_ms(uint32_t v) { this->manual_timeout_ms_ = v; }

  // --- Trapezprofil (alle Werte in Encoder-Counts bzw. Counts/s) ---
  void set_max_speed(float v) { this->max_speed_ = v; }
  void set_accel(float v) { this->accel_ = v; }
  void set_decel(float v) { this->decel_ = v; }
  void set_approach_speed(float v) { this->approach_speed_ = v; }
  // --- PI-Geschwindigkeitsregler und Messfilter ---
  void set_speed_kp(float v) { this->speed_kp_ = v; }
  void set_speed_ki(float v) { this->speed_ki_ = v; }
  void set_speed_window_ms(uint32_t v) { this->speed_window_us_ = v * 1000U; }
  void set_speed_filter(float v) { this->speed_filter_ = v; }
  // --- Persistenz der Position/Referenz im NVS ---
  void set_persist_position(bool v) { this->persist_position_ = v; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool start_task();
  bool task_running() const { return this->task_ != nullptr; }

  // Kommandos, nur aus dem ESPHome-Mainloop aufrufen.
  bool goto_position(int64_t target);
  bool start_manual(int direction);
  /** Geregelte Relativfahrt um genau delta Counts.
   *
   * Zweck: Kalibrierung der Umrechnung Counts -> mm. Der Regler haelt die
   * Count-Distanz exakt ein, sodass die zugehoerige Strecke am Hub gemessen
   * werden kann. Ohne Referenz erlaubt (es gibt dann keinen absoluten Bezug),
   * mit Referenz gelten die Softlimits weiterhin.
   */
  bool move_relative(int64_t delta);
  void stop();
  bool prepare_reference();
  bool set_reference(int64_t position = 0);
  /** Verwirft die gespeicherte Referenz (z. B. nach Handverstellung im
   * spannungslosen Zustand). Danach ist neues Referenzieren noetig. */
  void clear_reference();

  int64_t position() const { return this->pos_.load(std::memory_order_relaxed); }
  int64_t target() const { return this->target_.load(std::memory_order_relaxed); }
  int64_t error() const { return this->target() - this->position(); }
  int64_t min_position() const { return this->min_position_; }
  int64_t max_position() const { return this->max_position_; }
  uint8_t state() const { return this->state_.load(std::memory_order_relaxed); }
  bool busy() const {
    const uint8_t current = this->state();
    return current == LIFT_MOVING || current == LIFT_MANUAL;
  }
  bool homed() const { return this->homed_.load(std::memory_order_relaxed); }
  bool reference_pending() const { return this->reference_pending_.load(std::memory_order_relaxed); }
  int motion_direction() const { return this->motion_direction_.load(std::memory_order_relaxed); }
  float duty() const { return this->duty_.load(std::memory_order_relaxed); }
  // Gefilterte, vorzeichenbehaftete Encodergeschwindigkeit in Counts/s
  // (positiv = Oeffnen/Aufwaerts). Diagnosewert, jederzeit lesbar.
  float speed() const { return this->speed_.load(std::memory_order_relaxed); }
  // Aktueller Geschwindigkeits-Sollbetrag des Trapezprofils in Counts/s.
  float speed_command() const { return this->speed_cmd_.load(std::memory_order_relaxed); }
  uint32_t worst_period_us() const { return this->worst_period_us_.load(std::memory_order_relaxed); }
  uint32_t last_move_ms() const { return this->last_move_ms_.load(std::memory_order_relaxed); }
  uint32_t cycles() const { return this->cycles_.load(std::memory_order_relaxed); }
  uint32_t overruns() const { return this->overruns_.load(std::memory_order_relaxed); }
  void reset_worst_period() {
    this->worst_period_us_.store(0, std::memory_order_relaxed);
    this->cycles_.store(0, std::memory_order_relaxed);
    this->overruns_.store(0, std::memory_order_relaxed);
  }
  const char *state_name() const;

 protected:
  static void task_trampoline(void *arg);
  void task_loop();
  void drive_(int dir, float duty);
  void brake_();
  void coast_off_();
  void observe_encoder_(int64_t position, uint32_t now_ms);
  // Geschwindigkeitsmessung ueber ein gleitendes Zeitfenster plus EMA-Filter.
  // Wird ausschliesslich aus dem Regeltask aufgerufen.
  void update_speed_(int64_t position, int64_t now_us);
  // Vorsteuerung: richtungsabhaengiger Mindest-Duty als Startpunkt des Reglers.
  float duty_floor_(int dir) const { return dir > 0 ? this->duty_min_up_ : this->duty_min_down_; }
  // Bremsweggefuehrte Geschwindigkeitsgrenze: v = sqrt(2 * decel * Restweg).
  float brake_limit_speed_(float distance) const;
  /** Schreibt Position und Referenzstatus ins NVS. Nur aus dem Mainloop!
   *
   * Bewusst NICHT periodisch aufrufen: jeder Aufruf schreibt echt ins Flash
   * (inklusive sync) und kostet Schreibzyklen; ausserdem schaltet ein
   * NVS-Schreibvorgang kurz den Flash-Cache ab und haelt damit den Regeltask
   * auf Kern 1 an. Aufgerufen wird nur bei seltenen Ereignissen: Fahrtbeginn,
   * Fahrtende, Referenz setzen und Referenz verwerfen. Das ergibt zwei
   * Schreibvorgaenge pro Fahrt.
   */
  void save_state_();
  /** Uebernimmt die im NVS gefundene Position in den Encoder.
   *
   * Bewusst NICHT aus setup() aufgerufen, sondern beim ersten loop(): das
   * Setzen der Encoderposition loest dort ein publish_state() mit Filterkette
   * und Callbacks aus. Waehrend der Startphase ist das riskant -- genau daran
   * ist die Firmware haengengeblieben, sobald erstmals eine Position ungleich 0
   * gespeichert war. Im laufenden Betrieb ist derselbe Aufruf unkritisch.
   */
  void apply_restored_position_();
  bool target_in_limits_(int64_t target) const {
    return target >= this->min_position_ && target <= this->max_position_;
  }

  output::FloatOutput *rpwm_{nullptr};
  output::FloatOutput *lpwm_{nullptr};
  output::BinaryOutput *enable_{nullptr};
  pcnt_quadrature::PcntQuadratureSensor *encoder_{nullptr};

  int64_t min_position_{0};
  int64_t max_position_{940};
  uint32_t period_ms_{1};
  int task_core_{1};
  uint32_t task_priority_{10};
  int32_t tolerance_{2};
  float duty_min_up_{0.14f};
  float duty_min_down_{0.12f};
  float duty_max_{0.35f};
  float error_full_{12.0f};
  int32_t fine_window_{6};
  uint32_t pulse_period_{4};
  uint32_t brake_ms_{200};
  uint32_t stall_ms_{500};
  uint32_t timeout_ms_{4000};
  // Manuelle Fahrten brauchen die volle Fahrzeit; der Stall-Schutz bleibt die
  // eigentliche Sicherung gegen Druecken gegen einen Anschlag.
  uint32_t manual_timeout_ms_{30000};

  // Trapezprofil. Referenz: bei 100 Prozent Duty wurden rund 1400 Counts/s
  // gemessen, die Defaults liegen bewusst deutlich darunter.
  float max_speed_{400.0f};
  float accel_{800.0f};
  float decel_{800.0f};
  float approach_speed_{60.0f};
  // PI-Regler. Beide Werte sind konservativ vorbelegt und muessen an der realen
  // Mechanik kalibriert werden (siehe dump_config und README-Kalibrierung).
  float speed_kp_{0.0006f};
  float speed_ki_{0.004f};
  uint32_t speed_window_us_{20000};
  float speed_filter_{0.35f};

  TaskHandle_t task_{nullptr};
  portMUX_TYPE output_mux_ = portMUX_INITIALIZER_UNLOCKED;

  // Nur der Regeltask nutzt diese Messzustaende -- kein Zugriff aus dem Mainloop.
  int64_t speed_ref_pos_{0};
  int64_t speed_ref_us_{0};
  float speed_filtered_{0.0f};

  std::atomic<int64_t> pos_{0};
  std::atomic<int64_t> target_{0};
  std::atomic<int64_t> last_observed_pos_{0};
  std::atomic<uint32_t> last_encoder_change_ms_{0};
  std::atomic<uint32_t> cmd_epoch_{0};
  std::atomic<bool> cmd_move_{false};
  std::atomic<int> cmd_manual_direction_{0};
  // Kennzeichnet eine Relativfahrt (Kalibrierung): Positionsregelung ohne
  // Pflicht zur Referenzierung.
  std::atomic<bool> cmd_relative_{false};
  std::atomic<uint8_t> state_{LIFT_IDLE};
  std::atomic<float> duty_{0.0f};
  std::atomic<float> speed_{0.0f};
  std::atomic<float> speed_cmd_{0.0f};
  std::atomic<int> motion_direction_{0};
  std::atomic<bool> motion_allowed_{false};
  std::atomic<bool> homed_{false};
  std::atomic<bool> reference_pending_{false};
  std::atomic<bool> task_healthy_{false};
  std::atomic<uint32_t> hb_{0};
  std::atomic<uint32_t> main_hb_{0};
  std::atomic<uint32_t> worst_period_us_{0};
  std::atomic<uint32_t> last_move_ms_{0};
  std::atomic<uint32_t> cycles_{0};
  std::atomic<uint32_t> overruns_{0};
  std::atomic<float> stall_duty_{0.0f};
  std::atomic<float> stall_speed_{0.0f};

  uint32_t last_seen_hb_{0};
  uint32_t last_hb_change_ms_{0};
  uint8_t last_logged_state_{LIFT_IDLE};

  // --- Persistenz (nur Mainloop) ---
  bool persist_position_{true};
  ESPPreferenceObject pref_;
  bool pref_ready_{false};
  /** Im NVS gefundene Position, die noch uebernommen werden muss. Gesetzt in
   * setup(), angewendet im ersten loop(). Bis dahin gilt der Lift als
   * unreferenziert, absolute Fahrten sind also gesperrt. */
  int64_t restore_position_{0};
  bool restore_pending_{false};
  int64_t last_saved_position_{0};
  bool last_saved_homed_{false};
  bool last_saved_moving_{false};
  bool saved_valid_{false};
};

}  // namespace esphome::lift_motor
