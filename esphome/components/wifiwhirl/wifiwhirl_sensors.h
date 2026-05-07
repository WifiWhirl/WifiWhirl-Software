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

class WifiWhirlPowerSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlPowerSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->power_w());
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlEnergyTodaySensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlEnergyTodaySensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->energy_today_kwh());
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlEnergyTotalSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlEnergyTotalSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->energy_total_kwh());
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlUptimeSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlUptimeSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->uptime_s() / 3600.0f);
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlPumpTimeSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlPumpTimeSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->pump_time_s() / 3600.0f);
  }

 protected:
  WifiWhirlComponent *parent_;
};

class WifiWhirlHeaterTimeSensor : public sensor::Sensor, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlHeaterTimeSensor(WifiWhirlComponent *parent) : parent_(parent) {}
  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    this->publish_state(this->parent_->heater_time_s() / 3600.0f);
  }

 protected:
  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome
