import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID

CODEOWNERS = ["@Finest"]

# ESPHome only copies C++ sources that are located directly in the component
# directory. Our vendored WifiWhirl library sources live in separate helper
# components so they get included in the build.
AUTO_LOAD = ["wifiwhirl_bwc_unified", "wifiwhirl_cio", "wifiwhirl_dsp"]

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlComponent = wifiwhirl_ns.class_("WifiWhirlComponent", cg.Component)

CONF_MODEL = "model"
CONF_CIO_DATA_PIN = "cio_data_pin"
CONF_CIO_CLK_PIN = "cio_clk_pin"
CONF_CIO_CS_PIN = "cio_cs_pin"
CONF_DSP_DATA_PIN = "dsp_data_pin"
CONF_DSP_CLK_PIN = "dsp_clk_pin"
CONF_DSP_CS_PIN = "dsp_cs_pin"
CONF_DSP_AUDIO_PIN = "dsp_audio_pin"

MODEL_MIAMI2021 = "miami2021"
MODEL_MALDIVES2021 = "maldives2021"
MODEL_PRE2021 = "pre2021"

MODEL_CHOICES = {
    MODEL_PRE2021: 0,      # PRE2021 enum value
    MODEL_MIAMI2021: 1,    # MIAMI2021 enum value
    MODEL_MALDIVES2021: 2, # MALDIVES2021 enum value
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WifiWhirlComponent),
        cv.Optional(CONF_MODEL, default=MODEL_MIAMI2021): cv.one_of(*MODEL_CHOICES, lower=True),
        cv.Required(CONF_CIO_DATA_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_CIO_CLK_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_CIO_CS_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DSP_DATA_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DSP_CLK_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DSP_CS_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DSP_AUDIO_PIN): pins.internal_gpio_input_pin_number,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Ensure generated code sees the WifiWhirl umbrella header.
    cg.add_global(cg.RawExpression('#include "wifiwhirl.h"'))

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    model_enum = MODEL_CHOICES[config[CONF_MODEL]]

    cg.add(var.set_model(model_enum))
    cg.add(var.set_pins(
        config[CONF_CIO_DATA_PIN],
        config[CONF_CIO_CLK_PIN],
        config[CONF_CIO_CS_PIN],
        config[CONF_DSP_DATA_PIN],
        config[CONF_DSP_CLK_PIN],
        config[CONF_DSP_CS_PIN],
        config[CONF_DSP_AUDIO_PIN],
    ))
