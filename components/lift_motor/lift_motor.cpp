#include "lift_motor.h"

#include "esphome/core/log.h"

#include <esp_timer.h>

#include <cmath>

namespace esphome::lift_motor {

static const char *const TAG = "lift_motor";
static constexpr uint32_t REFERENCE_STABLE_MS = 250;
// Kennung des NVS-Datensatzes. Bei Formataenderung hochzaehlen, damit alte
// Datensaetze nicht falsch interpretiert werden.
// Kennung des Datensatztyps. Bleibt ueber alle Versionen KONSTANT -- die
// Unterscheidung macht das Versionsfeld, nicht die Magic. Nur aendern, wenn ein
// vollstaendig anderes Format gespeichert wird.
static constexpr uint32_t PERSIST_MAGIC = 0x4C494654;  // "LIFT"

// Aktuelle Layoutversion von LiftPersistedState.
// Vorgehen bei einer Erweiterung: neues Feld aus reserved[] entnehmen, diese
// Version um 1 erhoehen und beim Laden fuer aeltere Versionen einen sinnvollen
// Standardwert setzen. Die Datensatzgroesse darf sich dabei NICHT aendern --
// dann bleibt die gespeicherte Referenz ueber das Update hinweg erhalten.
//   v1: position, homed, moving
static constexpr uint16_t PERSIST_VERSION = 1;

const char *LiftMotor::state_name() const {
  const uint8_t current = this->state();
  if (current == LIFT_IDLE && this->reference_pending())
    return "REFERENCE_PENDING";
  if (current == LIFT_IDLE && !this->homed())
    return "UNREFERENCED";

  switch (current) {
    case LIFT_IDLE:
      return "IDLE";
    case LIFT_MOVING:
      return "MOVING";
    case LIFT_REACHED:
      return "REACHED";
    case LIFT_STALL:
      return "STALL";
    case LIFT_TIMEOUT:
      return "TIMEOUT";
    case LIFT_FAULT:
      return "FAULT";
    case LIFT_LIMIT:
      return "LIMIT";
    case LIFT_MANUAL:
      return "MANUAL";
    default:
      return "?";
  }
}

void LiftMotor::observe_encoder_(int64_t position, uint32_t now_ms) {
  const int64_t previous = this->last_observed_pos_.exchange(position, std::memory_order_relaxed);
  if (position != previous)
    this->last_encoder_change_ms_.store(now_ms, std::memory_order_relaxed);
  this->pos_.store(position, std::memory_order_relaxed);
}

void LiftMotor::update_speed_(int64_t position, int64_t now_us) {
  // Erstaufruf: nur Referenz setzen, noch keine Geschwindigkeit berechenbar.
  if (this->speed_ref_us_ == 0) {
    this->speed_ref_us_ = now_us;
    this->speed_ref_pos_ = position;
    return;
  }
  const int64_t delta_us = now_us - this->speed_ref_us_;
  // Messfenster noch nicht voll -> Position weiter mitlaufen lassen, aber
  // keinen neuen Rohwert bilden (bei 1-ms-Takt wuerde die Quantisierung
  // des Encoders sonst massiv rauschen).
  if (delta_us < static_cast<int64_t>(this->speed_window_us_))
    return;
  if (delta_us <= 0) {  // Schutz gegen Division durch 0 / Zeitspruenge
    this->speed_ref_us_ = now_us;
    this->speed_ref_pos_ = position;
    return;
  }

  const float v_raw = static_cast<float>(position - this->speed_ref_pos_) * 1e6f /
                      static_cast<float>(delta_us);
  this->speed_ref_us_ = now_us;
  this->speed_ref_pos_ = position;

  float alpha = this->speed_filter_;
  if (alpha <= 0.0f)
    alpha = 0.01f;
  if (alpha > 1.0f)
    alpha = 1.0f;
  this->speed_filtered_ += alpha * (v_raw - this->speed_filtered_);
  this->speed_.store(this->speed_filtered_, std::memory_order_relaxed);
}

float LiftMotor::brake_limit_speed_(float distance) const {
  // v = sqrt(2 * a * s): schnellste Geschwindigkeit, aus der mit decel noch
  // punktgenau angehalten werden kann.
  if (distance <= 0.0f || this->decel_ <= 0.0f)
    return 0.0f;
  return sqrtf(2.0f * this->decel_ * distance);
}

void LiftMotor::setup() {
  this->motion_allowed_.store(false, std::memory_order_release);
  this->homed_.store(false, std::memory_order_relaxed);
  this->reference_pending_.store(false, std::memory_order_relaxed);
  this->state_.store(LIFT_IDLE, std::memory_order_relaxed);

  if (this->rpwm_ == nullptr || this->lpwm_ == nullptr || this->enable_ == nullptr) {
    ESP_LOGE(TAG, "rpwm/lpwm/enable sind Pflicht; sichere Ausgabe ohne Outputs nicht moeglich");
    this->mark_failed();
    return;
  }
  // Sobald die drei Outputs vorhanden sind, ist der erste aktive Zugriff immer
  // Coast/Aus. Taskstart und Referenzlogik koennen daraus keine Fahrt ableiten.
  this->coast_off_();

  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Encoder ist beim Lift-Setup nicht bereit");
    this->mark_failed();
    return;
  }
  if (this->max_position_ <= this->min_position_) {
    ESP_LOGE(TAG, "Ungueltige Positionsgrenzen: min=%lld max=%lld", (long long) this->min_position_,
             (long long) this->max_position_);
    this->mark_failed();
    return;
  }

