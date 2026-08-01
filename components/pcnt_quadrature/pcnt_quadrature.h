#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <cstdint>

#if !defined(USE_ESP32)
#error "pcnt_quadrature requires an ESP32 target"
#endif

#include <soc/soc_caps.h>
#if !defined(SOC_PCNT_SUPPORTED)
#error "The selected ESP32 target has no PCNT peripheral"
#endif

#if !__has_include(<driver/pulse_cnt.h>)
#error "pcnt_quadrature requires the new ESP-IDF PCNT driver: driver/pulse_cnt.h"
#endif
#include <driver/pulse_cnt.h>

namespace esphome::pcnt_quadrature {

enum PcntQuadratureResolution : uint8_t {
  PCNT_QUADRATURE_X1 = 1,
  PCNT_QUADRATURE_X2 = 2,
  PCNT_QUADRATURE_X4 = 4,
};

class PcntQuadratureSensor : public sensor::Sensor, public PollingComponent {
 public:
  void set_pin_a(InternalGPIOPin *pin) { this->pin_a_ = pin; }
  void set_pin_b(InternalGPIOPin *pin) { this->pin_b_ = pin; }
  void set_resolution(PcntQuadratureResolution resolution) { this->resolution_ = resolution; }
  void set_invert_direction(bool invert) { this->invert_direction_ = invert; }
  void set_filter_us(uint32_t filter_us) { this->filter_us_ = filter_us; }
  void set_speed_sensor(sensor::Sensor *speed_sensor) { this->speed_sensor_ = speed_sensor; }

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  /** Richtet den PCNT einmalig waehrend setup() ein. Mehrfachaufrufe sind
   * unschaedlich; LiftMotor akzeptiert ohne fertige Hardware keine Fahrt. */
  bool init_hardware();
  bool is_hw_ready() const { return this->hw_ready_; }

  void set_position(int64_t position);
  void zero_position() { this->set_position(0); }
  int64_t get_position();

  /** Rohpegel der beiden Encoderleitungen ueber duration_ms abtasten.
   *
   * Liest die Eingangsregister direkt (gpio_get_level) und zaehlt Pegelwechsel.
   * Das laeuft parallel zum PCNT und aendert die Pin-Konfiguration NICHT -- im
   * Unterschied zu einem zusaetzlichen ESPHome-Binary-Sensor auf denselben
   * Pins, der dem PCNT frueher die Konfiguration weggenommen hat und zur
   * Falschdiagnose "Kanal B tot" fuehrte.
   *
   * Damit laesst sich beantworten, ob ueberhaupt Flanken ankommen, wenn der
   * Zaehler nichts zaehlt. Blockiert den aufrufenden Task fuer duration_ms.
   *
   * @param edges_a  Pegelwechsel auf Kanal A
   * @param edges_b  Pegelwechsel auf Kanal B
   * @param high_a   Anteil High-Samples auf A in Prozent
   * @param high_b   Anteil High-Samples auf B in Prozent
   * @param samples  Zahl der Abtastungen (Kontrolle der Abtastrate)
   */
  void sample_raw_levels(uint32_t duration_ms, uint32_t *edges_a, uint32_t *edges_b, float *high_a,
                         float *high_b, uint32_t *samples);

  /** Prueft, ob der Encoder seine Ausgaenge aktiv treibt.
   *
   * Beide Kanaele liegen konstant auf High. Mit den internen Pull-ups ist das
   * zweideutig: entweder treibt der Encoder aktiv High (dann lebt er und die
   * Welle steht einfach), oder sein Ausgang ist hochohmig (keine Versorgung,
   * Leitungsbruch, Baustein defekt) und man sieht nur den Pull-up.
   *
   * Der Test schaltet den internen Pull-up ab und einen Pull-down zu und misst
   * erneut. Bleibt der Pegel High, treibt eine Quelle aktiv gegen den
   * Pull-down. Faellt er auf Low, war es nur der Pull-up.
   *
   * Danach wird die urspruengliche Pull-up-Konfiguration wiederhergestellt.
   * gpio_set_pull_mode() aendert nur die Widerstaende, nicht die Anbindung an
   * den PCNT ueber die GPIO-Matrix -- der Zaehler laeuft weiter.
   *
   * @param high_a_pulldown Anteil High auf A mit Pull-down, in Prozent
   * @param high_b_pulldown Anteil High auf B mit Pull-down, in Prozent
   */
  void probe_pin_drive(float *high_a_pulldown, float *high_b_pulldown);

