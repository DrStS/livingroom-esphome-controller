#include "pcnt_quadrature.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <climits>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_private/esp_clk.h>
#include <hal/pcnt_ll.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>

namespace esphome::pcnt_quadrature {

static const char *const TAG = "pcnt_quadrature";

pcnt_channel_edge_action_t PcntQuadratureSensor::direction_adjust_(pcnt_channel_edge_action_t action) const {
  if (!this->invert_direction_)
    return action;

  if (action == PCNT_CHANNEL_EDGE_ACTION_INCREASE)
    return PCNT_CHANNEL_EDGE_ACTION_DECREASE;
  if (action == PCNT_CHANNEL_EDGE_ACTION_DECREASE)
    return PCNT_CHANNEL_EDGE_ACTION_INCREASE;
  return action;
}

bool PcntQuadratureSensor::configure_channel_a_() {
  const pcnt_chan_config_t config = {
      .edge_gpio_num = static_cast<gpio_num_t>(this->pin_a_->get_pin()),
      .level_gpio_num = static_cast<gpio_num_t>(this->pin_b_->get_pin()),
  };

  esp_err_t err = pcnt_new_channel(this->unit_, &config, &this->channel_a_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Creating PCNT channel A failed: %s", esp_err_to_name(err));
    return false;
  }

  // Positive direction is the state sequence 00 -> 10 -> 11 -> 01 -> 00
  // (A leads B), before applying invert_direction.
  pcnt_channel_edge_action_t positive = PCNT_CHANNEL_EDGE_ACTION_DECREASE;
  pcnt_channel_edge_action_t negative =
      this->resolution_ == PCNT_QUADRATURE_X1 ? PCNT_CHANNEL_EDGE_ACTION_HOLD
                                               : PCNT_CHANNEL_EDGE_ACTION_INCREASE;

  positive = this->direction_adjust_(positive);
  negative = this->direction_adjust_(negative);

  err = pcnt_channel_set_edge_action(this->channel_a_, positive, negative);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Setting PCNT channel A edge action failed: %s", esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_level_action(this->channel_a_, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Setting PCNT channel A level action failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool PcntQuadratureSensor::configure_channel_b_() {
  const pcnt_chan_config_t config = {
      .edge_gpio_num = static_cast<gpio_num_t>(this->pin_b_->get_pin()),
      .level_gpio_num = static_cast<gpio_num_t>(this->pin_a_->get_pin()),
  };

  esp_err_t err = pcnt_new_channel(this->unit_, &config, &this->channel_b_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Creating PCNT channel B failed: %s", esp_err_to_name(err));
    return false;
  }

  const auto positive = this->direction_adjust_(PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  const auto negative = this->direction_adjust_(PCNT_CHANNEL_EDGE_ACTION_DECREASE);

  err = pcnt_channel_set_edge_action(this->channel_b_, positive, negative);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Setting PCNT channel B edge action failed: %s", esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_level_action(this->channel_b_, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Setting PCNT channel B level action failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

void PcntQuadratureSensor::setup() {
  if (this->pin_a_ == nullptr || this->pin_b_ == nullptr) {
    ESP_LOGE(TAG, "Both pin_a and pin_b are required");
    this->mark_failed();
    return;
  }
  if (this->pin_a_->get_pin() == this->pin_b_->get_pin()) {
    ESP_LOGE(TAG, "pin_a and pin_b must be different GPIOs");
    this->mark_failed();
    return;
  }
  if (this->pin_a_->is_inverted() || this->pin_b_->is_inverted()) {
    ESP_LOGE(TAG, "GPIO inversion is unsupported; use invert_direction instead");
    this->mark_failed();
    return;
  }
  // PCNT wird in der Hardware-Setup-Phase initialisiert, bevor LiftMotor seinen
  // Regeltask startet und die Encoderbereitschaft prueft.
  if (!this->init_hardware()) {
    ESP_LOGE(TAG, "PCNT-Init fehlgeschlagen");
    this->mark_failed();
  }
}

bool PcntQuadratureSensor::init_hardware() {
  if (this->hw_ready_)
    return true;

  ESP_LOGI(TAG, "init 1/9: GPIO-Konfiguration A=%u B=%u", this->pin_a_->get_pin(), this->pin_b_->get_pin());
  // Nochmal setup() aufrufen falls zwischen Boot und jetzt etwas die Pins
  // verbogen hat. Danach Rohpegel loggen -- damit sehen wir, ob die Pins VOR
  // der PCNT-Routing-Zuweisung den physischen Pegel tragen.
  this->pin_a_->setup();
  this->pin_b_->setup();
  {
    const gpio_num_t pa = static_cast<gpio_num_t>(this->pin_a_->get_pin());
    const gpio_num_t pb = static_cast<gpio_num_t>(this->pin_b_->get_pin());
    ESP_LOGI(TAG, "init 1/9: Pegel VOR PCNT-Routing: A=%d B=%d", gpio_get_level(pa), gpio_get_level(pb));
  }

  ESP_LOGI(TAG, "init 2/9: pcnt_new_unit (accum_count)");
  const pcnt_unit_config_t unit_config = {
      .low_limit = INT16_MIN,
      .high_limit = INT16_MAX,
      .flags = {.accum_count = true},
  };

  esp_err_t err = pcnt_new_unit(&unit_config, &this->unit_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Creating PCNT unit failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "init 3/9: Kanal A (Edge=A, Level=B)");
  if (!this->configure_channel_a_())
    return false;

  if (this->resolution_ == PCNT_QUADRATURE_X4) {
    ESP_LOGI(TAG, "init 4/9: Kanal B (Edge=B, Level=A)");
    if (!this->configure_channel_b_())
      return false;
  } else {
    ESP_LOGI(TAG, "init 4/9: Kanal B uebersprungen (resolution x%u)", static_cast<unsigned>(this->resolution_));
  }

  ESP_LOGI(TAG, "init 5/9: Glitch-Filter %" PRIu32 " us", this->filter_us_);
  if (this->filter_us_ != 0) {
    const uint32_t apb_mhz = static_cast<uint32_t>(esp_clk_apb_freq()) / 1000000U;
    const uint32_t max_glitch_ns = PCNT_LL_MAX_GLITCH_WIDTH * 1000U / apb_mhz;
    const uint32_t requested_ns = this->filter_us_ * 1000U;
    const uint32_t effective_ns = std::min(requested_ns, max_glitch_ns);

    if (effective_ns != requested_ns) {
      ESP_LOGW(TAG, "Requested filter %" PRIu32 " us exceeds hardware limit; using %.3f us", this->filter_us_,
               effective_ns / 1000.0f);
    }

    const pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = effective_ns,
    };
    err = pcnt_unit_set_glitch_filter(this->unit_, &filter_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Setting PCNT glitch filter failed: %s", esp_err_to_name(err));
      return false;
    }
  }

  // Required by the new IDF driver for accum_count overflow compensation.
  ESP_LOGI(TAG, "init 6/9: Watch Points (INT16_MIN / INT16_MAX)");
  err = pcnt_unit_add_watch_point(this->unit_, INT16_MIN);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Adding low-limit watch point failed: %s", esp_err_to_name(err));
    return false;
  }
  err = pcnt_unit_add_watch_point(this->unit_, INT16_MAX);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Adding high-limit watch point failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "init 7/9: pcnt_unit_enable");
  err = pcnt_unit_enable(this->unit_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enabling PCNT unit failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "init 8/9: pcnt_unit_clear_count");
  err = pcnt_unit_clear_count(this->unit_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Clearing PCNT unit failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "init 9/9: pcnt_unit_start");
  err = pcnt_unit_start(this->unit_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Starting PCNT unit failed: %s", esp_err_to_name(err));
    return false;
  }