  int64_t position = this->encoder_->get_position();

  // --- Referenz aus dem NVS wiederherstellen ---
  // Die Mechanik ist selbsthemmend, ohne Strom bewegt sich der Lift nicht. Der
  // vor dem Ausfall gespeicherte Stand ist deshalb nach dem Neustart weiterhin
  // gueltig, solange niemand von Hand verstellt hat.
  if (this->persist_position_) {
    this->pref_ = global_preferences->make_preference<LiftPersistedState>(fnv1_hash("lift_motor_state"));
    this->pref_ready_ = true;
    LiftPersistedState saved{};
    const bool loaded = this->pref_.load(&saved);
    if (loaded && saved.magic == PERSIST_MAGIC && saved.version > PERSIST_VERSION) {
      // Von einer neueren Firmware geschrieben: Felder unbekannt, nicht raten.
      ESP_LOGW(TAG, "NVS-Datensatz hat Version %u, diese Firmware kennt nur %u -- ignoriert",
               (unsigned) saved.version, (unsigned) PERSIST_VERSION);
    } else if (loaded && saved.magic == PERSIST_MAGIC) {
      const bool in_limits = saved.position >= this->min_position_ && saved.position <= this->max_position_;
      if (saved.moving) {
        // Die letzte Fahrt wurde nicht regulaer beendet (Spannungsverlust oder
        // Reset waehrend der Bewegung). Der gespeicherte Stand ist damit der
        // Wert VOM FAHRTBEGINN und nicht die tatsaechliche Position -- deshalb
        // bewusst keine Referenz uebernehmen.
        ESP_LOGW(TAG, "Letzte Fahrt wurde unterbrochen (gespeicherter Stand %lld ist veraltet). "
                      "Referenz verworfen -- bitte untere Endlage anfahren und neu referenzieren.",
                 (long long) saved.position);
      } else if (saved.homed && in_limits) {
        this->encoder_->set_position(saved.position);
        position = saved.position;
        this->homed_.store(true, std::memory_order_relaxed);
        ESP_LOGI(TAG, "Referenz aus NVS wiederhergestellt: Position %lld (selbsthemmende Mechanik). "
                      "Nach Handverstellung ohne Strom den Schalter 'Lift Referenz' ausschalten.",
                 (long long) saved.position);
      } else {
        ESP_LOGI(TAG, "NVS-Datensatz vorhanden, aber nicht referenziert oder ausserhalb der Grenzen");
      }
    } else {
      ESP_LOGI(TAG, "Kein gueltiger NVS-Datensatz: Lift startet unreferenziert. "
                    "Einmal referenzieren; ab dann uebersteht die Position OTA und Stromausfall.");
    }
    this->last_saved_position_ = position;
    this->last_saved_homed_ = this->homed_.load(std::memory_order_relaxed);
    this->last_saved_moving_ = false;
    this->saved_valid_ = false;
  }

  this->pos_.store(position, std::memory_order_relaxed);
  this->target_.store(position, std::memory_order_relaxed);
  this->last_observed_pos_.store(position, std::memory_order_relaxed);
  this->last_encoder_change_ms_.store(millis(), std::memory_order_relaxed);
  // Startwerte der Geschwindigkeitsmessung: Referenz auf die Ist-Position.
  this->speed_ref_pos_ = position;
  this->speed_ref_us_ = esp_timer_get_time();
  this->speed_filtered_ = 0.0f;
  this->speed_.store(0.0f, std::memory_order_relaxed);
  this->speed_cmd_.store(0.0f, std::memory_order_relaxed);

  if (!this->start_task()) {
    this->motion_allowed_.store(false, std::memory_order_release);
    this->coast_off_();
    this->mark_failed();
  }
}

bool LiftMotor::start_task() {
  if (this->task_ != nullptr)
    return true;

  this->motion_allowed_.store(false, std::memory_order_release);
  this->coast_off_();
  this->state_.store(LIFT_IDLE, std::memory_order_relaxed);
  this->last_hb_change_ms_ = millis();

  const BaseType_t ok = xTaskCreatePinnedToCore(&LiftMotor::task_trampoline, "lift_ctl", 4096, this,
                                               this->task_priority_, &this->task_, this->task_core_);
  if (ok != pdPASS) {
    this->task_ = nullptr;
    this->task_healthy_.store(false, std::memory_order_release);
    ESP_LOGE(TAG, "xTaskCreatePinnedToCore fehlgeschlagen");
    return false;
  }
  ESP_LOGI(TAG, "Regeltask automatisch gestartet: Kern %d, Prioritaet %u, Takt %u ms", this->task_core_,
           (unsigned) this->task_priority_, (unsigned) this->period_ms_);
  return true;
}

