#pragma once

#include "esphome/components/switch/switch.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

enum class WifiWhirlSwitchKind : uint8_t {
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
      case WifiWhirlSwitchKind::HEATER:
        on = st.heat;
        break;
      case WifiWhirlSwitchKind::PUMP:
        on = st.pump;
        break;
      case WifiWhirlSwitchKind::BUBBLES:
        on = st.bubbles;
        break;
      case WifiWhirlSwitchKind::JETS:
        on = st.jets;
        break;
      case WifiWhirlSwitchKind::POWER:
        on = st.power;
        break;
      case WifiWhirlSwitchKind::LOCK:
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
      case WifiWhirlSwitchKind::HEATER:
        this->parent_->queue_command(SETHEATER, state ? 1 : 0);
        break;
      case WifiWhirlSwitchKind::PUMP:
        // Force allows pump-off while heater is running by inserting heater-off first.
        this->parent_->queue_command(SETPUMP, state ? 1 : 0, "", /*force=*/true);
        break;
      case WifiWhirlSwitchKind::BUBBLES:
        this->parent_->queue_command(SETBUBBLES, state ? 1 : 0);
        break;
      case WifiWhirlSwitchKind::JETS:
        this->parent_->queue_command(SETJETS, state ? 1 : 0);
        break;
      case WifiWhirlSwitchKind::POWER: {
        // Power is a toggle in the protocol; only toggle if a change is needed.
        if (static_cast<bool>(st.power) != state) {
          this->parent_->queue_command(TOGGLEPWR, 1);
        }
        break;
      }
      case WifiWhirlSwitchKind::LOCK: {
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
