#pragma once

#ifdef USE_TEXT_SENSOR

#include "esphome/components/text_sensor/text_sensor.h"

#include <string>

#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlErrorTextSensor : public text_sensor::TextSensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlErrorTextSensor(WifiWhirlComponent *parent) : parent_(parent) {}

  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    const int code = this->parent_->error_code();

    if (code <= 0) {
      this->publish_state("OK");
      return;
    }

    std::string msg;
    switch (code) {
      case 1:
        msg =
            "E01: Wasserdurchflusssensor defekt oder blockiert. Lösung: Filter reinigen, Pumpe kurz "
            "vom Stromnetz trennen und die Sensoren auf Verschmutzung prüfen.";
        break;
      case 2:
        msg =
            "E02: Durchflussproblem. Lösung: Prüfen, ob die Ventile geöffnet sind und der Filter sauber "
            "ist. Luft im System? Pumpe entlüften.";
        break;
      case 3:
        msg = "E03: Wassertemperatur unter 4°C. Lösung: Wasser erwärmen oder vor Frost schützen.";
        break;
      case 8:
        msg =
            "E08: Überhitzung (Wassereinheit über 52°C oder Pumpe in der Sonne). Lösung: Pumpe "
            "ausschalten, abkühlen lassen und Wasser auf unter 35°C bringen.";
        break;
      case 9:
        msg = "E09: Sicherungsausfall der Pumpeneinheit. Lösung: Kundenservice kontaktieren.";
        break;
      case 10:
        msg = "E10: Zu hoher Salzgehalt. Lösung: Wasser ablassen und teilweise durch Frischwasser ersetzen.";
        break;
      default:
        msg = "E" + std::to_string(code) + ": Unbekannter Fehlercode";
        break;
    }

    this->publish_state(msg);
  }

 protected:
  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome

#endif  // USE_TEXT_SENSOR