void LiftMotor::save_state_() {
  if (!this->persist_position_ || !this->pref_ready_)
    return;

  const int64_t position = this->position();
  const bool homed = this->homed();
  const bool moving = this->busy();

  // Nichts geaendert -> kein Schreibvorgang. Schont Flash-Zyklen.
  if (this->saved_valid_ && position == this->last_saved_position_ &&
      homed == this->last_saved_homed_ && moving == this->last_saved_moving_)
    return;

  // Immer vollstaendig genullt anlegen, damit reserved[] definiert ist.
  LiftPersistedState state{};
  state.magic = PERSIST_MAGIC;
  state.version = PERSIST_VERSION;
  state.position = position;
  state.homed = homed;
  state.moving = moving;
  if (!this->pref_.save(&state))
    return;

  // ESPHome legt save() nur in eine Warteliste; ins Flash schreibt erst sync().
  // Da hier ausschliesslich seltene Ereignisse gespeichert werden (zwei
  // Schreibvorgaenge pro Fahrt), ist der sofortige Schreibvorgang vertretbar --
  // periodisches Speichern waere fuer das Flash schaedlich.
  global_preferences->sync();

  this->last_saved_position_ = position;
  this->last_saved_homed_ = homed;
  this->last_saved_moving_ = moving;
  this->saved_valid_ = true;
}

void LiftMotor::clear_reference() {
  this->stop();
  this->homed_.store(false, std::memory_order_release);
  this->reference_pending_.store(false, std::memory_order_release);
  this->save_state_();
  ESP_LOGW(TAG, "Referenz verworfen: Positionsfahrten sind bis zum neuen Referenzieren gesperrt");
}

void LiftMotor::task_trampoline(void *arg) { static_cast<LiftMotor *>(arg)->task_loop(); }

void LiftMotor::drive_(int dir, float duty) {
  taskENTER_CRITICAL(&this->output_mux_);
  if (!this->motion_allowed_.load(std::memory_order_acquire)) {
    this->rpwm_->set_level(0.0f);
    this->lpwm_->set_level(0.0f);
    this->enable_->turn_off();
    this->duty_.store(0.0f, std::memory_order_relaxed);
    taskEXIT_CRITICAL(&this->output_mux_);
    return;
  }
  // Am realen Lift ist LPWM die physische Aufwaertsrichtung. Die logische
  // positive Richtung bleibt damit konsistent zur positiven Encoderposition.
  if (dir > 0) {
    this->rpwm_->set_level(0.0f);
    this->lpwm_->set_level(duty);
  } else {
    this->lpwm_->set_level(0.0f);
    this->rpwm_->set_level(duty);
  }
  this->enable_->turn_on();
  this->duty_.store(dir > 0 ? duty : -duty, std::memory_order_relaxed);
  taskEXIT_CRITICAL(&this->output_mux_);
}

void LiftMotor::brake_() {
  taskENTER_CRITICAL(&this->output_mux_);
  this->rpwm_->set_level(0.0f);
  this->lpwm_->set_level(0.0f);
  if (this->motion_allowed_.load(std::memory_order_acquire))
    this->enable_->turn_on();
  else
    this->enable_->turn_off();
  this->duty_.store(0.0f, std::memory_order_relaxed);
  taskEXIT_CRITICAL(&this->output_mux_);
}

void LiftMotor::coast_off_() {
  taskENTER_CRITICAL(&this->output_mux_);
  this->rpwm_->set_level(0.0f);
  this->lpwm_->set_level(0.0f);
  this->enable_->turn_off();
  this->duty_.store(0.0f, std::memory_order_relaxed);
  taskEXIT_CRITICAL(&this->output_mux_);
}

bool LiftMotor::goto_position(int64_t target) {
  if (!this->homed()) {
    ESP_LOGW(TAG, "Ziel %lld abgelehnt: Lift ist nicht referenziert", (long long) target);
    return false;
  }
  if (this->task_ == nullptr || !this->task_healthy_.load(std::memory_order_acquire)) {
    ESP_LOGE(TAG, "Ziel %lld abgelehnt: Regeltask ist nicht bereit", (long long) target);
    return false;
  }
  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Ziel %lld abgelehnt: Encoder ist nicht bereit", (long long) target);
    return false;
  }
  if (!this->target_in_limits_(target)) {
    ESP_LOGW(TAG, "Ziel %lld abgelehnt: erlaubt sind inklusive %lld..%lld", (long long) target,
             (long long) this->min_position_, (long long) this->max_position_);
    return false;
  }

  this->target_.store(target, std::memory_order_relaxed);
  this->reference_pending_.store(false, std::memory_order_relaxed);
  this->cmd_manual_direction_.store(0, std::memory_order_relaxed);
  this->cmd_relative_.store(false, std::memory_order_relaxed);
  this->motion_allowed_.store(true, std::memory_order_release);
  this->cmd_move_.store(true, std::memory_order_release);
  this->cmd_epoch_.fetch_add(1, std::memory_order_release);
  return true;
}

