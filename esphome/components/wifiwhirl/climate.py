import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlClimate = wifiwhirl_ns.class_("WifiWhirlClimate", climate.Climate)

# ESPHome 2026.4+: CLIMATE_SCHEMA was replaced by climate.climate_schema(...)
CONFIG_SCHEMA = climate.climate_schema(WifiWhirlClimate).extend(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await climate.register_climate(var, config)
    cg.add(parent.register_publisher(var))