  this->last_driver_count_ = 0;
  this->position_ = 0;
  this->last_accumulate_ms_ = millis();
  this->last_speed_ms_ = this->last_accumulate_ms_;
  this->last_speed_position_ = 0;
  this->hw_ready_ = true;
  {
    const gpio_num_t pa = static_cast<gpio_num_t>(this->pin_a_->get_pin());
    const gpio_num_t pb = static_cast<gpio_num_t>(this->pin_b_->get_pin());
    ESP_LOGI(TAG, "Pegel NACH PCNT-Routing: A=%d B=%d", gpio_get_level(pa), gpio_get_level(pb));
  }
  ESP_LOGI(TAG, "PCNT bereit, Quadratur x%u laeuft", static_cast<unsigned>(this->resolution_));
  return true;
}

bool PcntQuadratureSensor::read_and_accumulate_() {
  if (this->unit_ == nullptr)
    return false;

  // Der PortMUX umfasst bewusst den Hardware-Read UND das Delta-Update. Damit
  // koennen Mainloop (Sensor-Publish) und Lift-Regeltask auf Kern 1 weder
  // denselben Rohstand doppelt integrieren noch last_driver_count_ ueberholen.
  int raw_count = 0;
  esp_err_t err;
  taskENTER_CRITICAL(&this->mux_);
  err = pcnt_unit_get_count(this->unit_, &raw_count);
  if (err == ESP_OK) {
    const uint32_t delta_mod =
        static_cast<uint32_t>(raw_count) - static_cast<uint32_t>(this->last_driver_count_);
    int64_t delta = static_cast<int64_t>(delta_mod);
    if (delta_mod > static_cast<uint32_t>(INT32_MAX))
      delta -= (INT64_C(1) << 32);

    this->position_ += delta;
    this->last_driver_count_ = static_cast<int32_t>(raw_count);
  }
  taskEXIT_CRITICAL(&this->mux_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Reading PCNT count failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

void PcntQuadratureSensor::loop() {
  if (this->unit_ == nullptr)
    return;

  const uint32_t now = millis();
  if (now - this->last_accumulate_ms_ >= ACCUMULATE_INTERVAL_MS) {
    this->read_and_accumulate_();
    this->last_accumulate_ms_ = now;
  }
}

void PcntQuadratureSensor::update() {
  if (!this->read_and_accumulate_())
    return;

  const uint32_t now = millis();
  taskENTER_CRITICAL(&this->mux_);
  const int64_t position = this->position_;
  taskEXIT_CRITICAL(&this->mux_);
  this->publish_state(static_cast<float>(position));

  if (this->speed_sensor_ != nullptr) {
    const uint32_t elapsed_ms = now - this->last_speed_ms_;
    if (elapsed_ms > 0) {
      const int64_t delta = position - this->last_speed_position_;
      const float speed = static_cast<float>(delta) * 1000.0f / static_cast<float>(elapsed_ms);
      this->speed_sensor_->publish_state(speed);
    }
    this->last_speed_position_ = position;
    this->last_speed_ms_ = now;
  }
}

void PcntQuadratureSensor::set_position(int64_t position) {
  if (this->unit_ != nullptr)
    this->read_and_accumulate_();

  taskENTER_CRITICAL(&this->mux_);
  this->position_ = position;
  taskEXIT_CRITICAL(&this->mux_);
  this->last_speed_position_ = position;   // nur Mainloop-Statistik
  this->last_speed_ms_ = millis();
  this->publish_state(static_cast<float>(position));
  if (this->speed_sensor_ != nullptr)
    this->speed_sensor_->publish_state(0.0f);
}

int64_t PcntQuadratureSensor::get_position() {
  if (this->unit_ != nullptr)
    this->read_and_accumulate_();
  taskENTER_CRITICAL(&this->mux_);
  const int64_t pos = this->position_;
  taskEXIT_CRITICAL(&this->mux_);
  return pos;
}

void PcntQuadratureSensor::sample_raw_levels(uint32_t duration_ms, uint32_t *edges_a,
                                             uint32_t *edges_b, float *high_a, float *high_b,
                                             uint32_t *samples) {
  uint32_t ea = 0, eb = 0, ha = 0, hb = 0, n = 0;
  if (this->pin_a_ == nullptr || this->pin_b_ == nullptr) {
    if (edges_a)
      *edges_a = 0;
    if (edges_b)
      *edges_b = 0;
    if (high_a)
      *high_a = 0.0f;
    if (high_b)
      *high_b = 0.0f;
    if (samples)
      *samples = 0;
    return;
  }

  const gpio_num_t pa = static_cast<gpio_num_t>(this->pin_a_->get_pin());
  const gpio_num_t pb = static_cast<gpio_num_t>(this->pin_b_->get_pin());

  int la = gpio_get_level(pa);
  int lb = gpio_get_level(pb);
  const uint32_t t_start = millis();
  // Kein Delay in der Schleife: die Abtastrate soll so hoch wie moeglich sein,
  // damit auch kurze Pulse sicher erfasst werden. Der Aufrufer weiss, dass
  // diese Funktion blockiert.
  while (millis() - t_start < duration_ms) {
    const int a = gpio_get_level(pa);
    const int b = gpio_get_level(pb);
    if (a != la) {
      ea++;
      la = a;
    }
    if (b != lb) {
      eb++;
      lb = b;
    }
    ha += static_cast<uint32_t>(a);
    hb += static_cast<uint32_t>(b);
    n++;
  }

  if (edges_a)
    *edges_a = ea;
  if (edges_b)
    *edges_b = eb;
  if (high_a)
    *high_a = n ? (100.0f * static_cast<float>(ha) / static_cast<float>(n)) : 0.0f;
  if (high_b)
    *high_b = n ? (100.0f * static_cast<float>(hb) / static_cast<float>(n)) : 0.0f;
  if (samples)
    *samples = n;
}

void PcntQuadratureSensor::probe_pin_drive(float *high_a_pulldown, float *high_b_pulldown) {
  if (high_a_pulldown)
    *high_a_pulldown = -1.0f;
  if (high_b_pulldown)
    *high_b_pulldown = -1.0f;
  if (this->pin_a_ == nullptr || this->pin_b_ == nullptr)
    return;

  const gpio_num_t pa = static_cast<gpio_num_t>(this->pin_a_->get_pin());
  const gpio_num_t pb = static_cast<gpio_num_t>(this->pin_b_->get_pin());

  // Vorherige Pull-Konfiguration sichern und am Ende EXAKT wiederherstellen.
  //
  // FRUEHERER FEHLER: Hier wurde am Ende pauschal GPIO_PULLUP_ONLY gesetzt.
  // Unsere Konfiguration betreibt die Pins aber ohne internen Pull, weil die
  // Hall-Sensoren aktiv treiben. Nach dem ersten Aufruf dieser Diagnose hingen
  // deshalb dauerhaft rund 45 kOhm intern an 3,3 V. Ein abgezogener Encoder
  // lieferte dann HIGH, was faelschlich als Beweis fuer funktionierende externe
  // Pull-ups gelesen wurde. Eine Messfunktion darf den Messgegenstand nicht
  // verstellen.
  const uint32_t saved_a = REG_READ(GPIO_PIN_MUX_REG[pa]);
  const uint32_t saved_b = REG_READ(GPIO_PIN_MUX_REG[pb]);
  auto pull_mode_of = [](uint32_t mux) {
    const bool pu = (mux & FUN_PU_M) != 0;
    const bool pd = (mux & FUN_PD_M) != 0;
    if (pu && pd)
      return GPIO_PULLUP_PULLDOWN;
    if (pu)
      return GPIO_PULLUP_ONLY;
    if (pd)
      return GPIO_PULLDOWN_ONLY;
    return GPIO_FLOATING;
  };

  gpio_set_pull_mode(pa, GPIO_PULLDOWN_ONLY);
  gpio_set_pull_mode(pb, GPIO_PULLDOWN_ONLY);
  // Der interne Pull-down liegt bei rund 45 kOhm, mit Leitungskapazitaet
  // braucht der Pegel etwas Zeit. 5 ms sind reichlich.
  delay(5);

  uint32_t ha = 0, hb = 0, n = 0;
  const uint32_t t_start = millis();
  while (millis() - t_start < 20) {
    ha += static_cast<uint32_t>(gpio_get_level(pa));
    hb += static_cast<uint32_t>(gpio_get_level(pb));
    n++;
  }

  gpio_set_pull_mode(pa, pull_mode_of(saved_a));
  gpio_set_pull_mode(pb, pull_mode_of(saved_b));

  if (high_a_pulldown)
    *high_a_pulldown = n ? (100.0f * static_cast<float>(ha) / static_cast<float>(n)) : -1.0f;
  if (high_b_pulldown)
    *high_b_pulldown = n ? (100.0f * static_cast<float>(hb) / static_cast<float>(n)) : -1.0f;
}

void PcntQuadratureSensor::sample_states(uint32_t duration_ms, uint32_t *state_counts,
                                         uint32_t *edges_a, uint32_t *edges_b, uint32_t *samples) {
  uint32_t counts[4] = {0, 0, 0, 0};
  uint32_t ea = 0, eb = 0, n = 0;

  if (this->pin_a_ != nullptr && this->pin_b_ != nullptr) {
    const gpio_num_t pa = static_cast<gpio_num_t>(this->pin_a_->get_pin());
    const gpio_num_t pb = static_cast<gpio_num_t>(this->pin_b_->get_pin());
    int la = gpio_get_level(pa);
    int lb = gpio_get_level(pb);
    const uint32_t t_start = millis();
    // Ohne Delay, damit auch kurze Zustaende erfasst werden. Der Aufrufer haelt
    // die Dauer bewusst klein, damit die Mainloop-Ueberwachung nicht anspricht.
    while (millis() - t_start < duration_ms) {
      const int a = gpio_get_level(pa);
      const int b = gpio_get_level(pb);
      if (a != la) {
        ea++;
        la = a;
      }
      if (b != lb) {
        eb++;
        lb = b;
      }
      counts[(static_cast<unsigned>(a) << 1) | static_cast<unsigned>(b)]++;
      n++;
    }
  }

  if (state_counts != nullptr) {
    for (int i = 0; i < 4; i++)
      state_counts[i] = counts[i];
  }
  if (edges_a != nullptr)
    *edges_a = ea;
  if (edges_b != nullptr)
    *edges_b = eb;
  if (samples != nullptr)
    *samples = n;
}

void PcntQuadratureSensor::set_pins_floating() {
  if (this->pin_a_ == nullptr || this->pin_b_ == nullptr)
    return;
  gpio_set_pull_mode(static_cast<gpio_num_t>(this->pin_a_->get_pin()), GPIO_FLOATING);
  gpio_set_pull_mode(static_cast<gpio_num_t>(this->pin_b_->get_pin()), GPIO_FLOATING);
  ESP_LOGW(TAG, "Interne Pull-Widerstaende an GPIO%u/GPIO%u abgeschaltet (konfigurierter Zustand)",
           this->pin_a_->get_pin(), this->pin_b_->get_pin());
}

void PcntQuadratureSensor::dump_pin_config() {
  if (this->pin_a_ == nullptr || this->pin_b_ == nullptr) {
    ESP_LOGE(TAG, "Pinkonfiguration nicht auslesbar: Pins fehlen");
    return;
  }

  // Rohes Eingangsregister lesen. Der S3 hat zwei Register: GPIO0..31 und ab 32.
  const uint32_t in_low = REG_READ(GPIO_IN_REG);
  const uint32_t in_high = REG_READ(GPIO_IN1_REG);

  const int pins[2] = {this->pin_a_->get_pin(), this->pin_b_->get_pin()};
  const char *const labels[2] = {"A", "B"};

  for (int i = 0; i < 2; i++) {
    const int pin = pins[i];
    const uint32_t mux = REG_READ(GPIO_PIN_MUX_REG[pin]);
    const bool input_enabled = (mux & FUN_IE_M) != 0;
    const bool pull_up = (mux & FUN_PU_M) != 0;
    const bool pull_down = (mux & FUN_PD_M) != 0;
    const uint32_t function = (mux >> MCU_SEL_S) & MCU_SEL_V;
    const int register_level =
        pin < 32 ? static_cast<int>((in_low >> pin) & 1U)
                 : static_cast<int>((in_high >> (pin - 32)) & 1U);

    ESP_LOGW(TAG,
             "GPIO%d (Kanal %s): gpio_get_level=%d, IN-Register=%d, Input-Enable=%s, "
             "Pull-up=%s, Pull-down=%s, IO-MUX-Funktion=%u, MUX-Register=0x%08X",
             pin, labels[i], gpio_get_level(static_cast<gpio_num_t>(pin)), register_level,
             YESNO(input_enabled), YESNO(pull_up), YESNO(pull_down), (unsigned) function,
             (unsigned) mux);
  }

  // Rohstand direkt aus der PCNT-Hardware, unabhaengig von unserer Akkumulation.
  if (this->unit_ != nullptr) {
    int raw = 0;
    const esp_err_t err = pcnt_unit_get_count(this->unit_, &raw);
    if (err == ESP_OK) {
      taskENTER_CRITICAL(&this->mux_);
      const int64_t accumulated = this->position_;
      const int32_t last = this->last_driver_count_;
      taskEXIT_CRITICAL(&this->mux_);
      ESP_LOGW(TAG, "PCNT: Rohzaehler=%d, letzter Rohwert=%d, akkumuliert=%lld, HW bereit=%s",
               raw, (int) last, (long long) accumulated, YESNO(this->hw_ready_));
    } else {
      ESP_LOGE(TAG, "PCNT-Rohzaehler nicht lesbar: %s", esp_err_to_name(err));
    }
  }
}

void PcntQuadratureSensor::dump_config() {
  LOG_SENSOR("", "PCNT Quadrature Encoder", this);
  LOG_PIN("  Pin A: ", this->pin_a_);
  LOG_PIN("  Pin B: ", this->pin_b_);
  ESP_LOGCONFIG(TAG, "  Resolution: x%u", static_cast<unsigned>(this->resolution_));
  ESP_LOGCONFIG(TAG, "  Direction inverted: %s", YESNO(this->invert_direction_));
  ESP_LOGCONFIG(TAG, "  Glitch filter: %" PRIu32 " us", this->filter_us_);
  LOG_UPDATE_INTERVAL(this);
  if (this->speed_sensor_ != nullptr)
    LOG_SENSOR("  ", "Speed", this->speed_sensor_);
}

}  // namespace esphome::pcnt_quadrature