bool LiftMotor::move_relative(int64_t delta) {
  if (delta == 0) {
    ESP_LOGW(TAG, "Relativfahrt abgelehnt: Distanz 0");
    return false;
  }
  if (this->task_ == nullptr || !this->task_healthy_.load(std::memory_order_acquire)) {
    ESP_LOGE(TAG, "Relativfahrt abgelehnt: Regeltask ist nicht bereit");
    return false;
  }
  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Relativfahrt abgelehnt: Encoder ist nicht bereit");
    return false;
  }

  const int64_t start = this->encoder_->get_position();
  const int64_t target = start + delta;
  // Mit gueltiger Referenz bleiben die Softlimits verbindlich.
  if (this->homed() && !this->target_in_limits_(target)) {
    ESP_LOGW(TAG, "Relativfahrt abgelehnt: Ziel %lld liegt ausserhalb %lld..%lld", (long long) target,
             (long long) this->min_position_, (long long) this->max_position_);
    return false;
  }

  this->target_.store(target, std::memory_order_relaxed);
  this->reference_pending_.store(false, std::memory_order_relaxed);
  this->cmd_manual_direction_.store(0, std::memory_order_relaxed);
  this->cmd_relative_.store(true, std::memory_order_release);
  this->motion_allowed_.store(true, std::memory_order_release);
  this->cmd_move_.store(true, std::memory_order_release);
  this->cmd_epoch_.fetch_add(1, std::memory_order_release);
  ESP_LOGI(TAG, "Relativfahrt: Start %lld, Ziel %lld (%+lld Counts)", (long long) start,
           (long long) target, (long long) delta);
  return true;
}

bool LiftMotor::start_manual(int direction) {
  if (direction != 1 && direction != -1) {
    ESP_LOGW(TAG, "Manuelle Fahrt abgelehnt: Richtung muss +1 oder -1 sein (war %d)", direction);
    return false;
  }
  if (this->task_ == nullptr || !this->task_healthy_.load(std::memory_order_acquire)) {
    ESP_LOGE(TAG, "Manuelle Fahrt abgelehnt: Regeltask ist nicht bereit");
    return false;
  }
  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Manuelle Fahrt abgelehnt: Encoder ist nicht bereit");
    return false;
  }

  this->reference_pending_.store(false, std::memory_order_relaxed);
  this->cmd_manual_direction_.store(direction, std::memory_order_relaxed);
  this->cmd_relative_.store(false, std::memory_order_relaxed);
  this->motion_allowed_.store(true, std::memory_order_release);
  this->cmd_move_.store(true, std::memory_order_release);
  this->cmd_epoch_.fetch_add(1, std::memory_order_release);
  return true;
}

void LiftMotor::stop() {
  this->motion_allowed_.store(false, std::memory_order_release);
  this->coast_off_();
  this->speed_cmd_.store(0.0f, std::memory_order_relaxed);
  this->cmd_manual_direction_.store(0, std::memory_order_relaxed);
  this->cmd_relative_.store(false, std::memory_order_relaxed);
  this->motion_direction_.store(0, std::memory_order_relaxed);
  this->state_.store(LIFT_IDLE, std::memory_order_relaxed);
  this->cmd_move_.store(false, std::memory_order_release);
  this->cmd_epoch_.fetch_add(1, std::memory_order_release);
}

bool LiftMotor::prepare_reference() {
  this->stop();
  this->homed_.store(false, std::memory_order_relaxed);
  this->reference_pending_.store(true, std::memory_order_relaxed);

  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Referenzvorbereitung fehlgeschlagen: Encoder ist nicht bereit");
    return false;
  }
  const int64_t position = this->encoder_->get_position();
  const uint32_t now = millis();
  this->pos_.store(position, std::memory_order_relaxed);
  this->last_observed_pos_.store(position, std::memory_order_relaxed);
  this->last_encoder_change_ms_.store(now, std::memory_order_relaxed);
  ESP_LOGI(TAG, "Referenz vorbereitet: Antrieb aus; Position mindestens %u ms unveraendert lassen",
           (unsigned) REFERENCE_STABLE_MS);
  return true;
}

bool LiftMotor::set_reference(int64_t position) {
  // Kein vorgeschaltetes "vorbereiten" mehr noetig: der Stillstand wird hier
  // direkt geprueft (kein Auftrag, Antrieb aus, Encoder ruhig). Damit genuegt
  // eine Taste, nachdem manuell auf die Referenzlage gefahren wurde.
  if (this->busy()) {
    ESP_LOGW(TAG, "Referenz abgelehnt: Lift faehrt noch -- zuerst Stop druecken");
    return false;
  }
  if (!this->target_in_limits_(position)) {
    ESP_LOGW(TAG, "Referenzwert %lld abgelehnt: erlaubt sind inklusive %lld..%lld", (long long) position,
             (long long) this->min_position_, (long long) this->max_position_);
    return false;
  }
  if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
    ESP_LOGE(TAG, "Referenz abgelehnt: Encoder ist nicht bereit");
    return false;
  }
  if (this->motion_allowed_.load(std::memory_order_acquire) || this->duty() != 0.0f) {
    ESP_LOGW(TAG, "Referenz abgelehnt: Motor ist nicht sicher ausgeschaltet");
    return false;
  }

  const uint32_t now = millis();
  const int64_t observed = this->encoder_->get_position();
  const int64_t last_observed = this->last_observed_pos_.load(std::memory_order_relaxed);
  if (observed != last_observed) {
    this->last_observed_pos_.store(observed, std::memory_order_relaxed);
    this->last_encoder_change_ms_.store(now, std::memory_order_relaxed);
    this->pos_.store(observed, std::memory_order_relaxed);
    ESP_LOGW(TAG, "Referenz abgelehnt: Encoder hat sich seit der letzten Task-Abtastung bewegt");
    return false;
  }
  const uint32_t stable_ms = now - this->last_encoder_change_ms_.load(std::memory_order_relaxed);
  if (stable_ms < REFERENCE_STABLE_MS) {
    ESP_LOGW(TAG, "Referenz abgelehnt: Encoder erst seit %u ms unveraendert (erforderlich: %u ms)",
             (unsigned) stable_ms, (unsigned) REFERENCE_STABLE_MS);
    return false;
  }

  this->encoder_->set_position(position);
  this->pos_.store(position, std::memory_order_relaxed);
  this->target_.store(position, std::memory_order_relaxed);
  this->last_observed_pos_.store(position, std::memory_order_relaxed);
  this->motion_direction_.store(0, std::memory_order_relaxed);
  this->state_.store(LIFT_REACHED, std::memory_order_relaxed);
  this->homed_.store(true, std::memory_order_release);
  this->reference_pending_.store(false, std::memory_order_release);
  // Referenz sofort sichern, damit sie einen Stromausfall uebersteht.
  this->save_state_();
  ESP_LOGI(TAG, "Lift referenziert bei Position %lld (im NVS gesichert)", (long long) position);
  return true;
}

