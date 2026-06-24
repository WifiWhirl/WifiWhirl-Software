#pragma once

#ifdef USE_BUTTON

#include "esphome/components/button/button.h"
#include "wifiwhirl_component.h"

namespace esphome {
namespace wifiwhirl {

enum WifiWhirlButtonKind : uint8_t {
  BEEP,
  ACCORD,
};

class WifiWhirlButton : public button::Button {
 public:
  WifiWhirlButton(WifiWhirlComponent *parent, WifiWhirlButtonKind kind) : parent_(parent), kind_(kind) {}

 protected:
  void press_action() override {
    if (this->parent_ == nullptr) return;
    switch (this->kind_) {
      case BEEP:
        this->parent_->queue_command(SETBEEP, 0);
        break;
      case ACCORD:
        this->parent_->queue_command(SETBEEP, 1);
        break;
    }
  }

  WifiWhirlComponent *parent_;
  WifiWhirlButtonKind kind_;
};

}  // namespace wifiwhirl
}  // namespace esphome

#endif  // USE_BUTTON
