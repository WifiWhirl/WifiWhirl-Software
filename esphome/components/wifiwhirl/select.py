import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_UNIT = "unit"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlUnitSelect = wifiwhirl_ns.class_("WifiWhirlUnitSelect", select.Select)

UNIT_SCHEMA = select.select_schema(WifiWhirlUnitSelect)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_UNIT): UNIT_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    if CONF_UNIT in config:
        var = cg.new_Pvariable(config[CONF_UNIT][CONF_ID], parent)
        await select.register_select(var, config[CONF_UNIT])
        cg.add(parent.register_publisher(var))