void LiftMotor::task_loop() {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t configured_ticks = pdMS_TO_TICKS(this->period_ms_);
  const TickType_t period_ticks = configured_ticks > 0 ? configured_ticks : 1;

  uint32_t seen_command = this->cmd_epoch_.load(std::memory_order_acquire);
  uint32_t seen_main_hb = this->main_hb_.load(std::memory_order_relaxed);
  int64_t stall_ref = this->position();
  uint32_t stall_acc_ms = 0;
  uint32_t move_acc_ms = 0;
  uint32_t brake_left_ms = 0;
  uint32_t main_stale_ms = 0;
  uint32_t pulse = 0;
  int last_dir = 0;
  int active_manual_direction = 0;
  bool relative_run = false;
  int64_t t_prev = esp_timer_get_time();
  // Geschwindigkeits-Sollbetrag des Trapezprofils (Counts/s, immer >= 0) und
  // I-Anteil des Reglers. Beide gehoeren nur dem Task.
  float v_cmd = 0.0f;
  float integral = 0.0f;

  // Regler zuruecksetzen: jede Fahrt startet aus dem Stand und ohne Windup-Rest.
  auto reset_controller = [&]() {
    v_cmd = 0.0f;
    integral = 0.0f;
    this->speed_cmd_.store(0.0f, std::memory_order_relaxed);
  };

  while (true) {
    vTaskDelayUntil(&last_wake, period_ticks);

    const int64_t t_now = esp_timer_get_time();
    const uint32_t dt_us = static_cast<uint32_t>(t_now - t_prev);
    t_prev = t_now;
    if (dt_us > this->worst_period_us_.load(std::memory_order_relaxed))
      this->worst_period_us_.store(dt_us, std::memory_order_relaxed);
    this->cycles_.fetch_add(1, std::memory_order_relaxed);
    if (dt_us > this->period_ms_ * 2000U)
      this->overruns_.fetch_add(1, std::memory_order_relaxed);
    // Echte verstrichene Zeit verwenden. Mit der reinen Sollperiode liefen
    // Timeout-, Stall- und Bremszeiten gegenueber der Wanduhr falsch.
    uint32_t dt_ms = dt_us / 1000U;
    if (dt_ms == 0U)
      dt_ms = this->period_ms_ > 0U ? this->period_ms_ : 1U;
    // Reglerzeitschritt in Sekunden; niemals 0, sonst stehen Rampe und I-Anteil.
    float dt_s = static_cast<float>(dt_us) * 1e-6f;
    if (dt_s <= 0.0f)
      dt_s = static_cast<float>(this->period_ms_ > 0U ? this->period_ms_ : 1U) * 1e-3f;
    const uint32_t now_ms = millis();

    this->hb_.fetch_add(1, std::memory_order_relaxed);

    const uint32_t mh = this->main_hb_.load(std::memory_order_relaxed);
    if (mh != seen_main_hb) {
      seen_main_hb = mh;
      main_stale_ms = 0;
      this->task_healthy_.store(true, std::memory_order_release);
    } else {
      main_stale_ms += dt_ms;
    }
    if (main_stale_ms > 1000U) {
      this->task_healthy_.store(false, std::memory_order_release);
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_FAULT, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }

    if (this->encoder_ == nullptr || !this->encoder_->is_hw_ready()) {
      this->task_healthy_.store(false, std::memory_order_release);
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_FAULT, std::memory_order_relaxed);
      reset_controller();
      continue;
    }

    // Der Regeltask liest den PCNT in jedem Zyklus direkt; kein Mainloop-Relay.
    const int64_t pos = this->encoder_->get_position();
    this->observe_encoder_(pos, now_ms);
    // Geschwindigkeitsmessung laeuft immer mit, auch im Stillstand (Diagnose).
    this->update_speed_(pos, t_now);
    const float v_abs = fabsf(this->speed_filtered_);

    const uint32_t command = this->cmd_epoch_.load(std::memory_order_acquire);
    if (command != seen_command) {
      seen_command = command;
      const bool move = this->cmd_move_.load(std::memory_order_acquire);
      const int pending_manual_direction = this->cmd_manual_direction_.load(std::memory_order_relaxed);
      const bool pending_relative = this->cmd_relative_.load(std::memory_order_acquire);
      if (!move || !this->motion_allowed_.load(std::memory_order_acquire)) {
        this->motion_allowed_.store(false, std::memory_order_release);
        this->coast_off_();
        this->motion_direction_.store(0, std::memory_order_relaxed);
        this->state_.store(LIFT_IDLE, std::memory_order_relaxed);
        reset_controller();
        active_manual_direction = 0;
        relative_run = false;
        last_dir = 0;
        brake_left_ms = 0;
        continue;
      }
      relative_run = pending_relative;
      stall_ref = pos;
      stall_acc_ms = 0;
      move_acc_ms = 0;
      brake_left_ms = 0;
      pulse = 0;
      last_dir = 0;
      // Jede neue Fahrt beginnt bei v_cmd = 0 und ohne I-Anteil -> sanfter Anlauf.
      reset_controller();
      active_manual_direction = pending_manual_direction;
      if (active_manual_direction == 1 || active_manual_direction == -1) {
        this->motion_direction_.store(active_manual_direction, std::memory_order_relaxed);
        this->state_.store(LIFT_MANUAL, std::memory_order_relaxed);
      } else {
        this->motion_direction_.store(0, std::memory_order_relaxed);
        this->state_.store(LIFT_MOVING, std::memory_order_relaxed);
      }
    }

    const uint8_t current_state = this->state_.load(std::memory_order_relaxed);
    if (current_state != LIFT_MOVING && current_state != LIFT_MANUAL)
      continue;

    const bool manual = current_state == LIFT_MANUAL;
    const int manual_direction = active_manual_direction;
    const int64_t target = this->target_.load(std::memory_order_relaxed);
    // Absolute Ziele erfordern eine gueltige Referenz und muessen in den
    // Softlimits liegen. Eine Relativfahrt (Kalibrierung) ist auch ohne
    // Referenz zulaessig; mit Referenz gelten die Limits ebenfalls.
    const bool absolute_ok = relative_run ? (!this->homed() || this->target_in_limits_(target))
                                          : (this->homed() && this->target_in_limits_(target));
    if ((!manual && !absolute_ok) ||
        (manual && manual_direction != 1 && manual_direction != -1)) {
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_LIMIT, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }

    move_acc_ms += dt_ms;
    const uint32_t active_timeout_ms = manual ? this->manual_timeout_ms_ : this->timeout_ms_;
    if (move_acc_ms > active_timeout_ms) {
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_TIMEOUT, std::memory_order_relaxed);
      this->last_move_ms_.store(move_acc_ms, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }

    int64_t err = 0;
    int64_t mag = 0;
    if (!manual) {
      err = target - pos;
      mag = err >= 0 ? err : -err;

      if (brake_left_ms > 0) {
        this->brake_();
        brake_left_ms -= (brake_left_ms > dt_ms) ? dt_ms : brake_left_ms;
        if (brake_left_ms == 0) {
          if (mag <= this->tolerance_) {
            this->motion_allowed_.store(false, std::memory_order_release);
            this->coast_off_();
            this->motion_direction_.store(0, std::memory_order_relaxed);
            this->state_.store(LIFT_REACHED, std::memory_order_relaxed);
            this->last_move_ms_.store(move_acc_ms, std::memory_order_relaxed);
            reset_controller();
          } else {
            // Nachgerutscht: erneut anfahren, dabei wieder sanft aus dem Stand.
            stall_ref = pos;
            stall_acc_ms = 0;
            last_dir = 0;
            pulse = 0;
            reset_controller();
          }
        }
        continue;
      }

      if (mag <= this->tolerance_) {
        brake_left_ms = this->brake_ms_;
        this->brake_();
        reset_controller();
        continue;
      }
    }

    const int dir = manual ? manual_direction : (err > 0 ? 1 : -1);

    // Softlimits unmittelbar vor jeder moeglichen Ansteuerung. Im manuellen
    // Modus gelten sie erst nach der Referenzierung; vorher ist die absolute
    // Encoderposition noch nicht definiert.
    if (this->homed() &&
        ((dir > 0 && pos >= this->max_position_) || (dir < 0 && pos <= this->min_position_))) {
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_LIMIT, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }
    this->motion_direction_.store(dir, std::memory_order_relaxed);

    if (last_dir != 0 && dir != last_dir) {
      // Richtungswechsel: erst bremsen, dann mit frischer Rampe neu anfahren.
      this->brake_();
      reset_controller();
      last_dir = 0;
      pulse = 0;
      continue;
    }

    // --- Feinpositionierung: der letzte Count-Bereich bleibt Pulsbetrieb, dort
    // ist eine Geschwindigkeitsregelung wegen der Encoderquantisierung sinnlos.
    const bool fine = !manual && mag <= this->fine_window_;

    // --- Trapezprofil: Zielbetrag der Geschwindigkeit bestimmen (Counts/s) ---
    float v_target = 0.0f;
    if (fine) {
      // Kein Geschwindigkeitssollwert im Pulsbetrieb; Regler bleibt neutral.
      v_cmd = 0.0f;
      integral = 0.0f;
    } else if (manual) {
      v_target = this->max_speed_;
      if (this->homed()) {
        // Vor der jeweiligen Softgrenze bremsweggefuehrt abbremsen, statt mit
        // voller Fahrt hineinzulaufen. Die harte Grenzpruefung schaltet dann ab.
        const float distance = dir > 0 ? static_cast<float>(this->max_position_ - pos)
                                       : static_cast<float>(pos - this->min_position_);
        const float v_limit = this->brake_limit_speed_(distance > 0.0f ? distance : 0.0f);
        if (v_limit < v_target)
          v_target = v_limit;
        // Kriechgeschwindigkeit halten, damit die Grenze wirklich erreicht wird.
        if (distance > static_cast<float>(this->tolerance_) && v_target < this->approach_speed_)
          v_target = this->approach_speed_;
      }
    } else {
      // Positionsfahrt: Grenze aus dem verbleibenden Bremsweg, gedeckelt durch
      // max_speed, aber nie unter approach_speed solange das Ziel offen ist.
      const float remaining = static_cast<float>(mag - static_cast<int64_t>(this->tolerance_));
      v_target = this->brake_limit_speed_(remaining > 0.0f ? remaining : 0.0f);
      if (v_target > this->max_speed_)
        v_target = this->max_speed_;
      if (v_target < this->approach_speed_)
        v_target = this->approach_speed_;
    }
    if (v_target > this->max_speed_)
      v_target = this->max_speed_;

    // Rampe: Anstieg mit accel, Absenkung mit decel.
    if (v_cmd < v_target) {
      v_cmd += this->accel_ * dt_s;
      if (v_cmd > v_target)
        v_cmd = v_target;
    } else if (v_cmd > v_target) {
      v_cmd -= this->decel_ * dt_s;
      if (v_cmd < v_target)
        v_cmd = v_target;
    }
    if (v_cmd < 0.0f)
      v_cmd = 0.0f;
    this->speed_cmd_.store(v_cmd, std::memory_order_relaxed);

    // --- Stall-Erkennung: zaehlt erst, wenn wirklich nennenswerter Vortrieb
    // gefordert ist. Bei flacher Rampe ist v_cmd in den ersten Zyklen nur
    // wenige Counts/s gross; dort darf noch kein Stall gemeldet werden, sonst
    // schlaegt die Ueberwachung schon beim sanften Anfahren zu. Ab
    // approach_speed ist echte Bewegung zu erwarten -- gegen eine tatsaechliche
    // Blockade bleibt der Schutz damit voll wirksam.
    const bool stall_gate = fine || v_cmd >= this->approach_speed_;
    if (pos == stall_ref) {
      if (stall_gate)
        stall_acc_ms += dt_ms;
    } else {
      stall_ref = pos;
      stall_acc_ms = 0;
    }
    if (stall_acc_ms > this->stall_ms_) {
      this->stall_duty_.store(this->duty_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      this->stall_speed_.store(v_cmd, std::memory_order_relaxed);
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_STALL, std::memory_order_relaxed);
      this->last_move_ms_.store(move_acc_ms, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }

    // --- Stellgroesse bestimmen ---
    float duty;
    if (fine) {
      // Pulsbetrieb mit Mindest-Duty: kurze Stoesse, dazwischen Bremsen.
      pulse++;
      if (pulse < this->pulse_period_) {
        this->brake_();
        continue;
      }
      pulse = 0;
      duty = this->duty_floor_(dir);
    } else if (v_cmd <= 0.0f) {
      // Kein Sollwert (z. B. Profil auf 0 vor einer Softgrenze) -> bremsen.
      this->brake_();
      continue;
    } else {
      // PI-Regler auf den Betrag der gemessenen Geschwindigkeit. Der I-Anteil
      // uebernimmt die Last-, Reibungs- und Richtungskompensation.
      const float e = v_cmd - v_abs;
      integral += this->speed_ki_ * e * dt_s;
      if (integral < 0.0f)
        integral = 0.0f;
      if (integral > this->duty_max_)
        integral = this->duty_max_;

      const float floor_duty = this->duty_floor_(dir);
      const float raw = floor_duty + this->speed_kp_ * e + integral;
      duty = raw;
      if (duty > this->duty_max_)
        duty = this->duty_max_;
      if (duty < 0.0f)
        duty = 0.0f;
      if (duty != raw) {
        // Anti-Windup: I-Anteil auf den tatsaechlich gestellten Duty zurueckrechnen.
        float back = duty - floor_duty - this->speed_kp_ * e;
        if (back < 0.0f)
          back = 0.0f;
        if (back > this->duty_max_)
          back = this->duty_max_;
        integral = back;
      }
    }

    // Zweite, direkt an drive_ angrenzende Pruefung gegen spaete Positionsaenderungen.
    const int64_t drive_pos = this->encoder_->get_position();
    this->observe_encoder_(drive_pos, millis());
    if (this->homed() &&
        ((dir > 0 && drive_pos >= this->max_position_) || (dir < 0 && drive_pos <= this->min_position_))) {
      this->motion_allowed_.store(false, std::memory_order_release);
      this->coast_off_();
      this->motion_direction_.store(0, std::memory_order_relaxed);
      this->state_.store(LIFT_LIMIT, std::memory_order_relaxed);
      reset_controller();
      last_dir = 0;
      continue;
    }
    this->drive_(dir, duty);
    last_dir = dir;
  }
}

