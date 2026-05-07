import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_BEEP = "beep"
CONF_ACCORD = "accord"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlButton = wifiwhirl_ns.class_("WifiWhirlButton", button.Button)
WifiWhirlButtonKind = wifiwhirl_ns.enum("WifiWhirlButtonKind")

KIND_MAP = {
    CONF_BEEP: WifiWhirlButtonKind.BEEP,
    CONF_ACCORD: WifiWhirlButtonKind.ACCORD,
}

BUTTON_SCHEMA = button.button_schema(WifiWhirlButton)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_BEEP): BUTTON_SCHEMA,
        cv.Optional(CONF_ACCORD): BUTTON_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    for key, kind in KIND_MAP.items():
        if key not in config:
            continue
        var = cg.new_Pvariable(config[key][CONF_ID], parent, kind)
        await button.register_button(var, config[key])
