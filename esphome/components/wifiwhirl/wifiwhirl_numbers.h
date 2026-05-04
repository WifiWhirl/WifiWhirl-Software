#pragma once

#include "esphome/components/number/number.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlTargetTemperatureNumber : public number::Number, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlTargetTemperatureNumber(WifiWhirlComponent *parent) : parent_(parent) {}

  void set_min_value(float v) { this->min_value_ = v; }
  void set_max_value(float v) { this->max_value_ = v; }
  void set_step(float v) { this->step_ = v; }

  number::NumberTraits traits() override {
    auto traits = number::NumberTraits();
    traits.set_min_value(this->min_value_);
    traits.set_max_value(this->max_value_);
    traits.set_step(this->step_);
    return traits;
  }

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
  float min_value_{20.0f};
  float max_value_{40.0f};
  float step_{1.0f};
};

}  // namespace wifiwhirl
}  // namespace esphome
