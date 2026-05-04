import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID, CONF_NAME

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlClimate = wifiwhirl_ns.class_("WifiWhirlClimate", climate.Climate)

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.GenerateID(): cv.declare_id(WifiWhirlClimate),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await climate.register_climate(var, config)
    cg.add(parent.register_publisher(var))
