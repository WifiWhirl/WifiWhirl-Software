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
CONF_POWER_W = "power_w"
CONF_ENERGY_TODAY_KWH = "energy_today_kwh"
CONF_ENERGY_TOTAL_KWH = "energy_total_kwh"
CONF_UPTIME_H = "uptime_h"
CONF_PUMP_TIME_H = "pump_time_h"
CONF_HEATER_TIME_H = "heater_time_h"

wifiwhirl_ns = cg.esphome_ns.namespace("wifiwhirl")
WifiWhirlTemperatureSensor = wifiwhirl_ns.class_("WifiWhirlTemperatureSensor", sensor.Sensor)
WifiWhirlTargetTemperatureSensor = wifiwhirl_ns.class_("WifiWhirlTargetTemperatureSensor", sensor.Sensor)
WifiWhirlErrorCodeSensor = wifiwhirl_ns.class_("WifiWhirlErrorCodeSensor", sensor.Sensor)
WifiWhirlPowerSensor = wifiwhirl_ns.class_("WifiWhirlPowerSensor", sensor.Sensor)
WifiWhirlEnergyTodaySensor = wifiwhirl_ns.class_("WifiWhirlEnergyTodaySensor", sensor.Sensor)
WifiWhirlEnergyTotalSensor = wifiwhirl_ns.class_("WifiWhirlEnergyTotalSensor", sensor.Sensor)
WifiWhirlUptimeSensor = wifiwhirl_ns.class_("WifiWhirlUptimeSensor", sensor.Sensor)
WifiWhirlPumpTimeSensor = wifiwhirl_ns.class_("WifiWhirlPumpTimeSensor", sensor.Sensor)
WifiWhirlHeaterTimeSensor = wifiwhirl_ns.class_("WifiWhirlHeaterTimeSensor", sensor.Sensor)

TEMPERATURE_SCHEMA = sensor.sensor_schema(
    WifiWhirlTemperatureSensor,
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

TARGET_TEMPERATURE_SCHEMA = sensor.sensor_schema(
    WifiWhirlTargetTemperatureSensor,
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

ERROR_CODE_SCHEMA = sensor.sensor_schema(
    WifiWhirlErrorCodeSensor,
    accuracy_decimals=0,
)

POWER_SCHEMA = sensor.sensor_schema(
    WifiWhirlPowerSensor,
    unit_of_measurement="W",
    accuracy_decimals=0,
    device_class="power",
    state_class=STATE_CLASS_MEASUREMENT,
)

ENERGY_TODAY_SCHEMA = sensor.sensor_schema(
    WifiWhirlEnergyTodaySensor,
    unit_of_measurement="kWh",
    accuracy_decimals=3,
    device_class="energy",
    state_class="total_increasing",
)

ENERGY_TOTAL_SCHEMA = sensor.sensor_schema(
    WifiWhirlEnergyTotalSensor,
    unit_of_measurement="kWh",
    accuracy_decimals=3,
    device_class="energy",
    state_class="total_increasing",
)

DURATION_H_SCHEMA = sensor.sensor_schema(
    WifiWhirlUptimeSensor,
    unit_of_measurement="h",
    accuracy_decimals=2,
    device_class="duration",
    state_class="total_increasing",
)

PUMP_TIME_H_SCHEMA = sensor.sensor_schema(
    WifiWhirlPumpTimeSensor,
    unit_of_measurement="h",
    accuracy_decimals=2,
    device_class="duration",
    state_class="total_increasing",
)

HEATER_TIME_H_SCHEMA = sensor.sensor_schema(
    WifiWhirlHeaterTimeSensor,
    unit_of_measurement="h",
    accuracy_decimals=2,
    device_class="duration",
    state_class="total_increasing",
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WIFIWHIRL_ID): cv.use_id(WifiWhirlComponent),
        cv.Optional(CONF_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_TARGET_TEMPERATURE): TARGET_TEMPERATURE_SCHEMA,
        cv.Optional(CONF_ERROR_CODE): ERROR_CODE_SCHEMA,
        cv.Optional(CONF_POWER_W): POWER_SCHEMA,
        cv.Optional(CONF_ENERGY_TODAY_KWH): ENERGY_TODAY_SCHEMA,
        cv.Optional(CONF_ENERGY_TOTAL_KWH): ENERGY_TOTAL_SCHEMA,
        cv.Optional(CONF_UPTIME_H): DURATION_H_SCHEMA,
        cv.Optional(CONF_PUMP_TIME_H): PUMP_TIME_H_SCHEMA,
        cv.Optional(CONF_HEATER_TIME_H): HEATER_TIME_H_SCHEMA,
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

    if CONF_POWER_W in config:
        var = cg.new_Pvariable(config[CONF_POWER_W][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_POWER_W])
        cg.add(parent.register_publisher(var))

    if CONF_ENERGY_TODAY_KWH in config:
        var = cg.new_Pvariable(config[CONF_ENERGY_TODAY_KWH][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_ENERGY_TODAY_KWH])
        cg.add(parent.register_publisher(var))

    if CONF_ENERGY_TOTAL_KWH in config:
        var = cg.new_Pvariable(config[CONF_ENERGY_TOTAL_KWH][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_ENERGY_TOTAL_KWH])
        cg.add(parent.register_publisher(var))

    if CONF_UPTIME_H in config:
        var = cg.new_Pvariable(config[CONF_UPTIME_H][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_UPTIME_H])
        cg.add(parent.register_publisher(var))

    if CONF_PUMP_TIME_H in config:
        var = cg.new_Pvariable(config[CONF_PUMP_TIME_H][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_PUMP_TIME_H])
        cg.add(parent.register_publisher(var))

    if CONF_HEATER_TIME_H in config:
        var = cg.new_Pvariable(config[CONF_HEATER_TIME_H][CONF_ID], parent)
        await sensor.register_sensor(var, config[CONF_HEATER_TIME_H])
        cg.add(parent.register_publisher(var))
