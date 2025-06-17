/*
    Note, following does not work!
    doc[F("key")] = F("Value")
    String s; doc[s] = ...
    Throws all kind of errors. String throws stack smashing.
*/

const char _dev[] PROGMEM = "dev";
const char _name[] PROGMEM = "name";
const char _uniq_id[] PROGMEM = "uniq_id";
const char _stat_t[] PROGMEM = "stat_t";
const char _avty_t[] PROGMEM = "avty_t";
const char _pl_avail[] PROGMEM = "pl_avail";
const char _pl_not_avail[] PROGMEM = "pl_not_avail";
const char _val_tpl[] PROGMEM = "val_tpl";
const char _cmd_t[] PROGMEM = "cmd_t";
const char _cmd_tpl[] PROGMEM = "cmd_tpl";
const char _icon[] PROGMEM = "icon";
const char _unit_of_meas[] PROGMEM = "unit_of_meas";
const char _dev_cla[] PROGMEM = "dev_cla";
const char _state_class[] PROGMEM = "state_class";
const char _expire_after[] PROGMEM = "expire_after";
const char _pl_on[] PROGMEM = "pl_on";
const char _pl_off[] PROGMEM = "pl_off";
const char _state_on[] PROGMEM = "state_on";
const char _state_off[] PROGMEM = "state_off";
const char _payload_press[] PROGMEM = "payload_press";
const char _min_key[] PROGMEM = "min";
const char _max_key[] PROGMEM = "max";
const char _mode_key[] PROGMEM = "mode";
const char _assumed_state[] PROGMEM = "assumed_state";

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

static const HaEntityConfig entities[] PROGMEM = {
    {"number", "_brightness", "Helligkeit", "number.", "/message", "{{ value_json.BRT }}", "/command", "{CMD:12,VALUE:{{ value | int }}}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "0", "8", "slider", nullptr},
    {"sensor", "_reboot_time", "Letzter Neustart", "sensor.", "/reboot_time", "{{as_datetime(value)}}", nullptr, nullptr, nullptr, nullptr, "timestamp", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_reboot_reason", "Neustartgrund", "sensor.", "/reboot_reason", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_ssid", "WLAN-SSID", "sensor.", "/other", "{{ value_json.SSID }}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_rssi", "WLAN-RSSI", "sensor.", "/other", "{{ value_json.RSSI }}", nullptr, nullptr, "mdi:wifi", "dBm", "signal_strength", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_ip", "IP-Adresse", "sensor.", "/other", "{{ value_json.IP }}", nullptr, nullptr, "mdi:ip", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_connect_count", "MQTT-Verbindungszähler", "sensor.", "/MQTT_Connect_Count", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_error", "Fehler", "sensor.", "/message", "{{ value_json.ERR }}", nullptr, nullptr, "mdi:alert-circle-outline", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_time_to_ready", "Bereit in", "sensor.", "/times", "{{ value_json.T2R | round(2) }}", nullptr, nullptr, "mdi:clock", "Stunden", nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"sensor", "_rs", "Bereitschaftsstatus", "sensor.", "/times", "{{ value_json.RS }}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_energy", "Gesamtenergieverbrauch", "sensor.", "/times", "{{ value_json.KWH | round(3) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_today", "Energieverbrauch heute", "sensor.", "/times", "{{ value_json.KWHD | round(3) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_power", "Aktuelle Leistungsaufnahme", "sensor.", "/times", "{{ value_json.WATT | int }}", nullptr, nullptr, "mdi:flash", "W", "power", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_chlorine_age", "Letzte Chlorung", "sensor.", "/times", "{{ as_datetime(value_json.CLTIME | int) }}", nullptr, nullptr, "hass:hand-coin-outline", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_age", "Letzter Filterwechsel", "sensor.", "/times", "{{ as_datetime(value_json.FTIME | int) }}", nullptr, nullptr, "hass:air-filter", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_clean_age", "Letzte Filterreinigung", "sensor.", "/times", "{{ as_datetime(value_json.FCTIME | int) }}", nullptr, nullptr, "mdi:filter-check-outline", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_water_change_age", "Letzter Wasserwechsel", "sensor.", "/times", "{{ as_datetime(value_json.WCTIME | int) }}", nullptr, nullptr, "mdi:water-sync", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
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
    {"number", "_amb_temp_c_manual", "Umgebungstemperatur", "number.", "/message", "{{ value_json.AMBC }}", "/command", "{CMD:15,VALUE:{{ value | int }}}", "mdi:thermometer", "°C", "temperature", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "-20", "50", "slider", nullptr},
    {"binary_sensor", "_lock", "Tastensperre", "binary_sensor.", "/message", "{% if value_json.LCK == 1 %}OFF{% else %}ON{% endif %}", nullptr, nullptr, nullptr, nullptr, "lock", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"binary_sensor", "_heater", "Heizung", "binary_sensor.", "/message", "{% if value_json.RED == 1 %}ON{% else %}OFF{% endif %}", nullptr, nullptr, nullptr, nullptr, "heat", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"binary_sensor", "_ready", "Bereit", "binary_sensor.", "/message", "{% if value_json.TMP > 30 %}{% if value_json.TMP >= value_json.TGT-1 %}ON{% else %}OFF{% endif %}{% else %}OFF{% endif %}", nullptr, nullptr, "mdi:hot-tub", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"binary_sensor", "_connection", "Verbindung", "binary_sensor.", "/Status", nullptr, nullptr, nullptr, nullptr, nullptr, "connectivity", nullptr, nullptr, "Alive", "Dead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_heat_regulation", "Heizung", "switch.", "/message", "{% if value_json.RED == 1 %}1{% elif value_json.GRN == 1 %}1{% else %}0{% endif %}", "/command", nullptr, "mdi:radiator", nullptr, nullptr, nullptr, "700", "{CMD:3,VALUE:true}", "{CMD:3,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_jets", "HydroJet", "switch.", "/message", "{{ value_json.HJT }}", "/command", nullptr, "mdi:hydro-power", nullptr, nullptr, nullptr, "700", "{CMD:11,VALUE:true}", "{CMD:11,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_airbubbles", "AirJet", "switch.", "/message", "{{ value_json.AIR }}", "/command", nullptr, "mdi:chart-bubble", nullptr, nullptr, nullptr, "700", "{CMD:2,VALUE:true}", "{CMD:2,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_pump", "Filterpumpe", "switch.", "/message", "{{ value_json.FLT }}", "/command", nullptr, "mdi:pump", nullptr, nullptr, nullptr, "700", "{CMD:4,VALUE:true}", "{CMD:4,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_unit", "°F/°C", "switch.", "/message", "{{ value_json.UNT }}", "/command", nullptr, "mdi:circle-outline", nullptr, nullptr, nullptr, "700", "{CMD:1,VALUE:true}", "{CMD:1,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    //{"switch", "_ctrl", "Steuerung übernehmen", "switch.", "/message", "{{ value_json.GOD }}", "/command", nullptr, "mdi:steering", nullptr, nullptr, nullptr, "700", "{CMD:17,VALUE:true}", "{CMD:17,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, "0"},
    {"switch", "_power", "Ein/Aus", "switch.", "/message", "{{ value_json.PWR }}", "/command", nullptr, "mdi:power", nullptr, nullptr, nullptr, "700", "{CMD:22,VALUE:true}", "{CMD:22,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_lock", "Tastensperre", "switch.", "/message", "{{ value_json.LCK }}", "/command", nullptr, "mdi:lock", nullptr, nullptr, nullptr, "700", "{CMD:23,VALUE:true}", "{CMD:23,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_chlorine", "Timer Chlor zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:9,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_filter", "Timer Filterwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:10,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_clean_filter", "Timer Filterreinigung zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:filter-variant", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:24,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_water_change", "Timer Wasserwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:water-sync", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:25,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_restart_esp", "WifiWhirl-Modul neustarten", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, "restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:6,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"number", "_target_temp_c_set", "Zieltemperatur einstellen", "number.", "/message", "{{ value_json.TGTC }}", "/command", "{CMD:0,VALUE:{{ value | int }}}", "mdi:thermometer", "°C", "temperature", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "20", "40", "slider", nullptr},
};

constexpr size_t MQTT_TOPIC_LEN = 256; // MQTT topic must be <=255 per spec

bool publishHaEntity(const char *component, const char *entity_id_suffix, JsonDocument &doc)
{
    char topic[MQTT_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "%s/%s/%s%s/config", HA_PREFIX, component, mqttBaseTopic.c_str(), entity_id_suffix);

    size_t payloadSize = measureJson(doc);
    if (payloadSize == 0) return false;

    if (!mqttClient->beginPublish(topic, payloadSize, true)) {
        return false;
    }
    
    serializeJson(doc, *mqttClient);

    bool published = mqttClient->endPublish();
    mqttClient->loop(); // Allow network stack to process
    return published;
}

void setupHA()
{
    // HeapSelectIram ephemeral;
    // HeapSelectDram ephemeral;

    char mychipid_str[16];
#if defined(ESP8266)
    snprintf(mychipid_str, sizeof(mychipid_str), "%u", ESP.getChipId());
#elif defined(ESP32)
    uint64_t chipid_mac = ESP.getEfuseMac();
    snprintf(mychipid_str, sizeof(mychipid_str), "%04X%08X", (uint16_t)(chipid_mac >> 32), (uint32_t)chipid_mac);
#endif

    int maxtemp, mintemp;
    maxtemp = 104;
    mintemp = 68;
    static StaticJsonDocument<512> devicedoc;
    char config_url[128];
    snprintf(config_url, sizeof(config_url), "http://%s", WiFi.localIP().toString().c_str());
    devicedoc[FPSTR(_dev)]["configuration_url"] = config_url;

    char mac_conn[32];
    snprintf(mac_conn, sizeof(mac_conn), "[\"mac\",\"%s\"]", WiFi.macAddress().c_str());
    devicedoc[FPSTR(_dev)]["connections"].add(serialized(mac_conn));

    devicedoc[FPSTR(_dev)]["identifiers"] = mychipid_str;
    devicedoc[FPSTR(_dev)]["manufacturer"] = F("WifiWhirl");
    devicedoc[FPSTR(_dev)]["model"] = F("Lay-Z-Spa");
    //devicedoc[FPSTR(_dev)]["model"] = bwc->getModel();

    char device_name[128];
    snprintf(device_name, sizeof(device_name), "%s Whirlpool", mqttBaseTopic.c_str());
    devicedoc[FPSTR(_dev)][FPSTR(_name)] = device_name;

    devicedoc[FPSTR(_dev)]["sw_version"] = F(FW_VERSION);

    // Scratch buffer for reading PROGMEM strings.
    char ram_buffer[64];
    // Compile-time sanity: ensure ram_buffer is sufficient for longest field
    static_assert(sizeof(ram_buffer) > 32, "ram_buffer too small for field copies");

    // The JSON payload per entity rarely exceeds 600 bytes. 1 KB is a safe cushion.
    static StaticJsonDocument<1024> doc;

    for (size_t i = 0; i < (sizeof(entities) / sizeof(entities[0])); i++)
    {
        doc.clear();
        PGM_P pgm_p;
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        strcpy_P(ram_buffer, pgm_p);

        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0 && bwc->enableWeather)
        {
            continue;
        }
        if (strcmp(ram_buffer, "_jets") == 0 && bwc->getModel() != "MALDIVES2021")
        {
            continue;
        }
        
        doc[FPSTR(_dev)] = devicedoc[FPSTR(_dev)];

        char uniq_id_buf[256];
        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0)
        {
            pgm_p = (PGM_P)pgm_read_ptr(&entities[i].name_suffix);
            doc[FPSTR(_name)] = (const __FlashStringHelper *)pgm_p;

            PGM_P uniq_id_prefix_p = (PGM_P)pgm_read_ptr(&entities[i].uniq_id_prefix);
            char uniq_id_prefix_ram[32];
            strcpy_P(uniq_id_prefix_ram, uniq_id_prefix_p);
            snprintf(uniq_id_buf, sizeof(uniq_id_buf), "%s%s_amb_temp_c%s", uniq_id_prefix_ram, mqttBaseTopic.c_str(), mychipid_str);
        }
        else
        {
            pgm_p = (PGM_P)pgm_read_ptr(&entities[i].name_suffix);
            doc[FPSTR(_name)] = (const __FlashStringHelper *)pgm_p;

            PGM_P uniq_id_prefix_p = (PGM_P)pgm_read_ptr(&entities[i].uniq_id_prefix);
            char uniq_id_prefix_ram[32];
            strcpy_P(uniq_id_prefix_ram, uniq_id_prefix_p);
            
            PGM_P entity_id_suffix_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
            char entity_id_suffix_ram[64];
            strcpy_P(entity_id_suffix_ram, entity_id_suffix_p);

            snprintf(uniq_id_buf, sizeof(uniq_id_buf), "%s%s%s%s", uniq_id_prefix_ram, mqttBaseTopic.c_str(), entity_id_suffix_ram, mychipid_str);
        }
        doc[FPSTR(_uniq_id)] = uniq_id_buf;

        char topic_buf[256];
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].stat_t_suffix);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            snprintf(topic_buf, sizeof(topic_buf), "%s%s", mqttBaseTopic.c_str(), ram_buffer);
            doc[FPSTR(_stat_t)] = topic_buf;
        }
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].val_tpl);
        if(pgm_p) doc[FPSTR(_val_tpl)] = (const __FlashStringHelper *)pgm_p;
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].cmd_t_suffix);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            snprintf(topic_buf, sizeof(topic_buf), "%s%s", mqttBaseTopic.c_str(), ram_buffer);
            doc[FPSTR(_cmd_t)] = topic_buf;
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].cmd_tpl);
        if(pgm_p) doc[FPSTR(_cmd_tpl)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].icon);
        if(pgm_p) doc[FPSTR(_icon)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].unit_of_meas);
        if(pgm_p) doc[FPSTR(_unit_of_meas)] = (const __FlashStringHelper *)pgm_p;
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].dev_cla);
        if(pgm_p) doc[FPSTR(_dev_cla)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_class);
        if(pgm_p) doc[FPSTR(_state_class)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].expire_after);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_expire_after)] = atoi(ram_buffer);
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].pl_on);
        if(pgm_p) doc[FPSTR(_pl_on)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].pl_off);
        if(pgm_p) doc[FPSTR(_pl_off)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_on);
        if(pgm_p) doc[FPSTR(_state_on)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_off);
        if(pgm_p) doc[FPSTR(_state_off)] = (const __FlashStringHelper *)pgm_p;
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].payload_press);
        if(pgm_p) doc[FPSTR(_payload_press)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].min);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_min_key)] = atoi(ram_buffer);
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].max);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_max_key)] = atoi(ram_buffer);
        }
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].mode);
        if(pgm_p) doc[FPSTR(_mode_key)] = (const __FlashStringHelper *)pgm_p;
        
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].assumed_state);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_assumed_state)] = atoi(ram_buffer);
        }

        snprintf(topic_buf, sizeof(topic_buf), "%s/Status", mqttBaseTopic.c_str());
        doc[FPSTR(_avty_t)] = topic_buf;
        doc[FPSTR(_pl_avail)] = F("Alive");
        doc[FPSTR(_pl_not_avail)] = F("Dead");

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        strcpy_P(ram_buffer, pgm_p);
        
        PGM_P component_p = (PGM_P)pgm_read_ptr(&entities[i].component);
        char component_ram[32];
        strcpy_P(component_ram, component_p);

        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0)
        {
            if (!publishHaEntity(component_ram, "_amb_temp_c", doc))
            {
                return;
            }
        }
        else
        {
            if (!publishHaEntity(component_ram, ram_buffer, doc))
            {
                return;
            }
        }
    }

    /****************/
    /* CLIMATE      */
    /****************/

    Serial.println(F("climate"));

    /* spa climate control */
    doc.clear();
    doc[FPSTR(_dev)] = devicedoc[FPSTR(_dev)];
    doc[FPSTR(_name)] = F("Temperaturregelung");

    char uniq_id_buf[256];
    snprintf(uniq_id_buf, sizeof(uniq_id_buf), "climate.%s_climate%s", mqttBaseTopic.c_str(), mychipid_str);
    doc[FPSTR(_uniq_id)] = uniq_id_buf;

    doc["max_temp"] = maxtemp;
    doc["min_temp"] = mintemp;
    doc["precision"] = 1.0;
    doc["temp_unit"] = F("F");
    doc["modes"].add(serialized(F("\"fan_only\", \"off\", \"heat\"")));

    char topic_buf[256];
    snprintf(topic_buf, sizeof(topic_buf), "%s/command_batch", mqttBaseTopic.c_str());
    doc["mode_cmd_t"] = topic_buf;
    doc["mode_cmd_tpl"] = F("[{CMD:3,VALUE:{%if value == \\\"heat\\\" %}1{% else %}0{% endif %}},{CMD:4,VALUE:{%if value == \\\"fan_only\\\" %}1{% elif value == \\\"heat\\\" %}1{% else %}0{% endif %}}]");

    snprintf(topic_buf, sizeof(topic_buf), "%s/message", mqttBaseTopic.c_str());
    doc["mode_stat_t"] = topic_buf;
    doc["mode_stat_tpl"] = F("{% if value_json.RED == 1 %}heat{% elif value_json.GRN == 1 %}heat{% else %}off{% endif %}");

    doc["act_t"] = topic_buf;
    doc["act_tpl"] = F("{% if value_json.RED == 1 %}heating{% elif value_json.GRN == 1 %}idle{% elif value_json.FLT == 1 %}fan{% else %}off{% endif %}");

    doc["temp_stat_t"] = topic_buf;
    doc["temp_stat_tpl"] = F("{{ value_json.TGTF }}");

    doc["curr_temp_t"] = topic_buf;
    doc["curr_temp_tpl"] = F("{{ value_json.TMPF }}");

    snprintf(topic_buf, sizeof(topic_buf), "%s/command", mqttBaseTopic.c_str());
    doc["temp_cmd_t"] = topic_buf;
    doc["temp_cmd_tpl"] = F("{CMD:0,VALUE:{{ value|int }}}");

    snprintf(topic_buf, sizeof(topic_buf), "%s/Status", mqttBaseTopic.c_str());
    doc[FPSTR(_avty_t)] = topic_buf;
    doc[FPSTR(_pl_avail)] = F("Alive");
    doc[FPSTR(_pl_not_avail)] = F("Dead");
    if (!publishHaEntity("climate", "_climate", doc))
    {
        return;
    }
}