void LiftMotor::loop() {
  this->main_hb_.fetch_add(1, std::memory_order_relaxed);

  if (this->task_ == nullptr)
    return;

  const uint32_t now = millis();
  const uint32_t heartbeat = this->hb_.load(std::memory_order_relaxed);
  if (heartbeat != this->last_seen_hb_) {
    this->last_seen_hb_ = heartbeat;
    this->last_hb_change_ms_ = now;
  } else if (now - this->last_hb_change_ms_ > 300U) {
    this->task_healthy_.store(false, std::memory_order_release);
    this->motion_allowed_.store(false, std::memory_order_release);
    this->coast_off_();
    this->motion_direction_.store(0, std::memory_order_relaxed);
    if (this->state_.load(std::memory_order_relaxed) != LIFT_FAULT) {
      this->state_.store(LIFT_FAULT, std::memory_order_relaxed);
      ESP_LOGE(TAG, "Regeltask ohne Lebenszeichen seit %u ms -- Antrieb abgeschaltet",
               (unsigned) (now - this->last_hb_change_ms_));
    }
  }

  const uint8_t current = this->state_.load(std::memory_order_relaxed);
  if (current != this->last_logged_state_) {
    this->last_logged_state_ = current;
    // Genau hier wird gespeichert, sonst nie: bei Fahrtbeginn wird "moving"
    // gesetzt, bei Fahrtende der Endstand mit "moving = false". Das sind zwei
    // Flash-Schreibvorgaenge pro Fahrt statt eines periodischen Dauerbetriebs.
    this->save_state_();
    const long long position = static_cast<long long>(this->position());
    const long long target = static_cast<long long>(this->target());
    switch (current) {
      case LIFT_REACHED:
        ESP_LOGI(TAG, "Ziel erreicht: Soll %+lld, Stand %+lld, Restfehler %+lld, Dauer %u ms", target,
                 position, target - position, (unsigned) this->last_move_ms());
        break;
      case LIFT_STALL:
        ESP_LOGE(TAG,
                 "STALL: %u ms ohne Encoderbewegung, Soll %+lld, Stand %+lld, Duty %+.0f Prozent, "
                 "v_soll %.0f Counts/s",
                 (unsigned) this->stall_ms_, target, position,
                 this->stall_duty_.load(std::memory_order_relaxed) * 100.0f,
                 this->stall_speed_.load(std::memory_order_relaxed));
        break;
      case LIFT_TIMEOUT:
        ESP_LOGE(TAG, "TIMEOUT nach %u ms: Soll %+lld, Stand %+lld", (unsigned) this->last_move_ms(), target,
                 position);
        break;
      case LIFT_LIMIT:
        ESP_LOGE(TAG, "SOFTLIMIT: Soll %+lld, Stand %+lld, erlaubt %lld..%lld; Antrieb aus", target, position,
                 (long long) this->min_position_, (long long) this->max_position_);
        break;
      case LIFT_MOVING:
        ESP_LOGI(TAG, "Fahre: Soll %+lld, Stand %+lld", target, position);
        break;
      default:
        break;
    }
  }
}

