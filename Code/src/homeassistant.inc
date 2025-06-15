/*
    Note, following does not work!
    doc[F("key")] = F("Value")
    String s; doc[s] = ...
    Throws all kind of errors. String throws stack smashing.
*/

const char *_dev = "dev";
const char *_name = "name";
const char *_uniq_id = "uniq_id";
const char *_stat_t = "stat_t";
const char *_avty_t = "avty_t";
const char *_pl_avail = "pl_avail";
const char *_pl_not_avail = "pl_not_avail";
const char *_val_tpl = "val_tpl";
const char *_cmd_t = "cmd_t";
const char *_cmd_tpl = "cmd_tpl";

struct HaEntityConfig
{
    const char *component;
    const char *entity_id_suffix;
    const char *name_suffix;
    const char *uniq_id_prefix;
    const char *stat_t_suffix; // Suffix for state topic
    const char *val_tpl;
    const char *cmd_t_suffix; // Suffix for command topic
    const char *cmd_tpl;
    const char *icon;
    const char *unit_of_meas;
    const char *dev_cla;
    const char *state_class;
    const char *expire_after;
    const char *pl_on;
    const char *pl_off;
    const char *state_on;
    const char *state_off;
    const char *payload_press;
    const char *min;
    const char *max;
    const char *mode;
    const char *assumed_state;
};

