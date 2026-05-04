#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <cstdint>
#include <vector>

// WifiWhirl library (vendored into this external component)
#include "wifiwhirl_lib/BWC_unified/bwc.h"

namespace esphome {
namespace wifiwhirl {

class WifiWhirlPublisher {
 public:
  virtual void wifiwhirl_publish() = 0;
  virtual ~WifiWhirlPublisher() = default;
};

class WifiWhirlComponent : public Component {
 public:
  void set_model(uint8_t model) { this->model_ = model; }

  void set_pins(int cio_data, int cio_clk, int cio_cs, int dsp_data, int dsp_clk, int dsp_cs, int dsp_audio) {
    this->pins_[0] = cio_data;
    this->pins_[1] = cio_clk;
    this->pins_[2] = cio_cs;
    this->pins_[3] = dsp_data;
    this->pins_[4] = dsp_clk;
    this->pins_[5] = dsp_cs;
    this->pins_[6] = dsp_audio;
    this->pins_[7] = 231;  // reserved (onewire temp pin placeholder in original project)
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

  void register_publisher(WifiWhirlPublisher *pub) { this->publishers_.push_back(pub); }

  // Read-only accessors for current state.
  const sStates &states() const { return this->bwc_->cio->cio_states; }

  // Convenience helpers.
  float temperature_c() const { return static_cast<float>(this->states().temperature); }
  float target_temperature_c() const { return static_cast<float>(this->states().target); }
  int error_code() const { return static_cast<int>(this->states().error); }

  // Queue a command into the WifiWhirl command queue.
  bool queue_command(Commands cmd, int64_t val, const char *text = "", bool force = false);

 protected:
  uint8_t model_{1};
  int pins_[8]{};

  BWC *bwc_{nullptr};

  std::vector<WifiWhirlPublisher *> publishers_;
  uint32_t last_publish_ms_{0};
};

}  // namespace wifiwhirl
}  // namespace esphome
