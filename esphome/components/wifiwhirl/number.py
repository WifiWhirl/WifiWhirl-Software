import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_BRIGHTNESS = "brightness"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlTargetTemperatureNumber = wifiwhirl_ns.class_(
    "WifiWhirlTargetTemperatureNumber", number.Number
)
WifiWhirlBrightnessNumber = wifiwhirl_ns.class_("WifiWhirlBrightnessNumber", number.Number)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_TARGET_TEMPERATURE): number.number_schema(
            WifiWhirlTargetTemperatureNumber,
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            icon="mdi:thermometer",
        ).extend(
            {
                cv.Optional("min_value", default=20): cv.int_,
                cv.Optional("max_value", default=40): cv.int_,
                cv.Optional("step", default=1): cv.int_,
            }
        ),
        cv.Optional(CONF_BRIGHTNESS): number.number_schema(
            WifiWhirlBrightnessNumber,
            icon="mdi:brightness-6",
        ).extend(
            {
                cv.Optional("min_value", default=1): cv.int_,
                cv.Optional("max_value", default=8): cv.int_,
                cv.Optional("step", default=1): cv.int_,
            }
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    if CONF_TARGET_TEMPERATURE in config:
        cfg = config[CONF_TARGET_TEMPERATURE]
        var = cg.new_Pvariable(cfg[CONF_ID], parent)

        # ESPHome 2026.4+: min/max/step are required keyword-only args
        await number.register_number(
            var,
            cfg,
            min_value=float(cfg["min_value"]),
            max_value=float(cfg["max_value"]),
            step=float(cfg["step"]),
        )

        cg.add(parent.register_publisher(var))

    if CONF_BRIGHTNESS in config:
        cfg = config[CONF_BRIGHTNESS]
        var = cg.new_Pvariable(cfg[CONF_ID], parent)
        await number.register_number(
            var,
            cfg,
            min_value=float(cfg["min_value"]),
            max_value=float(cfg["max_value"]),
            step=float(cfg["step"]),
        )
        cg.add(parent.register_publisher(var))
