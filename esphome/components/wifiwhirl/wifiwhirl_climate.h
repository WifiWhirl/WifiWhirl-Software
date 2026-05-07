#pragma once

#ifdef USE_CLIMATE

#include "esphome/components/climate/climate.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlClimate : public climate::Climate, public WifiWhirlPublisher {
 public:
  explicit WifiWhirlClimate(WifiWhirlComponent *parent) : parent_(parent) {}

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();

    // ESPHome 2026.4+: use feature flags instead of deprecated set_supports_* helpers.
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);

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

    if (!st.power) {
      this->mode = climate::CLIMATE_MODE_OFF;
    } else if (st.heat) {
      this->mode = climate::CLIMATE_MODE_HEAT;
    } else if (st.pump) {
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
    } else {
      this->mode = climate::CLIMATE_MODE_OFF;
    }

    if (!st.power) {
      this->action = climate::CLIMATE_ACTION_OFF;
    } else if (st.heatred) {
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

      const auto &st = this->parent_->states();

      if (mode == climate::CLIMATE_MODE_HEAT) {
        // Ensure spa is powered on.
        if (!st.power) this->parent_->queue_command(TOGGLEPWR, 1);
        // For heating, the pump must be on.
        this->parent_->queue_command(SETPUMP, 1);
        this->parent_->queue_command(SETHEATER, 1);
      } else if (mode == climate::CLIMATE_MODE_FAN_ONLY) {
        if (!st.power) this->parent_->queue_command(TOGGLEPWR, 1);
        this->parent_->queue_command(SETHEATER, 0);
        this->parent_->queue_command(SETPUMP, 1);
      } else {
        // OFF: turn off heater/pump, then power down.
        this->parent_->queue_command(SETHEATER, 0);
        this->parent_->queue_command(SETPUMP, 0, "", /*force=*/true);
        if (st.power) this->parent_->queue_command(TOGGLEPWR, 1);
      }
      this->mode = mode;
    }

    this->publish_state();
  }

  WifiWhirlComponent *parent_;
};

}  // namespace wifiwhirl
}  // namespace esphome

#endif  // USE_CLIMATE
