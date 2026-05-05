#pragma once

#include "esphome/components/switch/switch.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

// Note: ESPHome codegen currently generates unscoped enum values (wifiwhirl::HEATER, ...).
// Using an unscoped enum here keeps the generated code compatible.
enum WifiWhirlSwitchKind : uint8_t {
  HEATER,
  PUMP,
  BUBBLES,
  JETS,
  POWER,
  LOCK,
};

class WifiWhirlSwitch : public switch_::Switch, public WifiWhirlPublisher {
 public:
  WifiWhirlSwitch(WifiWhirlComponent *parent, WifiWhirlSwitchKind kind) : parent_(parent), kind_(kind) {}

  void wifiwhirl_publish() override {
    if (this->parent_ == nullptr) return;
    const auto &st = this->parent_->states();

    bool on = false;
    switch (this->kind_) {
      case HEATER:
        on = st.heat;
        break;
      case PUMP:
        on = st.pump;
        break;
      case BUBBLES:
        on = st.bubbles;
        break;
      case JETS:
        on = st.jets;
        break;
      case POWER:
        on = st.power;
        break;
      case LOCK:
        on = st.locked;
        break;
    }

    this->publish_state(on);
  }

 protected:
  void write_state(bool state) override {
    if (this->parent_ == nullptr) {
      return;
    }

    const auto &st = this->parent_->states();

    switch (this->kind_) {
      case HEATER:
        this->parent_->queue_command(SETHEATER, state ? 1 : 0);
        break;
      case PUMP:
        // Force allows pump-off while heater is running by inserting heater-off first.
        this->parent_->queue_command(SETPUMP, state ? 1 : 0, "", /*force=*/true);
        break;
      case BUBBLES:
        this->parent_->queue_command(SETBUBBLES, state ? 1 : 0);
        break;
      case JETS:
        this->parent_->queue_command(SETJETS, state ? 1 : 0);
        break;
      case POWER: {
        // Power is a toggle in the protocol; only toggle if a change is needed.
        if (static_cast<bool>(st.power) != state) {
          this->parent_->queue_command(TOGGLEPWR, 1);
        }
        break;
      }
      case LOCK: {
        if (static_cast<bool>(st.locked) != state) {
          this->parent_->queue_command(TOGGLELCK, 1);
        }
        break;
      }
    }

    // Optimistic update.
    this->publish_state(state);
  }

  WifiWhirlComponent *parent_;
  WifiWhirlSwitchKind kind_;
};

}  // namespace wifiwhirl
}  // namespace esphome
