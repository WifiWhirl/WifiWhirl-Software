#include "wifiwhirl_component.h"

#include "esphome/core/log.h"

#ifdef ESP8266
#include <LittleFS.h>
#endif

namespace esphome {
namespace wifiwhirl {

static const char *const TAG = "wifiwhirl";

void WifiWhirlComponent::setup() {
  ESP_LOGI(TAG, "Setting up WifiWhirl (ESPHome external component)");

#ifdef ESP8266
  // WifiWhirl expects LittleFS to exist (even if we don't use it yet).
  // ESPHome itself does not guarantee that LittleFS is mounted.
  if (!LittleFS.begin()) {
    ESP_LOGW(TAG, "LittleFS.begin() failed (continuing without FS)");
  }
#endif

  this->bwc_ = new BWC();

  // We intentionally bypass WifiWhirl's hwcfg.json filesystem path and
  // configure hardware explicitly from ESPHome YAML.
  auto model = static_cast<Models>(this->model_);
  this->bwc_->setup_hardware(model, model, this->pins_);

  // One initial loop to populate baseline state.
  this->bwc_->loop();
}

void WifiWhirlComponent::loop() {
  if (this->bwc_ == nullptr) {
    return;
  }

  // Run the core MITM state machine as often as possible.
  this->bwc_->loop();

  // Publish state to ESPHome entities at a safe rate.
  const uint32_t now = millis();
  if (now - this->last_publish_ms_ < 1000) {
    return;
  }
  this->last_publish_ms_ = now;

  for (auto *pub : this->publishers_) {
    pub->wifiwhirl_publish();
  }
}

bool WifiWhirlComponent::queue_command(Commands cmd, int64_t val, const char *text, bool force) {
  if (this->bwc_ == nullptr) {
    return false;
  }

  command_que_item item;
  item.cmd = cmd;
  item.val = val;
  item.xtime = 0;
  item.interval = 0;
  item.text = text != nullptr ? String(text) : String("");
  item.force = force;

  return this->bwc_->add_command(item);
}

void WifiWhirlComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "WifiWhirl:");
  ESP_LOGCONFIG(TAG, "  Model: %u", this->model_);
  ESP_LOGCONFIG(TAG, "  CIO pins: data=%d clk=%d cs=%d", this->pins_[0], this->pins_[1], this->pins_[2]);
  ESP_LOGCONFIG(TAG, "  DSP pins: data=%d clk=%d cs=%d audio=%d", this->pins_[3], this->pins_[4], this->pins_[5], this->pins_[6]);
}

}  // namespace wifiwhirl
}  // namespace esphome
