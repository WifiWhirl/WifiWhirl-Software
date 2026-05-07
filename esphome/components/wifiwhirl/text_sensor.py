import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_ERROR_TEXT = "error_text"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlErrorTextSensor = wifiwhirl_ns.class_("WifiWhirlErrorTextSensor", text_sensor.TextSensor)

ERROR_TEXT_SCHEMA = text_sensor.text_sensor_schema(WifiWhirlErrorTextSensor)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_ERROR_TEXT): ERROR_TEXT_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    if CONF_ERROR_TEXT in config:
        var = cg.new_Pvariable(config[CONF_ERROR_TEXT][CONF_ID], parent)
        await text_sensor.register_text_sensor(var, config[CONF_ERROR_TEXT])
        cg.add(parent.register_publisher(var))