  /** Protokolliert die tatsaechliche GPIO-Konfiguration beider Kanaele.
   *
   * Anlass: Am Stecker liegen messbar 3,3 V, gpio_get_level() liefert aber 0.
   * Ein Pin, der elektrisch HIGH ist und im Register als 0 erscheint, deutet auf
   * einen abgeschalteten Eingangspfad (FUN_IE) oder eine falsche IO-MUX-Funktion
   * hin -- also auf ein Konfigurationsproblem im Chip, nicht auf Kabel oder
   * Sensor. Ausgegeben werden Pegel, das rohe GPIO-Eingangsregister,
   * Input-Enable, die internen Pull-Widerstaende, die IO-MUX-Funktion sowie der
   * Rohstand des PCNT-Zaehlers.
   */
  void dump_pin_config();

  /** Setzt beide Encoderpins auf den konfigurierten Zustand ohne internen Pull.
   *
   * Noetig, weil eine aeltere Fassung von probe_pin_drive() am Ende pauschal
   * Pull-up gesetzt hat. Ein einmal so verstellter Pin bleibt es bis zum
   * Neustart und verfaelscht jede weitere Messung -- ein abgezogener Encoder
   * liest dann HIGH, obwohl extern nichts hochzieht.
   */
  void set_pins_floating();

  /** Zaehlt ueber duration_ms die Haeufigkeit der vier Quadraturzustaende.
   *
   * Index = (A << 1) | B, also 0=00, 1=01, 2=10, 3=11.
   *
   * WOZU: Ob zwei Kanaele Flanken liefern, sagt nichts ueber ihre Phasenlage.
   * Gleich viele Flanken und je 50 Prozent High-Anteil ergeben sich bei 90 Grad
   * Versatz genauso wie bei gleichphasigen Signalen. Die Zustandsverteilung
   * trennt beides eindeutig:
   *   alle vier Zustaende vorhanden   -> echte Quadratur, dekodierbar
   *   nur 00 und 11                   -> gleichphasig, Schritte heben sich auf
   *   nur 01 und 10                   -> gegenphasig, ebenfalls nicht dekodierbar
   * Ein Quadraturzaehler kann bei den letzten beiden Faellen keine Richtung
   * bilden und bleibt netto bei null -- genau das beobachtete Verhalten.
   */
  void sample_states(uint32_t duration_ms, uint32_t *state_counts, uint32_t *edges_a,
                     uint32_t *edges_b, uint32_t *samples);

 protected:
  bool configure_channel_a_();
  bool configure_channel_b_();
  bool read_and_accumulate_();
  pcnt_channel_edge_action_t direction_adjust_(pcnt_channel_edge_action_t action) const;

  InternalGPIOPin *pin_a_{nullptr};
  InternalGPIOPin *pin_b_{nullptr};
  sensor::Sensor *speed_sensor_{nullptr};

  PcntQuadratureResolution resolution_{PCNT_QUADRATURE_X4};
  bool invert_direction_{false};
  uint32_t filter_us_{10};

  pcnt_unit_handle_t unit_{nullptr};
  pcnt_channel_handle_t channel_a_{nullptr};
  pcnt_channel_handle_t channel_b_{nullptr};

  // IDF's accum_count extends the physical 16-bit counter to int32_t.
  // This component then integrates modulo-2^32 deltas into int64_t.
  int32_t last_driver_count_{0};
  int64_t position_{0};
  bool hw_ready_{false};

  /** Schutz fuer die Akkumulation.
   *
   * get_position() wird sowohl aus dem ESPHome-Mainloop (Kern 0, Sensor-Update)
   * als auch aus dem Regeltask (Kern 1) aufgerufen. read_and_accumulate_()
   * veraendert position_ und last_driver_count_ nicht atomar -- ohne Sperre
   * wuerden sich beide Kerne den Zaehlerstand zerschiessen.
   */
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  uint32_t last_accumulate_ms_{0};
  uint32_t last_speed_ms_{0};
  int64_t last_speed_position_{0};

  static constexpr uint32_t ACCUMULATE_INTERVAL_MS = 100;
};

template<typename... Ts> class SetPositionAction : public Action<Ts...> {
 public:
  explicit SetPositionAction(PcntQuadratureSensor *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(int64_t, value)

  void play(Ts... x) override { this->parent_->set_position(this->value_.value(x...)); }

 protected:
  PcntQuadratureSensor *parent_;
};

template<typename... Ts> class ZeroPositionAction : public Action<Ts...> {
 public:
  explicit ZeroPositionAction(PcntQuadratureSensor *parent) : parent_(parent) {}

  // Signatur muss exakt zu Action<Ts...>::play passen (const-Referenzen).
  // Mit "Ts... x" liess sich die Action nur in Skripten OHNE Parameter
  // instanzieren; in motor_duty_scan (Parameter dir: int) schlug sie fehl.
  void play(const Ts &...x) override { this->parent_->zero_position(); }

 protected:
  PcntQuadratureSensor *parent_;
};

}  // namespace esphome::pcnt_quadrature
