#pragma once

#ifdef USE_SELECT

#include "esphome/components/select/select.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

// Temperature unit select: Celsius/Fahrenheit
class WifiWhirlUnitSelect : public select::Select, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlUnitSelect(WifiWhirlComponent *parent) : parent_(parent) {}

  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    const auto &st = this->parent_->states();
    this->publish_state(st.unit ? "C" : "F");
  }

 protected:
  void control(const std::string &value) override {
    if (this->parent_ == nullptr) return;
    if (value == "C") {
      this->parent_->queue_command(SETUNIT, 1);
    } else if (value == "F") {
      this->parent_->queue_command(SETUNIT, 0);
    }
    this->publish_state(value);
  }

  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome

#endif  // USE_SELECT
