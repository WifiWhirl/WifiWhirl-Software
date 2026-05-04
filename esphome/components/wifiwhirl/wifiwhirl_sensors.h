#pragma once

#include "esphome/components/sensor/sensor.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlTemperatureSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlTemperatureSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->temperature_c());
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlTargetTemperatureSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlTargetTemperatureSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->target_temperature_c());
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlErrorCodeSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlErrorCodeSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->error_code());
  }

 protected:
  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome
