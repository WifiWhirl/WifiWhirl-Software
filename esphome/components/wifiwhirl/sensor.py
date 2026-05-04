import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

from . import WifiWhirlComponent

DEPENDENCIES = ["wifiwhirl"]

CONF_WIFIWHIRL_ID = "wifiwhirl_id"
CONF_TEMPERATURE = "temperature"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_ERROR_CODE = "error_code"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlTemperatureSensor = wifiwhirl_ns.class_("WifiWhirlTemperatureSensor", sensor.Sensor)
WifiWhirlTargetTemperatureSensor = wifiwhirl_ns.class_("WifiWhirlTargetTemperatureSensor", sensor.Sensor)
WifiWhirlErrorCodeSensor = wifiwhirl_ns.class_("WifiWhirlErrorCodeSensor", sensor.Sensor)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_TARGET_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
            accuracy_decimals=0,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WIFIWHIRL_ID])

    if CONF_TEMPERATURE in config:
        var = cg.new_Pvariable(config[CONF_TEMPERATURE][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_TEMPERATURE])
        cg.add(parent.register_publisher(var))

    if CONF_TARGET_TEMPERATURE in config:
        var = cg.new_Pvariable(config[CONF_TARGET_TEMPERATURE][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_TARGET_TEMPERATURE])
        cg.add(parent.register_publisher(var))

    if CONF_ERROR_CODE in config:
        var = cg.new_Pvariable(config[CONF_ERROR_CODE][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_ERROR_CODE])
        cg.add(parent.register_publisher(var))
