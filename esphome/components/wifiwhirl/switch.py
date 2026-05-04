import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_NAME

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_HEATER = "heater"
CONF_PUMP = "pump"
CONF_BUBBLES = "bubbles"
CONF_JETS = "jets"
CONF_POWER = "power"
CONF_LOCK = "lock"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlSwitch = wifiwhirl_ns.class_("WifiWhirlSwitch", switch.Switch)
WifiWhirlSwitchKind = wifiwhirl_ns.enum("WifiWhirlSwitchKind")

KIND_MAP = {
    CONF_HEATER: WifiWhirlSwitchKind.HEATER,
    CONF_PUMP: WifiWhirlSwitchKind.PUMP,
    CONF_BUBBLES: WifiWhirlSwitchKind.BUBBLES,
    CONF_JETS: WifiWhirlSwitchKind.JETS,
    CONF_POWER: WifiWhirlSwitchKind.POWER,
    CONF_LOCK: WifiWhirlSwitchKind.LOCK,
}

SWITCH_SCHEMA = switch.switch_schema(WifiWhirlSwitch)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_HEATER): SWITCH_SCHEMA,
        cv.Optional(CONF_PUMP): SWITCH_SCHEMA,
        cv.Optional(CONF_BUBBLES): SWITCH_SCHEMA,
        cv.Optional(CONF_JETS): SWITCH_SCHEMA,
        cv.Optional(CONF_POWER): SWITCH_SCHEMA,
        cv.Optional(CONF_LOCK): SWITCH_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    for key, kind in KIND_MAP.items():
        if key not in config:
            continue
        var = cg.new_Pvariable(config[key][CONF_ID], parent, kind)
        await switch.register_switch(var, config[key])
        cg.add(parent.register_publisher(var))
