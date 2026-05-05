#pragma once

#include "esphome/components/number/number.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlTargetTemperatureNumber : public number::Number, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlTargetTemperatureNumber(WifiWhirlComponent *parent) : parent_(parent) {}

  // Keep setters for convenience/testing; ESPHome codegen normally writes directly to `traits`.
  void set_min_value(float v) { this->traits.set_min_value(v); }
  void set_max_value(float v) { this->traits.set_max_value(v); }
  void set_step(float v) { this->traits.set_step(v); }

  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->target_temperature_c());
  }

 protected:
  void control(float value) override {
    if (this->parent_ == nullptr) return;
    // WifiWhirl expects integer temperatures.
    const int64_t target = static_cast<int64_t>(lroundf(value));
    this->parent_->queue_command(SETTARGET, target);
    this->publish_state(value);
  }

  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome
