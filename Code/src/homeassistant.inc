//--------------------------------------------------------------------------------------------------
/**
 * Build and publish Home-Assistant MQTT auto-discovery payloads for all WifiWhirl entities
 * keeping RAM usage to a minimum by storing constant metadata in flash (PROGMEM).
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Home Assistant MQTT discovery JSON keys stored in flash memory (PROGMEM)
 * to minimize RAM usage. These correspond to the standard HA discovery spec.
 */
//--------------------------------------------------------------------------------------------------
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
const char _object_id[] PROGMEM = "object_id";

// Climate entity specific keys
const char _max_temp[] PROGMEM = "max_temp";
const char _min_temp[] PROGMEM = "min_temp";
const char _precision[] PROGMEM = "precision";
const char _temp_unit[] PROGMEM = "temp_unit";
const char _modes[] PROGMEM = "modes";
const char _mode_cmd_t[] PROGMEM = "mode_cmd_t";
const char _mode_cmd_tpl[] PROGMEM = "mode_cmd_tpl";
const char _mode_stat_t[] PROGMEM = "mode_stat_t";
const char _mode_stat_tpl[] PROGMEM = "mode_stat_tpl";
const char _act_t[] PROGMEM = "act_t";
const char _act_tpl[] PROGMEM = "act_tpl";
const char _temp_stat_t[] PROGMEM = "temp_stat_t";
const char _temp_stat_tpl[] PROGMEM = "temp_stat_tpl";
const char _curr_temp_t[] PROGMEM = "curr_temp_t";
const char _curr_temp_tpl[] PROGMEM = "curr_temp_tpl";
const char _temp_cmd_t[] PROGMEM = "temp_cmd_t";
const char _temp_cmd_tpl[] PROGMEM = "temp_cmd_tpl";

// Climate templates stored in PROGMEM
const char _climate_mode_cmd_tpl[] PROGMEM = "[{CMD:3,VALUE:{% if value == \"heat\" %}1{% else %}0{% endif %}},{CMD:4,VALUE:{% if value == \"fan_only\" %}1{% elif value == \"heat\" %}1{% else %}0{% endif %}}]";
const char _climate_mode_stat_tpl[] PROGMEM = "{% if value_json.RED == 1 %}heat{% elif value_json.GRN == 1 %}heat{% else %}off{% endif %}";
const char _climate_act_tpl[] PROGMEM = "{% if value_json.RED == 1 %}heating{% elif value_json.GRN == 1 %}idle{% elif value_json.FLT == 1 %}fan{% else %}off{% endif %}";
const char _climate_temp_stat_tpl[] PROGMEM = "{{ value_json.TGTC }}";
const char _climate_curr_temp_tpl[] PROGMEM = "{{ value_json.TMPC }}";
const char _climate_temp_cmd_tpl[] PROGMEM = "{CMD:0,VALUE:{{ value|int }}}";
const char _climate_mode_off[] PROGMEM = "off";
const char _climate_mode_heat[] PROGMEM = "heat";
const char _climate_mode_fan[] PROGMEM = "fan_only";
const char _climate_temp_unit[] PROGMEM = "C";

//--------------------------------------------------------------------------------------------------
/**
 * Descriptor for one Home-Assistant entity.
 *
 * Each field holds the raw metadata that will later be copied from flash
 * (PROGMEM) into RAM while composing the JSON auto-discovery payload.
 * The meaning of the members closely mirrors the keys expected by Home
 * Assistant for MQTT discovery. Leaving a pointer as nullptr means that
 * the respective attribute is not applicable for this entity.
 */
//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
/**
 * Lookup table holding the static definition for *all* entities that shall
 * be announced to Home Assistant. Storing the table in PROGMEM keeps RAM
 * usage minimal on the constrained ESP micro-controller.
 */
//--------------------------------------------------------------------------------------------------
static const HaEntityConfig entities[] PROGMEM = {
    // Control entities
    {"number", "_brightness", "Helligkeit", "number.", "/message", "{{ value_json.BRT }}", "/command", "{CMD:12,VALUE:{{ value | int }}}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "0", "8", "slider", nullptr},

    // System information sensors
    {"sensor", "_reboot_time", "Letzter Neustart", "sensor.", "/reboot_time", "{{as_datetime(value)}}", nullptr, nullptr, nullptr, nullptr, "timestamp", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_reboot_reason", "Neustartgrund", "sensor.", "/reboot_reason", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Network status sensors
    {"sensor", "_ssid", "WLAN-SSID", "sensor.", "/other", "{{ value_json.SSID }}", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_rssi", "WLAN-RSSI", "sensor.", "/other", "{{ value_json.RSSI }}", nullptr, nullptr, "mdi:wifi", "dBm", "signal_strength", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_ip", "IP-Adresse", "sensor.", "/other", "{{ value_json.IP }}", nullptr, nullptr, "mdi:ip", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_connect_count", "MQTT-Verbindungszähler", "sensor.", "/MQTT_Connect_Count", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_error", "Fehler", "sensor.", "/message", "{{ value_json.ERR }}", nullptr, nullptr, "mdi:alert-circle-outline", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_time_to_ready", "Bereit in", "sensor.", "/times", "{% if value_json.T2R == -2 %}0{% elif value_json.T2R == -1 %}999{% else %}{{ value_json.T2R }}{% endif %}", nullptr, nullptr, "mdi:clock", "h", "duration", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Energy monitoring sensors
    {"sensor", "_energy", "Gesamtenergieverbrauch", "sensor.", "/times", "{{ value_json.KWH | round(2) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_today", "Energieverbrauch heute", "sensor.", "/times", "{{ value_json.KWHD | round(2) }}", nullptr, nullptr, "mdi:flash", "kWh", "energy", "total_increasing", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_power", "Aktuelle Leistungsaufnahme", "sensor.", "/times", "{{ value_json.WATT | int }}", nullptr, nullptr, "mdi:flash", "W", "power", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Maintenance tracking sensors
    {"sensor", "_chlorine_age", "Letzte Chlorung", "sensor.", "/times", "{{ as_datetime(value_json.CLTIME | int) }}", nullptr, nullptr, "hass:hand-coin-outline", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_age", "Letzter Filterwechsel", "sensor.", "/times", "{{ as_datetime(value_json.FTIME | int) }}", nullptr, nullptr, "hass:air-filter", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_filter_clean_age", "Letzte Filterreinigung", "sensor.", "/times", "{{ as_datetime(value_json.FCTIME | int) }}", nullptr, nullptr, "mdi:filter-check-outline", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_water_change_age", "Letzter Wasserwechsel", "sensor.", "/times", "{{ as_datetime(value_json.WCTIME | int) }}", nullptr, nullptr, "mdi:water-sync", nullptr, "timestamp", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Runtime tracking sensors
    {"sensor", "_uptime", "Betriebszeit", "sensor.", "/times", "{{ ( (value_json.UPTIME|int)/3600) }}", nullptr, nullptr, "mdi:clock-outline", "h", "duration", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_pumptime", "Pumpenlaufzeit", "sensor.", "/times", "{{ ( (value_json.PUMPTIME|int)/3600) }}", nullptr, nullptr, "mdi:clock-outline", "h", "duration", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_heatertime", "Heizungslaufzeit", "sensor.", "/times", "{{ ( (value_json.HEATINGTIME|int)/3600) }}", nullptr, nullptr, "mdi:clock-outline", "h", "duration", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_airtime", "AirJet Laufzeit", "sensor.", "/times", "{{ ( (value_json.AIRTIME|int)/3600) }}", nullptr, nullptr, "mdi:clock-outline", "h", "duration", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Temperature sensors
    // {"sensor", "_temperature_f", "Temperatur (F)", "sensor.", "/message", "{{ value_json.TMPF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_temp_c", "Aktuelle Temperatur", "sensor.", "/message", "{{ value_json.TMPC }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"sensor", "_virtual_temp_f", "Virtuelle Temperatur (F)", "sensor.", "/message", "{{ value_json.VTMF | round(2) }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"sensor", "_virtual_temp_c", "Virtuelle Temperatur", "sensor.", "/message", "{{ value_json.VTMC | round(2) }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"sensor", "_target_temp_f", "Solltemperatur (F)", "sensor.", "/message", "{{ value_json.TGTF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_target_temp_c", "Zieltemperatur", "sensor.", "/message", "{{ value_json.TGTC }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"sensor", "_amb_temp_c", "Umgebungstemperatur", "sensor.", "/message", "{{ value_json.AMBC }}", nullptr, nullptr, nullptr, "°C", "temperature", "measurement", "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"number", "_amb_temp_c_manual", "Umgebungstemperatur", "number.", "/message", "{{ value_json.AMBC }}", "/command", "{CMD:15,VALUE:{{ value | int }}}", "mdi:thermometer", "°C", "temperature", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "-20", "50", "slider", nullptr},

    // Status sensors
    // {"binary_sensor", "_lock", "Tastensperre", "binary_sensor.", "/message", "{% if value_json.LCK == 1 %}OFF{% else %}ON{% endif %}", nullptr, nullptr, nullptr, nullptr, "lock", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"binary_sensor", "_heater", "Heizung", "binary_sensor.", "/message", "{% if value_json.RED == 1 %}ON{% else %}OFF{% endif %}", nullptr, nullptr, nullptr, nullptr, "heat", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"binary_sensor", "_ready", "Bereit", "binary_sensor.", "/message", "{% if value_json.TMP > 30 %}{% if value_json.TMP >= value_json.TGT-1 %}ON{% else %}OFF{% endif %}{% else %}OFF{% endif %}", nullptr, nullptr, "mdi:hot-tub", nullptr, nullptr, nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"binary_sensor", "_connection", "Verbindung", "binary_sensor.", "/Status", nullptr, nullptr, nullptr, nullptr, nullptr, "connectivity", nullptr, nullptr, "Alive", "Dead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Control switches
    {"switch", "_heat_regulation", "Heizung", "switch.", "/message", "{% if value_json.RED == 1 %}1{% elif value_json.GRN == 1 %}1{% else %}0{% endif %}", "/command", nullptr, "mdi:radiator", nullptr, "switch", nullptr, "700", "{CMD:3,VALUE:true}", "{CMD:3,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_jets", "HydroJet", "switch.", "/message", "{{ value_json.HJT }}", "/command", nullptr, "mdi:hydro-power", nullptr, "switch", nullptr, "700", "{CMD:11,VALUE:true}", "{CMD:11,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_airbubbles", "AirJet", "switch.", "/message", "{{ value_json.AIR }}", "/command", nullptr, "mdi:chart-bubble", nullptr, "switch", nullptr, "700", "{CMD:2,VALUE:true}", "{CMD:2,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_pump", "Filterpumpe", "switch.", "/message", "{{ value_json.FLT }}", "/command", nullptr, "mdi:pump", nullptr, "switch", nullptr, "700", "{CMD:4,VALUE:true}", "{CMD:4,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_unit", "°F/°C", "switch.", "/message", "{{ value_json.UNT }}", "/command", nullptr, "mdi:circle-outline", nullptr, nullptr, nullptr, "700", "{CMD:1,VALUE:true}", "{CMD:1,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"switch", "_ctrl", "Steuerung übernehmen", "switch.", "/message", "{{ value_json.GOD }}", "/command", nullptr, "mdi:steering", nullptr, nullptr, nullptr, "700", "{CMD:17,VALUE:true}", "{CMD:17,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, "0"},
    {"switch", "_power", "Ein/Aus", "switch.", "/message", "{{ value_json.PWR }}", "/command", nullptr, "mdi:power", nullptr, "switch", nullptr, "700", "{CMD:22,VALUE:true}", "{CMD:22,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"switch", "_lock", "Tastensperre", "switch.", "/message", "{{ value_json.LCK }}", "/command", nullptr, "mdi:lock", nullptr, nullptr, nullptr, "700", "{CMD:23,VALUE:true}", "{CMD:23,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, nullptr},

    // Reset buttons
    {"button", "_reset_chlorine", "Timer Chlor zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:9,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_filter", "Timer Filterwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:10,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_clean_filter", "Timer Filterreinigung zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:filter-variant", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:24,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_reset_water_change", "Timer Wasserwechsel zurücksetzen", "button.", nullptr, nullptr, "/command", nullptr, "mdi:water-sync", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:25,VALUE:true}", nullptr, nullptr, nullptr, nullptr},
    {"button", "_restart_esp", "WifiWhirl-Modul neustarten", "button.", nullptr, nullptr, "/command", nullptr, "mdi:restart", nullptr, "restart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "{CMD:6,VALUE:true}", nullptr, nullptr, nullptr, nullptr},

    // Temperature control
    {"number", "_target_temp_c_set", "Zieltemperatur einstellen", "number.", "/message", "{{ value_json.TGTC }}", "/command", "{CMD:0,VALUE:{{ value | int }}}", "mdi:thermometer", "°C", "temperature", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "20", "40", "slider", nullptr},
};

constexpr size_t MQTT_TOPIC_LEN = 256; // MQTT topic must be <=255 per spec + null terminator

//--------------------------------------------------------------------------------------------------
/**
 * Helper that publishes a single discovery payload to the correct
 * Home-Assistant topic.
 *
 * The topic is built according to the MQTT discovery spec:
 *    <HA_PREFIX>/<component>/<node_id><entity_id_suffix>/config
 * The payload is already prepared by the caller in the provided JSON
 * document. This function just measures, publishes and flushes the MQTT
 * client so the network stack can process the data.
 *
 * @return true if the payload has been sent successfully.
 */
//--------------------------------------------------------------------------------------------------
bool publishHaEntity(const char *component, const char *entity_id_suffix, JsonDocument &doc)
{
    char topic[MQTT_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "%s/%s/%s%s/config", HA_PREFIX, component, mqttBaseTopic.c_str(), entity_id_suffix);

    size_t payloadSize = measureJson(doc);
    Serial.print(" (");
    Serial.print(payloadSize);
    Serial.print(" bytes)");
    
    if (payloadSize == 0)
    {
        Serial.print(" - Empty payload");
        return false;
    }
    
    // Safety check: ensure MQTT client is connected before attempting publish
    if (!mqttClient->connected())
    {
        Serial.print(" - MQTT not connected");
        return false;
    }

    if (!mqttClient->beginPublish(topic, payloadSize, true))
    {
        Serial.print(" - MQTT beginPublish failed");
        return false;
    }

    serializeJson(doc, *mqttClient);

    bool published = mqttClient->endPublish();
    if (!published)
    {
        Serial.print(" - MQTT endPublish failed");
    }
    
    // CRITICAL: Ensure data is sent to network stack
    mqttClient->loop();
    delay(10); // Brief delay to allow transmission to start
    mqttClient->loop();
    
    return published;
}

//--------------------------------------------------------------------------------------------------
/**
 * Build and publish the MQTT discovery payloads for all configured
 * WifiWhirl entities as well as the composite "climate" component.
 *
 * The function is intentionally large but mostly mechanical: it iterates
 * over the compile-time `entities` table, resolves all strings from
 * PROGMEM, fills the JSON template and finally calls `publishHaEntity()`.
 *
 * Because RAM is scarce, temporary buffers are reused aggressively and the
 * StaticJsonDocument size has been tuned to the required minimum.
 */
//--------------------------------------------------------------------------------------------------
void setupHA()
{
    Serial.print("HA: Starting Home Assistant entity setup... Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    
    // CRITICAL: Disable WiFi sleep mode BEFORE starting to prevent network stack memory issues
    #ifdef ESP8266
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    delay(100); // Allow WiFi stack to stabilize in no-sleep mode
    Serial.println("HA: WiFi sleep mode disabled for discovery");
    #endif
    
    // Generate unique chip ID for device identification
    char mychipid_str[16];
#if defined(ESP8266)
    snprintf(mychipid_str, sizeof(mychipid_str), "%u", ESP.getChipId());
#elif defined(ESP32)
    // Use MAC-based ID for ESP32 (no direct chip ID available)
    uint64_t chipid_mac = ESP.getEfuseMac();
    snprintf(mychipid_str, sizeof(mychipid_str), "%04X%08X", (uint16_t)(chipid_mac >> 32), (uint32_t)chipid_mac);
#endif

    // Device metadata - reusable across all entities
    // NOTE: Using static allocation to avoid heap fragmentation
    static StaticJsonDocument<500> devicedoc;
    devicedoc.clear(); // Ensure clean state on re-entry
    char config_url[64]; // Optimized for IP-based URLs
    snprintf(config_url, sizeof(config_url), "http://%s", WiFi.localIP().toString().c_str());
    devicedoc[FPSTR(_dev)]["configuration_url"] = config_url;

    char mac_conn[32];
    snprintf(mac_conn, sizeof(mac_conn), "[\"mac\",\"%s\"]", WiFi.macAddress().c_str());
    devicedoc[FPSTR(_dev)]["connections"].add(serialized(mac_conn));

    devicedoc[FPSTR(_dev)]["identifiers"] = mychipid_str;
    devicedoc[FPSTR(_dev)]["manufacturer"] = F("WifiWhirl");
    devicedoc[FPSTR(_dev)]["model"] = F("Lay-Z-Spa");
    // devicedoc[FPSTR(_dev)]["model"] = bwc->getModel();

    char device_name[128];
    snprintf(device_name, sizeof(device_name), "%s Whirlpool", mqttBaseTopic.c_str());
    devicedoc[FPSTR(_dev)][FPSTR(_name)] = device_name;

    devicedoc[FPSTR(_dev)]["sw_version"] = F(FW_VERSION);

    // Scratch buffer for reading PROGMEM strings (48 bytes)
    char ram_buffer[48];
    // Compile-time sanity: ensure ram_buffer is sufficient for longest field
    static_assert(sizeof(ram_buffer) > 32, "ram_buffer too small for field copies");

    // Topic construction buffer (220 bytes)
    char topic_buf[220];

    // Entity payload
    static StaticJsonDocument<1000> doc;

    const size_t totalEntities = sizeof(entities) / sizeof(entities[0]);
    size_t successCount = 0;
    size_t failureCount = 0;
    const size_t BATCH_SIZE = 5; // OPTIMIZED: Increased from 3 to 5 due to reduced MQTT buffer (768 bytes)
    
    Serial.print("HA: Processing ");
    Serial.print(totalEntities);
    Serial.print(" entities in batches of ");
    Serial.print(BATCH_SIZE);
    Serial.println("...");
    
    delay(100); // Safe watchdog feeding before starting entity processing

    for (size_t i = 0; i < totalEntities; i++)
    {
        // AGGRESSIVE memory cleanup every batch to prevent fragmentation and crashes
        if (i % BATCH_SIZE == 0 && i > 0) {
            Serial.print("HA: Batch ");
            Serial.print(i / BATCH_SIZE);
            Serial.print(" completed. Free heap: ");
            Serial.print(ESP.getFreeHeap());
            Serial.println(" bytes");
            
            // Force MQTT client to flush all pending data and clear internal buffers
            mqttClient->loop();
            delay(50);
            mqttClient->loop(); // Double loop to ensure complete flush
            delay(300); // Allow network stack to fully process and release buffers
            
            // Explicit memory check - abort if critically low
            // OPTIMIZED: Reduced threshold from 8000 to 6000 due to MQTT buffer reduction
            if (ESP.getFreeHeap() < 6000) {
                Serial.print("HA: CRITICAL - Low memory detected (");
                Serial.print(ESP.getFreeHeap());
                Serial.println(" bytes). Aborting discovery to prevent crash.");
                return;
            }
        }
        
        // Iterate over every entity definition and publish its Home-Assistant
        // auto-discovery payload
        doc.clear();
        PGM_P pgm_p;

        //--------------------------------------------------------------------------------------------------
        /**
         * Pull the *entity_id_suffix* pointer out of PROGMEM. Because the
         * ESP32/ESP8266 store string literals in flash, we cannot access them
         * directly with a normal `const char *` – we must first read the
         * 32-bit address from flash using `pgm_read_ptr()` and cast it to
         * `PGM_P` (pointer to PROGMEM). Only then can we copy the bytes into
         * the mutable `ram_buffer` with `strcpy_P()` for normal RAM access.
         */
        //--------------------------------------------------------------------------------------------------

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        strcpy_P(ram_buffer, pgm_p);

        //--------------------------------------------------------------------------------------------------
        /**
         * Skip entities that are either redundant or incompatible with the
         * current runtime configuration. Saves MQTT traffic and HA entities.
         * 1. `_amb_temp_c_manual` is omitted when the external weather module
         *    (bwc->enableWeather) already supplies the ambient temperature.
         * 2. `_jets` exists only for the Maldives-2021 model variant.
         */
        //--------------------------------------------------------------------------------------------------
        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0 && bwc->enableWeather)
        {
            continue;
        }
        if (strcmp(ram_buffer, "_jets") == 0 && bwc->getModel() != "MALDIVES2021")
        {
            continue;
        }

        doc[FPSTR(_dev)] = devicedoc[FPSTR(_dev)];

        char uniq_id_buf[160];
        //--------------------------------------------------------------------------------------------------
        /**
         * Compose the globally-unique entity ID that Home Assistant will use
         * for this object. Some entities (like `_amb_temp_c_manual`) must be
         * mapped to an alternative canonical name so that the manual and
         * automatic variants don't collide inside HA.
         */
        //--------------------------------------------------------------------------------------------------

        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0)
        {
            pgm_p = (PGM_P)pgm_read_ptr(&entities[i].name_suffix);
            doc[FPSTR(_name)] = (const __FlashStringHelper *)pgm_p;

            PGM_P uniq_id_prefix_p = (PGM_P)pgm_read_ptr(&entities[i].uniq_id_prefix);
            char uniq_id_prefix_ram[32];
            strcpy_P(uniq_id_prefix_ram, uniq_id_prefix_p);
            snprintf(uniq_id_buf, sizeof(uniq_id_buf), "%s%s_amb_temp_c", uniq_id_prefix_ram, mqttBaseTopic.c_str());
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

            snprintf(uniq_id_buf, sizeof(uniq_id_buf), "%s%s%s", uniq_id_prefix_ram, mqttBaseTopic.c_str(), entity_id_suffix_ram);
        }
        doc[FPSTR(_uniq_id)] = uniq_id_buf;

        PGM_P suffix_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        char suffix_ram[64];
        strcpy_P(suffix_ram, suffix_p);
        // Strip leading underscore from entity suffix for object_id generation
        const char *suffix_ptr = (suffix_ram[0] == '_') ? suffix_ram + 1 : suffix_ram;
        char object_id_buf[160];
        snprintf(object_id_buf, sizeof(object_id_buf), "%s_%s", mqttBaseTopic.c_str(), suffix_ptr);
        doc[FPSTR(_object_id)] = object_id_buf;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].stat_t_suffix);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            snprintf(topic_buf, sizeof(topic_buf), "%s%s", mqttBaseTopic.c_str(), ram_buffer);
            doc[FPSTR(_stat_t)] = topic_buf;
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].val_tpl);
        if (pgm_p)
            doc[FPSTR(_val_tpl)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].cmd_t_suffix);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            snprintf(topic_buf, sizeof(topic_buf), "%s%s", mqttBaseTopic.c_str(), ram_buffer);
            doc[FPSTR(_cmd_t)] = topic_buf;
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].cmd_tpl);
        if (pgm_p)
            doc[FPSTR(_cmd_tpl)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].icon);
        if (pgm_p)
            doc[FPSTR(_icon)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].unit_of_meas);
        if (pgm_p)
            doc[FPSTR(_unit_of_meas)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].dev_cla);
        if (pgm_p)
            doc[FPSTR(_dev_cla)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_class);
        if (pgm_p)
            doc[FPSTR(_state_class)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].expire_after);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_expire_after)] = atoi(ram_buffer);
        }

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].pl_on);
        if (pgm_p)
            doc[FPSTR(_pl_on)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].pl_off);
        if (pgm_p)
            doc[FPSTR(_pl_off)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_on);
        if (pgm_p)
            doc[FPSTR(_state_on)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].state_off);
        if (pgm_p)
            doc[FPSTR(_state_off)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].payload_press);
        if (pgm_p)
            doc[FPSTR(_payload_press)] = (const __FlashStringHelper *)pgm_p;

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
        if (pgm_p)
            doc[FPSTR(_mode_key)] = (const __FlashStringHelper *)pgm_p;

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].assumed_state);
        if (pgm_p)
        {
            strcpy_P(ram_buffer, pgm_p);
            doc[FPSTR(_assumed_state)] = atoi(ram_buffer);
        }

        // Set up availability topic for connection status monitoring
        snprintf(topic_buf, sizeof(topic_buf), "%s/Status", mqttBaseTopic.c_str());
        doc[FPSTR(_avty_t)] = topic_buf;
        doc[FPSTR(_pl_avail)] = F("Alive");    // Payload when device is online
        doc[FPSTR(_pl_not_avail)] = F("Dead"); // Payload when device is offline

        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        strcpy_P(ram_buffer, pgm_p);

        PGM_P component_p = (PGM_P)pgm_read_ptr(&entities[i].component);
        char component_ram[32];
        strcpy_P(component_ram, component_p);

        // Publish the entity with timing info for debugging
        unsigned long startTime = millis();
        bool publishResult = false;
        
        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0)
        {
            Serial.print("HA: Publishing entity ");
            Serial.print(component_ram);
            Serial.print(" -> _amb_temp_c");
            publishResult = publishHaEntity(component_ram, "_amb_temp_c", doc);
        }
        else
        {
            Serial.print("HA: Publishing entity ");
            Serial.print(component_ram);
            Serial.print(" -> ");
            Serial.print(ram_buffer);
            publishResult = publishHaEntity(component_ram, ram_buffer, doc);
        }
        
        unsigned long publishTime = millis() - startTime;
        
        // Log the result
        if (publishResult)
        {
            Serial.println(" ✓ SUCCESS");
            successCount++;
            
            // CRITICAL: Force immediate MQTT buffer flush after successful publish
            mqttClient->loop();
            delay(50);
            mqttClient->loop(); // Double loop ensures buffer is fully cleared
        }
        else
        {
            Serial.println(" ✗ FAILED");
            failureCount++;
            
            // On failure, still flush to clear any partial data
            mqttClient->loop();
            delay(100);
            continue;
        }
        
        // If publishing took longer than 500ms, add extra delay for network recovery
        if (publishTime > 500) {
            delay(150); // Give network stack time to recover from slow publish
        }
        
        // Adaptive delay based on network activity to prevent watchdog timeout
        // ESP8266 WiFi stack can block the main loop unpredictably
        delay(150); // Base delay for network processing
        
        // Additional delay and flush every 10 entities to account for WiFi interrupts
        if ((i + 1) % 10 == 0) {
            mqttClient->loop(); // Extra flush every 10 entities
            delay(200); // Extra time for WiFi stack processing
        }
    }

    Serial.print("HA: Entity summary - Success: ");
    Serial.print(successCount);
    Serial.print(", Failed: ");
    Serial.print(failureCount);
    Serial.print(", Total: ");
    Serial.print(totalEntities);
    Serial.print(". Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    
    // CRITICAL: Final buffer flush before climate entity
    mqttClient->loop();
    delay(200);
    mqttClient->loop();
    delay(200);

    //--------------------------------------------------------------------------------------------------
    /**
     * Climate entity - large JSON payload requires extra memory safety
     */
    //--------------------------------------------------------------------------------------------------
    {
        // Check if we have sufficient memory for climate entity (needs ~1700 bytes + overhead)
        // OPTIMIZED: Reduced threshold from 10000 to 8000 due to MQTT buffer reduction
        if (ESP.getFreeHeap() < 8000) {
            Serial.print("HA: WARNING - Insufficient memory for climate entity (");
            Serial.print(ESP.getFreeHeap());
            Serial.println(" bytes). Skipping to prevent crash.");
            return;
        }
        
        // Use safe delay instead of yield during early boot
        delay(100); // Extended delay for memory stabilization
        
        // Climate entity - increased to account for JSON internal representation overhead (1700 bytes)
        static StaticJsonDocument<1700> climate_doc;
        climate_doc.clear();
        
        // Basic entity setup
        climate_doc[FPSTR(_dev)] = devicedoc[FPSTR(_dev)];

        // Build climate entity name
        snprintf(ram_buffer, sizeof(ram_buffer), "Temperatursteuerung");
        climate_doc[FPSTR(_name)] = ram_buffer;

        // Build unique ID - consistent with other entities
        snprintf(topic_buf, sizeof(topic_buf), "climate.%s_climate", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_uniq_id)] = topic_buf;

        // Build object_id
        snprintf(ram_buffer, sizeof(ram_buffer), "%s_climate", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_object_id)] = ram_buffer;

        // Temperature settings
        climate_doc[FPSTR(_max_temp)] = 40;
        climate_doc[FPSTR(_min_temp)] = 20;
        climate_doc[FPSTR(_precision)] = 1.0;
        climate_doc[FPSTR(_temp_unit)] = F("C");

        // Climate modes
        climate_doc[FPSTR(_modes)].add(F("off"));
        climate_doc[FPSTR(_modes)].add(F("heat"));
        climate_doc[FPSTR(_modes)].add(F("fan_only"));

        // Climate entity buffers
        char mode_cmd_topic[100];
        char message_topic[100];
        char temp_cmd_topic[100];
        char status_topic[100];
        
        // Mode command topic
        snprintf(mode_cmd_topic, sizeof(mode_cmd_topic), "%s/command_batch", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_mode_cmd_t)] = mode_cmd_topic;
        
        // Mode command template - simplified to avoid Flash string issues
        climate_doc[FPSTR(_mode_cmd_tpl)] = F("[{\"CMD\":3,\"VALUE\":{% if value == \"heat\" %}1{% else %}0{% endif %}},{\"CMD\":4,\"VALUE\":{% if value == \"fan_only\" %}1{% elif value == \"heat\" %}1{% else %}0{% endif %}}]");

        // Status topics - use dedicated buffer for message topic
        snprintf(message_topic, sizeof(message_topic), "%s/message", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_mode_stat_t)] = message_topic;
        climate_doc[FPSTR(_act_t)] = message_topic;
        climate_doc[FPSTR(_temp_stat_t)] = message_topic;
        climate_doc[FPSTR(_curr_temp_t)] = message_topic;

        // Templates - add all at once without interruption
        climate_doc[FPSTR(_mode_stat_tpl)] = F("{% if value_json.RED == 1 %}heat{% elif value_json.GRN == 1 %}heat{% else %}off{% endif %}");
        climate_doc[FPSTR(_act_tpl)] = F("{% if value_json.RED == 1 %}heating{% elif value_json.GRN == 1 %}idle{% elif value_json.FLT == 1 %}fan{% else %}off{% endif %}");
        climate_doc[FPSTR(_temp_stat_tpl)] = F("{{ value_json.TGTC }}");
        climate_doc[FPSTR(_curr_temp_tpl)] = F("{{ value_json.TMPC }}");

        // Temperature command - use dedicated buffer
        snprintf(temp_cmd_topic, sizeof(temp_cmd_topic), "%s/command", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_temp_cmd_t)] = temp_cmd_topic;
        climate_doc[FPSTR(_temp_cmd_tpl)] = F("{CMD:0,VALUE:{{ value|int }}}");

        // Availability setup - use dedicated buffer
        snprintf(status_topic, sizeof(status_topic), "%s/Status", mqttBaseTopic.c_str());
        climate_doc[FPSTR(_avty_t)] = status_topic;
        climate_doc[FPSTR(_pl_avail)] = F("Alive");
        climate_doc[FPSTR(_pl_not_avail)] = F("Dead");

        // Publish climate entity
        Serial.print("HA: Publishing climate entity -> _climate");
        
        // Add delay before climate entity to ensure network is ready
        delay(250);
        
        bool climateResult = publishHaEntity("climate", "_climate", climate_doc);
        if (climateResult)
        {
            Serial.println(" ✓ SUCCESS");
            
            // CRITICAL: Aggressive flush after large climate payload
            mqttClient->loop();
            delay(100);
            mqttClient->loop();
            delay(100);
        }
        else
        {
            Serial.println(" ✗ FAILED");
            // On failure, still try to flush any partial data
            mqttClient->loop();
            delay(100);
        }
        
        // Extra delay after climate entity to ensure complete transmission
        delay(200);
    }
    
    Serial.print("HA: Home Assistant entity setup completed! Final free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    
    // CRITICAL: Final aggressive buffer flush before re-enabling sleep
    mqttClient->loop();
    delay(200);
    mqttClient->loop();
    delay(200);
    mqttClient->loop(); // Triple loop for absolute certainty
    
    // Final stabilization delay before re-enabling WiFi sleep mode
    delay(300); // Allow all MQTT operations to complete
    
    // Re-enable WiFi sleep mode for normal operation
    #ifdef ESP8266
    WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
    Serial.println("HA: WiFi sleep mode re-enabled");
    #endif
    
    Serial.println("HA: Setup complete and memory flushed");
}