void LiftMotor::dump_config() {
  ESP_LOGCONFIG(TAG, "Lift Motor (Regeltask):");
  ESP_LOGCONFIG(TAG, "  Kern: %d, Prioritaet: %u, Takt: %u ms", this->task_core_,
                (unsigned) this->task_priority_, (unsigned) this->period_ms_);
  ESP_LOGCONFIG(TAG, "  Positionsgrenzen: %lld..%lld (inklusive)", (long long) this->min_position_,
                (long long) this->max_position_);
  ESP_LOGCONFIG(TAG, "  Richtung: positive Position/LPWM = Oeffnen; Encoder invert_direction=false");
  ESP_LOGCONFIG(TAG, "  Toleranz: %d, Feinfenster: %d, Pulsteiler: %u", (int) this->tolerance_,
                (int) this->fine_window_, (unsigned) this->pulse_period_);
  ESP_LOGCONFIG(TAG, "  Duty min auf/ab (Vorsteuerung): %.2f / %.2f, max: %.2f", this->duty_min_up_,
                this->duty_min_down_, this->duty_max_);
  ESP_LOGCONFIG(TAG, "  Trapezprofil: v_max %.0f, a %.0f, d %.0f, Kriechen %.0f (Counts/s)",
                this->max_speed_, this->accel_, this->decel_, this->approach_speed_);
  ESP_LOGCONFIG(TAG, "  Geschwindigkeitsregler: kp %.5f, ki %.5f (Duty pro Count/s) -- kalibrierbar",
                this->speed_kp_, this->speed_ki_);
  ESP_LOGCONFIG(TAG, "  Messung: Fenster %u ms, EMA-Alpha %.2f", (unsigned) (this->speed_window_us_ / 1000U),
                this->speed_filter_);
  ESP_LOGCONFIG(TAG, "  Bremse: %u ms, Stall: %u ms, Timeout: %u ms, Manuell-Timeout: %u ms",
                (unsigned) this->brake_ms_, (unsigned) this->stall_ms_, (unsigned) this->timeout_ms_,
                (unsigned) this->manual_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Persistenz: %s (nur bei Fahrtbeginn/-ende und Referenzwechsel,"
                     " kein periodisches Schreiben)",
                this->persist_position_ ? "NVS aktiv" : "aus");
}

}  // namespace esphome::lift_motor