static const HaEntityConfig entities[] = {
    {"number", "_brightness", "Helligkeit", "number.", "/message", "{{ value_json.BRT }}", "/command", "{CMD:12,VALUE:{{ value | int }},XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "0", "8", "slider", nullptr},
    {"sensor", "_reboot_time", "Letzter Neustart", "sensor.", "/reboot_time", "{{as_datetime(value)}}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_reboot_reason", "Neustartgrund", "sensor.", "/reboot_reason", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_ssid", "WLAN-SSID", "sensor.", "/other", "{{ value_json.SSID }}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_rssi", "WLAN-RSSI", "sensor.", "/other", "{{ value_json.RSSI }}", nullptr, nullptr, "mdi:wifi", "dBm", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_ip", "IP-Adresse", "sensor.", "/other", "{{ value_json.IP }}", nullptr, nullptr, "mdi:ip", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_connect_count", "MQTT-Verbindungszähler", "sensor.", "/MQTT_Connect_Count", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_error", "Fehler", "sensor.", "/message", "{{ value_json.ERR }}", nullptr, nullptr, "mdi:alert-circle-outline", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_time_to_ready", "Bereit in", "sensor.", "/times", "{{ value_json.T2R | round(2) }}", nullptr, nullptr, "mdi:clock", "Stunden", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_rs", "Bereitschaftsstatus", "sensor.", "/times", "{{ value_json.RS }}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_energy", "Gesamtenergieverbrauch", "sensor.", "/times", "{{ value_json.KWH | round(3) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_today", "Energieverbrauch heute", "sensor.", "/times", "{{ value_json.KWHD | round(3) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_power", "Aktuelle Leistungsaufnahme", "sensor.", "/times", "{{ value_json.WATT | int }}", nullptr, nullptr, "mdi:flash", "W", "power", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_chlorine_age", "Letzte Chlorung", "sensor.", "/times", "{{ ( ( (now().timestamp()|int) - value_json.CLTIME|int)/3600/24) | round(2) }}", nullptr, nullptr, "hass:hand-coin-outline", "Tage", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_age", "Letzter Filterwechsel", "sensor.", "/times", "{{ ( ( (now().timestamp()|int) - value_json.FTIME|int)/3600/24) | round(2) }}", nullptr, nullptr, "hass:air-filter", "Tage", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_clean_age", "Letzte Filterreinigung", "sensor.", "/times", "{{ ( ( (now().timestamp()|int) - value_json.FCTIME|int)/3600/24) | round(2) }}", nullptr, nullptr, "mdi:filter-check-outline", "Tage", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_water_change_age", "Letzter Wasserwechsel", "sensor.", "/times", "{{ ( ( (now().timestamp()|int) - value_json.WCTIME|int)/3600/24) | round(2) }}", nullptr, nullptr, "mdi:water-sync", "Tage", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_uptime", "Betriebszeit", "sensor.", "/times", "{{ ( (value_json.UPTIME|int)/3600/24) | round(2) }}", nullptr, nullptr, "mdi:clock-outline", "Tage", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_pumptime", "Pumpenlaufzeit", "sensor.", "/times", "{{ ( (value_json.PUMPTIME|int)/3600) | round(2) }}", nullptr, nullptr, "mdi:clock-outline", "Stunden", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_heatertime", "Heizungslaufzeit", "sensor.", "/times", "{{ ( (value_json.HEATINGTIME|int)/3600) | round(2) }}", nullptr, nullptr, "mdi:clock-outline", "Stunden", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_airtime", "AirJet Laufzeit", "sensor.", "/times", "{{ ( (value_json.AIRTIME|int)/3600) | round(2) }}", nullptr, nullptr, "mdi:clock-outline", "Stunden", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_temperature_f", "Temperatur (F)", "sensor.", "/message", "{{ value_json.TMPF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_temp_c", "Aktuelle Temperatur", "sensor.", "/message", "{{ value_json.TMPC }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_virtual_temp_f", "Virtuelle Temperatur (F)", "sensor.", "/message", "{{ value_json.VTMF | round(2) }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_virtual_temp_c", "Virtuelle Temperatur", "sensor.", "/message", "{{ value_json.VTMC | round(2) }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_target_temp_f", "Solltemperatur (F)", "sensor.", "/message", "{{ value_json.TGTF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_target_temp_c", "Zieltemperatur", "sensor.", "/message", "{{ value_json.TGTC }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_amb_temp_c", "Umgebungstemperatur", "sensor.", "/message", "{{ value_json.AMBC }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"number", "_amb_temp_c_manual", "Umgebungstemperatur", "number.", "/message", "{{ value_json.AMBC }}", "/command", "{CMD:15,VALUE:{{ value | int }},XTIME:0,INTERVAL:0}", "mdi:thermometer", "°C", "temperature", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "-20", "50", "box", nullptr},
    {"binary_sensor", "_lock", "Tastensperre", "binary_sensor.", "/message", "{% if value_json.LCK == 1 %}OFF{% else %}ON{% endif %}", nullptr, nullptr, nullptr, nullptr, "lock", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"binary_sensor", "_heater", "Heizung", "binary_sensor.", "/message", "{% if value_json.RED == 1 %}ON{% else %}OFF{% endif %}", nullptr, nullptr, nullptr, nullptr, "heat", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"binary_sensor", "_ready", "Bereit", "binary_sensor.", "/message", "{% if value_json.TMP > 30 %}{% if value_json.TMP >= value_json.TGT-1 %}ON{% else %}OFF{% endif %}{% else %}OFF{% endif %}", nullptr, nullptr, "mdi:hot-tub", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"binary_sensor", "_connection", "Verbindung", "binary_sensor.", "/Status", nullptr, nullptr, nullptr, nullptr, nullptr, "connectivity", nullptr, nullptr, "Alive", "Dead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_heat_regulation", "Heizung", "switch.", "/message", "{% if value_json.RED == 1 %}1{% elif value_json.GRN == 1 %}1{% else %}0{% endif %}", "/command", nullptr, "mdi:radiator", nullptr, nullptr, nullptr, "700", "{CMD:3,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:3,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_jets", "HydroJet", "switch.", "/message", "{{ value_json.HJT }}", "/command", nullptr, "mdi:hydro-power", nullptr, nullptr, nullptr, "700", "{CMD:11,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:11,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_airbubbles", "AirJet", "switch.", "/message", "{{ value_json.AIR }}", "/command", nullptr, "mdi:chart-bubble", nullptr, nullptr, nullptr, "700", "{CMD:2,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:2,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_pump", "Filterpumpe", "switch.", "/message", "{{ value_json.FLT }}", "/command", nullptr, "mdi:pump", nullptr, nullptr, nullptr, "700", "{CMD:4,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:4,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_unit", "°F/°C", "switch.", "/message", "{{ value_json.UNT }}", "/command", nullptr, "mdi:circle-outline", nullptr, nullptr, nullptr, "700", "{CMD:1,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:1,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"switch", "_ctrl", "Steuerung übernehmen", "switch.", "/message", "{{ value_json.GOD }}", "/command", nullptr, "mdi:steering", nullptr, nullptr, nullptr, "700", "{CMD:17,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:17,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, "0"},
    {"switch", "_power", "Ein/Aus", "switch.", "/message", "{{ value_json.PWR }}", "/command", nullptr, "mdi:power", nullptr, nullptr, nullptr, "700", "{CMD:22,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:22,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_lock", "Tastensperre", "switch.", "/message", "{{ value_json.LCK }}", "/command", nullptr, "mdi:lock", nullptr, nullptr, nullptr, "700", "{CMD:23,VALUE:true,XTIME:0,INTERVAL:0}", "{CMD:23,VALUE:false,XTIME:0,INTERVAL:0}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_chlorine", "Timer Chlor zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:9,VALUE:true,XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_filter", "Timer Filterwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:10,VALUE:true,XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_clean_filter", "Timer Filterreinigung zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:filter-variant", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:24,VALUE:true,XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_water_change", "Timer Wasserwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:water-sync", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:25,VALUE:true,XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_restart_esp", "WifiWhirl-Modul neustarten", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, "restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:6,VALUE:true,XTIME:0,INTERVAL:0}", nullptr, nullptr, nullptr, nullptr},
};

bool publishHaEntity(const char *component, const char *entity_id_suffix, DynamicJsonDocument &doc)
{
    String payload;
    payload.reserve(1300);
    String topic = String(HA_PREFIX) + F("/") + component + F("/") + mqttBaseTopic + entity_id_suffix + F("/config");
    if (serializeJson(doc, payload) == 0)
    {
        return false;
    }
    mqttClient->publish(topic.c_str(), payload.c_str(), true);
    mqttClient->loop();
    doc.clear();
    doc.garbageCollect();
    return true;
}

void setupHA()
{
    // HeapSelectIram ephemeral;
    // HeapSelectDram ephemeral;

#if defined(ESP8266)
    String mychipid = String((unsigned int)ESP.getChipId());
#elif defined(ESP32)
    String mychipid = String((unsigned int)ESP.getChipModel());
#endif
    int maxtemp, mintemp;
    maxtemp = 104;
    mintemp = 68;
    DynamicJsonDocument devicedoc(512);
    devicedoc[_dev]["configuration_url"] = "http://" + WiFi.localIP().toString();
    devicedoc[_dev]["connections"].add(serialized("[\"mac\",\"" + WiFi.macAddress() + "\"]"));
    devicedoc[_dev]["identifiers"] = mychipid;
    devicedoc[_dev]["manufacturer"] = F("WifiWhirl");
    devicedoc[_dev]["model"] = "Lay-Z-Spa";
    //devicedoc[_dev]["model"] = bwc->getModel();
    devicedoc[_dev][_name] = mqttBaseTopic + F(" Whirlpool");
    devicedoc[_dev]["sw_version"] = FW_VERSION;

    for (const auto &entity : entities)
    {
        if (strcmp(entity.entity_id_suffix, "_amb_temp_c_manual") == 0 && bwc->enableWeather)
        {
            continue;
        }
        if (strcmp(entity.entity_id_suffix, "_jets") == 0 && bwc->getModel() != "MALDIVES2021")
        {
            continue;
        }
        DynamicJsonDocument doc(1536);
        doc[_dev] = devicedoc[_dev];

        if (strcmp(entity.entity_id_suffix, "_amb_temp_c_manual") == 0) {
            doc[_name] = String(entity.name_suffix);
            doc[_uniq_id] = String(entity.uniq_id_prefix) + mqttBaseTopic + String("_amb_temp_c") + mychipid;
        } else {
            doc[_name] = String(entity.name_suffix);
            doc[_uniq_id] = String(entity.uniq_id_prefix) + mqttBaseTopic + String(entity.entity_id_suffix) + mychipid;
        }
        
        if (entity.stat_t_suffix)
            doc[_stat_t] = mqttBaseTopic + String(entity.stat_t_suffix);
        if (entity.val_tpl)
            doc[_val_tpl] = entity.val_tpl;
        if (entity.cmd_t_suffix)
            doc[_cmd_t] = mqttBaseTopic + String(entity.cmd_t_suffix);
        if (entity.cmd_tpl)
            doc[_cmd_tpl] = entity.cmd_tpl;
        if (entity.icon)
            doc["icon"] = entity.icon;
        if (entity.unit_of_meas)
            doc["unit_of_meas"] = entity.unit_of_meas;
        if (entity.dev_cla)
            doc["dev_cla"] = entity.dev_cla;
        if (entity.state_class)
            doc["state_class"] = entity.state_class;
        if (entity.expire_after)
            doc["expire_after"] = atoi(entity.expire_after);
        if (entity.pl_on)
            doc["pl_on"] = entity.pl_on;
        if (entity.pl_off)
            doc["pl_off"] = entity.pl_off;
        if (entity.state_on)
            doc["state_on"] = entity.state_on;
        if (entity.state_off)
            doc["state_off"] = entity.state_off;
        if (entity.payload_press)
            doc["payload_press"] = entity.payload_press;
        if (entity.min)
            doc["min"] = atoi(entity.min);
        if (entity.max)
            doc["max"] = atoi(entity.max);
        if (entity.mode)
            doc["mode"] = entity.mode;
        if (entity.assumed_state)
            doc["assumed_state"] = atoi(entity.assumed_state);

        doc[_avty_t] = mqttBaseTopic + F("/Status");
        doc[_pl_avail] = F("Alive");
        doc[_pl_not_avail] = F("Dead");

        if (strcmp(entity.entity_id_suffix, "_amb_temp_c_manual") == 0) {
            if (!publishHaEntity(entity.component, "_amb_temp_c", doc))
            {
                return;
            }
        } else {
            if (!publishHaEntity(entity.component, entity.entity_id_suffix, doc))
            {
                return;
            }
        }
    }

    /****************/
    /* CLIMATE      */
    /****************/

    Serial.println("climate");

    /* spa climate control */
    DynamicJsonDocument doc(1536);
    doc[_dev] = devicedoc[_dev];
    doc[_name] = F("Temperaturregelung");
    doc[_uniq_id] = F("climate.") + mqttBaseTopic + F("_climate") + mychipid;
    doc["max_temp"] = maxtemp;
    doc["min_temp"] = mintemp;
    doc["precision"] = 1.0;
    doc["temp_unit"] = "F";
    doc["modes"].add(serialized("\"fan_only\", \"off\", \"heat\""));
    doc["mode_cmd_t"] = mqttBaseTopic + F("/command_batch");
    doc["mode_cmd_tpl"] = F("[{CMD:3,VALUE:{%if value == \"heat\" %}1{% else %}0{% endif %},XTIME:0,INTERVAL:0},{CMD:4,VALUE:{%if value == \"fan_only\" %}1{% elif value == \"heat\" %}1{% else %}0{% endif %},XTIME:0,INTERVAL:0}]");
    doc["mode_stat_t"] = mqttBaseTopic + F("/message");
    doc["mode_stat_tpl"] = F("{% if value_json.RED == 1 %}heat{% elif value_json.GRN == 1 %}heat{% else %}off{% endif %}");
    doc["act_t"] = mqttBaseTopic + F("/message");
    doc["act_tpl"] = F("{% if value_json.RED == 1 %}heating{% elif value_json.GRN == 1 %}idle{% elif value_json.FLT == 1 %}fan{% else %}off{% endif %}");
    doc["temp_stat_t"] = mqttBaseTopic + F("/message");
    doc["temp_stat_tpl"] = F("{{ value_json.TGTF }}");
    doc["curr_temp_t"] = mqttBaseTopic + F("/message");
    doc["curr_temp_tpl"] = F("{{ value_json.TMPF }}");
    doc["temp_cmd_t"] = mqttBaseTopic + F("/command");
    doc["temp_cmd_tpl"] = F("{CMD:0,VALUE:{{ value|int }},XTIME:0,INTERVAL:0}");
    doc[_avty_t] = mqttBaseTopic + F("/Status");
    doc[_pl_avail] = F("Alive");
    doc[_pl_not_avail] = F("Dead");
    if (!publishHaEntity("climate", "_climate", doc))
    {
        return;
    }
}