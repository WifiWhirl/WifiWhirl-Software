#pragma once

#include "esphome/components/climate/climate.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlClimate : public climate::Climate, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlClimate(WifiWhirlComponent *parent) : parent_(parent) {}

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();

    traits.set_supports_current_temperature(true);
    traits.set_supports_two_point_target_temperature(false);

    climate::ClimateModeMask modes;
    modes.insert(climate::CLIMATE_MODE_OFF);
    modes.insert(climate::CLIMATE_MODE_HEAT);
    modes.insert(climate::CLIMATE_MODE_FAN_ONLY);
    traits.set_supported_modes(modes);

    climate::ClimateFanModeMask fan_modes;
    traits.set_supported_fan_modes(fan_modes);

    traits.set_visual_min_temperature(20);
    traits.set_visual_max_temperature(40);
    traits.set_visual_temperature_step(1);

    // Temperature unit is handled by Home Assistant; ESPHome no longer exposes
    // set_temperature_unit() on ClimateTraits.
    return traits;
  }

  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    const auto &st = this->parent_->states();

    this->current_temperature = this->parent_->temperature_c();
    this->target_temperature = this->parent_->target_temperature_c();

    if (st.heat) {
      this->mode = climate::CLIMATE_MODE_HEAT;
    } else if (st.pump) {
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
    } else {
      this->mode = climate::CLIMATE_MODE_OFF;
    }

    if (st.heatred) {
      this->action = climate::CLIMATE_ACTION_HEATING;
    } else if (st.heatgrn) {
      this->action = climate::CLIMATE_ACTION_IDLE;
    } else if (st.pump) {
      this->action = climate::CLIMATE_ACTION_FAN;
    } else {
      this->action = climate::CLIMATE_ACTION_OFF;
    }

    this->publish_state();
  }

 protected:
  void control(const climate::ClimateCall &call) override {
    if (this->parent_ == nullptr) return;

    if (call.get_target_temperature().has_value()) {
      const float temp = *call.get_target_temperature();
      const int64_t target = static_cast<int64_t>(lroundf(temp));
      this->parent_->queue_command(SETTARGET, target);
      this->target_temperature = temp;
    }

    if (call.get_mode().has_value()) {
      const auto mode = *call.get_mode();

      if (mode == climate::CLIMATE_MODE_HEAT) {
        // For heating, the pump must be on.
        this->parent_->queue_command(SETPUMP, 1);
        this->parent_->queue_command(SETHEATER, 1);
      } else if (mode == climate::CLIMATE_MODE_FAN_ONLY) {
        this->parent_->queue_command(SETHEATER, 0);
        this->parent_->queue_command(SETPUMP, 1);
      } else {
        // OFF
        this->parent_->queue_command(SETHEATER, 0);
        this->parent_->queue_command(SETPUMP, 0, "", /*force=*/true);
      }
      this->mode = mode;
    }

    this->publish_state();
  }

  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome
