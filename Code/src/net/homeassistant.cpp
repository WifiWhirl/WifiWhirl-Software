#include "net/net.h"
#include "WifiWhirl_Version.h"

#ifndef ESP8266
// ESP32's heap-fragmentation metric is getMaxAllocHeap(); alias it so the
// shared HA discovery diagnostics below read the same on both cores.
#define getMaxFreeBlockSize getMaxAllocHeap
#endif

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
const char _step_key[] PROGMEM = "step";

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
// Smart Schedule entity strings in flash: the table only holds pointers, and
// plain literals cost ~1 KB of RAM for these 8 rows
static const char SS0[] PROGMEM = "number";
static const char SS1[] PROGMEM = "_ss_temp";
static const char SS2[] PROGMEM = "Smart Schedule Zieltemperatur";
static const char SS3[] PROGMEM = "number.";
static const char SS4[] PROGMEM = "/smartschedule";
static const char SS5[] PROGMEM = "{{ value_json.TEMP }}";
static const char SS6[] PROGMEM = "/smartschedule/set";
static const char SS7[] PROGMEM = "{\"TEMP\":{{ value | int }}}";
static const char SS8[] PROGMEM = "mdi:thermometer";
static const char SS9[] PROGMEM = "°C";
static const char SS10[] PROGMEM = "20";
static const char SS11[] PROGMEM = "40";
static const char SS12[] PROGMEM = "box";
static const char SS13[] PROGMEM = "text";
static const char SS14[] PROGMEM = "_ss_time";
static const char SS15[] PROGMEM = "Smart Schedule Zielzeit (JJJJ-MM-TT HH:MM)";
static const char SS16[] PROGMEM = "text.";
static const char SS17[] PROGMEM = "{{ value_json.TIME }}";
static const char SS18[] PROGMEM = "{\"TIME\":\"{{ value }}\"}";
static const char SS19[] PROGMEM = "mdi:calendar-clock";
// Date/time picker for the target time. MQTT "datetime" exists only in newer
// Home Assistant (2026.x); older versions skip it and keep the text field above,
// so both are announced (no breaking change).
static const char SS56[] PROGMEM = "datetime";
static const char SS57[] PROGMEM = "_ss_datetime";
static const char SS58[] PROGMEM = "Smart Schedule Zielzeit";
static const char SS59[] PROGMEM = "datetime.";
static const char SS60[] PROGMEM = "{{ value_json.TS if value_json.TS else 'None' }}";
static const char SS61[] PROGMEM = "{\"EPOCH\":{{ as_timestamp(value) | int }}}";
static const char SS20[] PROGMEM = "select";
static const char SS21[] PROGMEM = "_ss_after";
static const char SS22[] PROGMEM = "Smart Schedule nach der Zielzeit";
static const char SS23[] PROGMEM = "select.";
static const char SS24[] PROGMEM = "{{ value_json.AFTER }}";
static const char SS25[] PROGMEM = "{\"AFTER\":\"{{ value }}\"}";
static const char SS26[] PROGMEM = "mdi:pump";
static const char SS27[] PROGMEM = "button";
static const char SS28[] PROGMEM = "_ss_start";
static const char SS29[] PROGMEM = "Smart Schedule starten";
static const char SS30[] PROGMEM = "button.";
static const char SS31[] PROGMEM = "mdi:play";
static const char SS32[] PROGMEM = "{\"ACTION\":\"start\"}";
static const char SS33[] PROGMEM = "_ss_cancel";
static const char SS34[] PROGMEM = "Smart Schedule abbrechen";
static const char SS35[] PROGMEM = "mdi:stop";
static const char SS36[] PROGMEM = "{\"ACTION\":\"cancel\"}";
static const char SS37[] PROGMEM = "binary_sensor";
static const char SS38[] PROGMEM = "_ss_active";
static const char SS39[] PROGMEM = "Smart Schedule aktiv";
static const char SS40[] PROGMEM = "binary_sensor.";
static const char SS41[] PROGMEM = "{{ 'ON' if value_json.ACTIVE else 'OFF' }}";
static const char SS42[] PROGMEM = "mdi:calendar-check";
static const char SS43[] PROGMEM = "sensor";
static const char SS44[] PROGMEM = "_ss_heat_start";
static const char SS45[] PROGMEM = "Smart Schedule Heizstart";
static const char SS46[] PROGMEM = "sensor.";
static const char SS47[] PROGMEM = "{{ as_datetime(value_json.START) if value_json.START else None }}";
static const char SS48[] PROGMEM = "mdi:clock-start";
static const char SS49[] PROGMEM = "timestamp";
static const char SS50[] PROGMEM = "_ss_cost";
static const char SS51[] PROGMEM = "Smart Schedule Kosten";
static const char SS52[] PROGMEM = "{{ value_json.COST }}";
static const char SS53[] PROGMEM = "mdi:cash";
static const char SS54[] PROGMEM = "EUR"; // HA wants an ISO code for monetary; the currency only lives in the browser
static const char SS55[] PROGMEM = "monetary";
// Energy costs (upstream a249d53 / 21fd180): COST and COSTD of /times
static const char EC0[] PROGMEM = "sensor";
static const char EC1[] PROGMEM = "_cost_total";
static const char EC2[] PROGMEM = "Energiekosten gesamt";
static const char EC3[] PROGMEM = "sensor.";
static const char EC4[] PROGMEM = "/times";
static const char EC5[] PROGMEM = "{{ value_json.COST | round(2) }}";
static const char EC6[] PROGMEM = "_cost_today";
static const char EC7[] PROGMEM = "Energiekosten heute";
static const char EC8[] PROGMEM = "{{ value_json.COSTD | round(2) }}";
static const char EC9[] PROGMEM = "total";
static const char EC10[] PROGMEM = "700";
// Last care-product doses: amount + when, from /times. Unset
// timestamps render "None" so HA shows "unknown" instead of 1970.
static const char DS0[] PROGMEM = "_dose_ph";
static const char DS1[] PROGMEM = "Letzte Dosierung pH";
static const char DS2[] PROGMEM = "{{ value_json.DPH }}";
static const char DS3[] PROGMEM = "_dose_ph_time";
static const char DS4[] PROGMEM = "Letzte Dosierung pH Zeitpunkt";
static const char DS5[] PROGMEM = "{{ as_datetime(value_json.DPHTIME | int) if value_json.DPHTIME | int > 0 else None }}";
static const char DS6[] PROGMEM = "_dose_cl";
static const char DS7[] PROGMEM = "Letzte Dosierung Chlor";
static const char DS8[] PROGMEM = "{{ value_json.DCL }} {{ 'Tabletten' if value_json.DCLTAB else 'g' }}";
static const char DS9[] PROGMEM = "_dose_cl_time";
static const char DS10[] PROGMEM = "Letzte Dosierung Chlor Zeitpunkt";
static const char DS11[] PROGMEM = "{{ as_datetime(value_json.DCLTIME | int) if value_json.DCLTIME | int > 0 else None }}";
static const char DS12[] PROGMEM = "_dose_ta";
static const char DS13[] PROGMEM = "Letzte Dosierung Alkalität";
static const char DS14[] PROGMEM = "{{ value_json.DTA }}";
static const char DS15[] PROGMEM = "_dose_ta_time";
static const char DS16[] PROGMEM = "Letzte Dosierung Alkalität Zeitpunkt";
static const char DS17[] PROGMEM = "{{ as_datetime(value_json.DTATIME | int) if value_json.DTATIME | int > 0 else None }}";
static const char DS18[] PROGMEM = "mdi:beaker-plus-outline";
static const char DS19[] PROGMEM = "g";
static const char DS20[] PROGMEM = "timestamp";
// all entity strings in flash, shared between rows (see the SS block above)
static const char HS0[] PROGMEM = "number";
static const char HS1[] PROGMEM = "_brightness";
static const char HS2[] PROGMEM = "Helligkeit";
static const char HS3[] PROGMEM = "number.";
static const char HS4[] PROGMEM = "/message";
static const char HS5[] PROGMEM = "{{ value_json.BRTBASE }}";
static const char HS6[] PROGMEM = "/command";
static const char HS7[] PROGMEM = "{CMD:12,VALUE:{{ value | int }}}";
static const char HS8[] PROGMEM = "0";
static const char HS9[] PROGMEM = "8";
static const char HS10[] PROGMEM = "slider";
static const char HS11[] PROGMEM = "sensor";
static const char HS12[] PROGMEM = "_reboot_time";
static const char HS13[] PROGMEM = "Letzter Neustart";
static const char HS14[] PROGMEM = "sensor.";
static const char HS15[] PROGMEM = "/reboot_time";
static const char HS16[] PROGMEM = "{{ as_datetime(value) if value and value != 'Z' else None }}";
static const char HS17[] PROGMEM = "timestamp";
static const char HS18[] PROGMEM = "_reboot_reason";
static const char HS19[] PROGMEM = "Neustartgrund";
static const char HS20[] PROGMEM = "/reboot_reason";
static const char HS21[] PROGMEM = "_ssid";
static const char HS22[] PROGMEM = "WLAN-SSID";
static const char HS23[] PROGMEM = "/other";
static const char HS24[] PROGMEM = "{{ value_json.SSID }}";
static const char HS25[] PROGMEM = "700";
static const char HS26[] PROGMEM = "_rssi";
static const char HS27[] PROGMEM = "WLAN-RSSI";
static const char HS28[] PROGMEM = "{{ value_json.RSSI }}";
static const char HS29[] PROGMEM = "mdi:wifi";
static const char HS30[] PROGMEM = "dBm";
static const char HS31[] PROGMEM = "signal_strength";
static const char HS32[] PROGMEM = "measurement";
static const char HS33[] PROGMEM = "_ip";
static const char HS34[] PROGMEM = "IP-Adresse";
static const char HS35[] PROGMEM = "{{ value_json.IP }}";
static const char HS36[] PROGMEM = "mdi:ip";
static const char HS37[] PROGMEM = "_connect_count";
static const char HS38[] PROGMEM = "MQTT-Verbindungszähler";
static const char HS39[] PROGMEM = "/MQTT_Connect_Count";
static const char HS40[] PROGMEM = "_error";
static const char HS41[] PROGMEM = "Fehler";
static const char HS42[] PROGMEM = "{{ value_json.ERR }}";
static const char HS43[] PROGMEM = "mdi:alert-circle-outline";
static const char HS44[] PROGMEM = "_time_to_ready";
static const char HS45[] PROGMEM = "Bereit in";
static const char HS46[] PROGMEM = "/times";
static const char HS47[] PROGMEM = "{% if value_json.T2R == -2 %}0{% elif value_json.T2R == -1 %}999{% else %}{{ value_json.T2R }}{% endif %}";
static const char HS48[] PROGMEM = "mdi:clock";
static const char HS49[] PROGMEM = "h";
static const char HS50[] PROGMEM = "duration";
static const char HS51[] PROGMEM = "_energy";
static const char HS52[] PROGMEM = "Gesamtenergieverbrauch";
static const char HS53[] PROGMEM = "{{ value_json.KWH | round(2) }}";
static const char HS54[] PROGMEM = "mdi:flash";
static const char HS55[] PROGMEM = "kWh";
static const char HS56[] PROGMEM = "energy";
static const char HS57[] PROGMEM = "total_increasing";
static const char HS58[] PROGMEM = "_today";
static const char HS59[] PROGMEM = "Energieverbrauch heute";
static const char HS60[] PROGMEM = "{{ value_json.KWHD | round(2) }}";
static const char HS61[] PROGMEM = "_power";
static const char HS62[] PROGMEM = "Aktuelle Leistungsaufnahme";
static const char HS63[] PROGMEM = "{{ value_json.WATT | int }}";
static const char HS64[] PROGMEM = "W";
static const char HS65[] PROGMEM = "power";
static const char HS66[] PROGMEM = "_chlorine_age";
static const char HS67[] PROGMEM = "Letzte Chlorung";
static const char HS68[] PROGMEM = "{{ as_datetime(value_json.CLTIME | int) }}";
static const char HS69[] PROGMEM = "hass:hand-coin-outline";
static const char HS70[] PROGMEM = "_filter_age";
static const char HS71[] PROGMEM = "Letzter Filterwechsel";
static const char HS72[] PROGMEM = "{{ as_datetime(value_json.FTIME | int) }}";
static const char HS73[] PROGMEM = "hass:air-filter";
static const char HS74[] PROGMEM = "_filter_clean_age";
static const char HS75[] PROGMEM = "Letzte Filterreinigung";
static const char HS76[] PROGMEM = "{{ as_datetime(value_json.FCTIME | int) }}";
static const char HS77[] PROGMEM = "mdi:filter-check-outline";
static const char HS78[] PROGMEM = "_water_change_age";
static const char HS79[] PROGMEM = "Letzter Wasserwechsel";
static const char HS80[] PROGMEM = "{{ as_datetime(value_json.WCTIME | int) }}";
static const char HS81[] PROGMEM = "mdi:water-sync";
static const char HS82[] PROGMEM = "_chlorine_interval";
static const char HS83[] PROGMEM = "Chlorintervall";
static const char HS84[] PROGMEM = "{{ value_json.CLINT }}";
static const char HS85[] PROGMEM = "d";
static const char HS86[] PROGMEM = "_filter_interval";
static const char HS87[] PROGMEM = "Filterwechselintervall";
static const char HS88[] PROGMEM = "{{ value_json.FINT }}";
static const char HS89[] PROGMEM = "_filter_clean_interval";
static const char HS90[] PROGMEM = "Filterreinigungsintervall";
static const char HS91[] PROGMEM = "{{ value_json.FCINT }}";
static const char HS92[] PROGMEM = "_water_change_interval";
static const char HS93[] PROGMEM = "Wasserwechselintervall";
static const char HS94[] PROGMEM = "{{ value_json.WCINT }}";
static const char HS95[] PROGMEM = "_uptime";
static const char HS96[] PROGMEM = "Betriebszeit";
static const char HS97[] PROGMEM = "{{ ( (value_json.UPTIME|int)/3600) }}";
static const char HS98[] PROGMEM = "mdi:clock-outline";
static const char HS99[] PROGMEM = "_pumptime";
static const char HS100[] PROGMEM = "Pumpenlaufzeit";
static const char HS101[] PROGMEM = "{{ ( (value_json.PUMPTIME|int)/3600) }}";
static const char HS102[] PROGMEM = "_heatertime";
static const char HS103[] PROGMEM = "Heizungslaufzeit";
static const char HS104[] PROGMEM = "{{ ( (value_json.HEATINGTIME|int)/3600) }}";
static const char HS105[] PROGMEM = "_airtime";
static const char HS106[] PROGMEM = "AirJet Laufzeit";
static const char HS107[] PROGMEM = "{{ ( (value_json.AIRTIME|int)/3600) }}";
static const char HS108[] PROGMEM = "_temp_c";
static const char HS109[] PROGMEM = "Aktuelle Temperatur";
static const char HS110[] PROGMEM = "{{ value_json.TMPC }}";
static const char HS111[] PROGMEM = "°C";
static const char HS112[] PROGMEM = "temperature";
static const char HS113[] PROGMEM = "_target_temp_c";
static const char HS114[] PROGMEM = "Zieltemperatur";
static const char HS115[] PROGMEM = "{{ value_json.TGTC }}";
static const char HS116[] PROGMEM = "_amb_temp_c";
static const char HS117[] PROGMEM = "Umgebungstemperatur";
static const char HS118[] PROGMEM = "{{ value_json.AMBC }}";
static const char HS119[] PROGMEM = "_amb_temp_c_manual";
static const char HS120[] PROGMEM = "{CMD:15,VALUE:{{ value | int }}}";
static const char HS121[] PROGMEM = "mdi:thermometer";
static const char HS122[] PROGMEM = "-20";
static const char HS123[] PROGMEM = "50";
static const char HS124[] PROGMEM = "binary_sensor";
static const char HS125[] PROGMEM = "_ready";
static const char HS126[] PROGMEM = "Bereit";
static const char HS127[] PROGMEM = "binary_sensor.";
static const char HS128[] PROGMEM = "{% if value_json.TMP > 30 %}{% if value_json.TMP >= value_json.TGT-1 %}ON{% else %}OFF{% endif %}{% else %}OFF{% endif %}";
static const char HS129[] PROGMEM = "mdi:hot-tub";
static const char HS130[] PROGMEM = "switch";
static const char HS131[] PROGMEM = "_heat_regulation";
static const char HS132[] PROGMEM = "Heizung";
static const char HS133[] PROGMEM = "switch.";
static const char HS134[] PROGMEM = "{% if value_json.RED == 1 %}1{% elif value_json.GRN == 1 %}1{% else %}0{% endif %}";
static const char HS135[] PROGMEM = "mdi:radiator";
static const char HS136[] PROGMEM = "{CMD:3,VALUE:true}";
static const char HS137[] PROGMEM = "{CMD:3,VALUE:false}";
static const char HS138[] PROGMEM = "1";
static const char HS139[] PROGMEM = "_jets";
static const char HS140[] PROGMEM = "HydroJet";
static const char HS141[] PROGMEM = "{{ value_json.HJT }}";
static const char HS142[] PROGMEM = "mdi:hydro-power";
static const char HS143[] PROGMEM = "{CMD:11,VALUE:true}";
static const char HS144[] PROGMEM = "{CMD:11,VALUE:false}";
static const char HS145[] PROGMEM = "_airbubbles";
static const char HS146[] PROGMEM = "AirJet";
static const char HS147[] PROGMEM = "{{ value_json.AIR }}";
static const char HS148[] PROGMEM = "mdi:chart-bubble";
static const char HS149[] PROGMEM = "{CMD:2,VALUE:true}";
static const char HS150[] PROGMEM = "{CMD:2,VALUE:false}";
static const char HS151[] PROGMEM = "_pump";
static const char HS152[] PROGMEM = "Filterpumpe";
static const char HS153[] PROGMEM = "{{ value_json.FLT }}";
static const char HS154[] PROGMEM = "mdi:pump";
static const char HS155[] PROGMEM = "{CMD:4,VALUE:true}";
static const char HS156[] PROGMEM = "{CMD:4,VALUE:false}";
static const char HS157[] PROGMEM = "_unit";
static const char HS158[] PROGMEM = "°F/°C";
static const char HS159[] PROGMEM = "{{ value_json.UNT }}";
static const char HS160[] PROGMEM = "mdi:circle-outline";
static const char HS161[] PROGMEM = "{CMD:1,VALUE:true}";
static const char HS162[] PROGMEM = "{CMD:1,VALUE:false}";
static const char HS163[] PROGMEM = "Ein/Aus";
static const char HS164[] PROGMEM = "{{ value_json.PWR }}";
static const char HS165[] PROGMEM = "mdi:power";
static const char HS166[] PROGMEM = "{CMD:22,VALUE:true}";
static const char HS167[] PROGMEM = "{CMD:22,VALUE:false}";
static const char HS168[] PROGMEM = "_lock";
static const char HS169[] PROGMEM = "Tastensperre";
static const char HS170[] PROGMEM = "{{ value_json.LCK }}";
static const char HS171[] PROGMEM = "mdi:lock";
static const char HS172[] PROGMEM = "{CMD:23,VALUE:true}";
static const char HS173[] PROGMEM = "{CMD:23,VALUE:false}";
static const char HS174[] PROGMEM = "button";
static const char HS175[] PROGMEM = "_reset_chlorine";
static const char HS176[] PROGMEM = "Timer Chlor zurücksetzen";
static const char HS177[] PROGMEM = "button.";
static const char HS178[] PROGMEM = "mdi:restart";
static const char HS179[] PROGMEM = "{CMD:9,VALUE:true}";
static const char HS180[] PROGMEM = "_reset_filter";
static const char HS181[] PROGMEM = "Timer Filterwechsel zurücksetzen";
static const char HS182[] PROGMEM = "{CMD:10,VALUE:true}";
static const char HS183[] PROGMEM = "_reset_clean_filter";
static const char HS184[] PROGMEM = "Timer Filterreinigung zurücksetzen";
static const char HS185[] PROGMEM = "mdi:filter-variant";
static const char HS186[] PROGMEM = "{CMD:24,VALUE:true}";
static const char HS187[] PROGMEM = "_reset_water_change";
static const char HS188[] PROGMEM = "Timer Wasserwechsel zurücksetzen";
static const char HS189[] PROGMEM = "{CMD:25,VALUE:true}";
static const char HS190[] PROGMEM = "_restart_esp";
static const char HS191[] PROGMEM = "WifiWhirl Modul neustarten";
static const char HS192[] PROGMEM = "restart";
static const char HS193[] PROGMEM = "{CMD:6,VALUE:true}";
static const char HS194[] PROGMEM = "_target_temp_c_set";
static const char HS195[] PROGMEM = "Zieltemperatur einstellen";
static const char HS196[] PROGMEM = "{CMD:0,VALUE:{{ value | int }}}";
static const char HS197[] PROGMEM = "20";
static const char HS198[] PROGMEM = "40";
static const char HS199[] PROGMEM = "_ph_value";
static const char HS200[] PROGMEM = "pH-Wert";
static const char HS201[] PROGMEM = "{{ value_json.PHVAL / 10 }}";
static const char HS202[] PROGMEM = "{CMD:27,VALUE:{{ (value * 10) | int }}}";
static const char HS203[] PROGMEM = "mdi:beaker-outline";
static const char HS204[] PROGMEM = "pH";
static const char HS205[] PROGMEM = "14";
static const char HS206[] PROGMEM = "box";
static const char HS207[] PROGMEM = "_cl_value";
static const char HS208[] PROGMEM = "Chlorgehalt";
static const char HS209[] PROGMEM = "{{ value_json.CLVAL / 10 }}";
static const char HS210[] PROGMEM = "{CMD:28,VALUE:{{ (value * 10) | int }}}";
static const char HS211[] PROGMEM = "mdi:flask-outline";
static const char HS212[] PROGMEM = "mg/L";
static const char HS213[] PROGMEM = "10";
static const char HS214[] PROGMEM = "_cya_value";
static const char HS215[] PROGMEM = "Cyanursäure";
static const char HS216[] PROGMEM = "{{ value_json.CYAVAL / 10 }}";
static const char HS217[] PROGMEM = "{CMD:29,VALUE:{{ (value * 10) | int }}}";
static const char HS218[] PROGMEM = "mdi:shield-sun-outline";
static const char HS219[] PROGMEM = "100";
static const char HS220[] PROGMEM = "_alk_value";
static const char HS221[] PROGMEM = "Alkalität";
static const char HS222[] PROGMEM = "{{ value_json.ALKVAL }}";
static const char HS223[] PROGMEM = "{CMD:30,VALUE:{{ value | int }}}";
static const char HS224[] PROGMEM = "mdi:water";
static const char HS225[] PROGMEM = "300";
static const char HS226[] PROGMEM = "_ph_time";
static const char HS227[] PROGMEM = "pH zuletzt gemessen";
static const char HS228[] PROGMEM = "{{ as_datetime(value_json.PHTIME | int) }}";
static const char HS229[] PROGMEM = "_cl_time";
static const char HS230[] PROGMEM = "Chlor zuletzt gemessen";
static const char HS231[] PROGMEM = "{{ as_datetime(value_json.CLVTIME | int) }}";
static const char HS232[] PROGMEM = "_cya_time";
static const char HS233[] PROGMEM = "Cyanursäure zuletzt gemessen";
static const char HS234[] PROGMEM = "{{ as_datetime(value_json.CYATIME | int) }}";
static const char HS235[] PROGMEM = "_alk_time";
static const char HS236[] PROGMEM = "Alkalität zuletzt gemessen";
static const char HS237[] PROGMEM = "{{ as_datetime(value_json.ALKTIME | int) }}";
static const HaEntityConfig entities[] PROGMEM = {
    // Control entities
    {HS0, HS1, HS2, HS3, HS4, HS5, HS6, HS7, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS8, HS9, HS10, nullptr}, // number _brightness: Helligkeit

    // System information sensors
    {HS11, HS12, HS13, HS14, HS15, HS16, nullptr, nullptr, nullptr, nullptr, HS17, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _reboot_time: Letzter Neustart
    {HS11, HS18, HS19, HS14, HS20, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _reboot_reason: Neustartgrund

    // Network status sensors
    {HS11, HS21, HS22, HS14, HS23, HS24, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _ssid: WLAN-SSID
    {HS11, HS26, HS27, HS14, HS23, HS28, nullptr, nullptr, HS29, HS30, HS31, HS32, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _rssi: WLAN-RSSI
    {HS11, HS33, HS34, HS14, HS23, HS35, nullptr, nullptr, HS36, nullptr, nullptr, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _ip: IP-Adresse
    {HS11, HS37, HS38, HS14, HS39, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _connect_count: MQTT-Verbindungszähler
    {HS11, HS40, HS41, HS14, HS4, HS42, nullptr, nullptr, HS43, nullptr, nullptr, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _error: Fehler
    {HS11, HS44, HS45, HS14, HS46, HS47, nullptr, nullptr, HS48, HS49, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _time_to_ready: Bereit in

    // Energy monitoring sensors
    {HS11, HS51, HS52, HS14, HS46, HS53, nullptr, nullptr, HS54, HS55, HS56, HS57, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _energy: Gesamtenergieverbrauch
    {HS11, HS58, HS59, HS14, HS46, HS60, nullptr, nullptr, HS54, HS55, HS56, HS57, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _today: Energieverbrauch heute
    {EC0, EC1, EC2, EC3, EC4, EC5, nullptr, nullptr, SS53, SS54, SS55, EC9, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 EC1: EC2
    {EC0, EC6, EC7, EC3, EC4, EC8, nullptr, nullptr, SS53, SS54, SS55, EC9, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 EC6: EC7
    {HS11, HS61, HS62, HS14, HS46, HS63, nullptr, nullptr, HS54, HS64, HS65, HS32, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _power: Aktuelle Leistungsaufnahme

    // Maintenance tracking sensors
    {HS11, HS66, HS67, HS14, HS46, HS68, nullptr, nullptr, HS69, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _chlorine_age: Letzte Chlorung
    {HS11, HS70, HS71, HS14, HS46, HS72, nullptr, nullptr, HS73, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _filter_age: Letzter Filterwechsel
    {HS11, HS74, HS75, HS14, HS46, HS76, nullptr, nullptr, HS77, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _filter_clean_age: Letzte Filterreinigung
    {HS11, HS78, HS79, HS14, HS46, HS80, nullptr, nullptr, HS81, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _water_change_age: Letzter Wasserwechsel

    // Last care-product doses: pH/TA in g (minus = reducer), chlorine in g or tablets
    {EC0, DS0, DS1, EC3, EC4, DS2, nullptr, nullptr, DS18, DS19, nullptr, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS0: DS1
    {EC0, DS3, DS4, EC3, EC4, DS5, nullptr, nullptr, DS18, nullptr, DS20, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS3: DS4
    {EC0, DS6, DS7, EC3, EC4, DS8, nullptr, nullptr, DS18, nullptr, nullptr, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS6: DS7
    {EC0, DS9, DS10, EC3, EC4, DS11, nullptr, nullptr, DS18, nullptr, DS20, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS9: DS10
    {EC0, DS12, DS13, EC3, EC4, DS14, nullptr, nullptr, DS18, DS19, nullptr, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS12: DS13
    {EC0, DS15, DS16, EC3, EC4, DS17, nullptr, nullptr, DS18, nullptr, DS20, nullptr, EC10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // EC0 DS15: DS16

    // Maintenance interval configuration sensors (read-only)
    {HS11, HS82, HS83, HS14, HS46, HS84, nullptr, nullptr, HS69, HS85, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _chlorine_interval: Chlorintervall
    {HS11, HS86, HS87, HS14, HS46, HS88, nullptr, nullptr, HS73, HS85, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _filter_interval: Filterwechselintervall
    {HS11, HS89, HS90, HS14, HS46, HS91, nullptr, nullptr, HS77, HS85, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _filter_clean_interval: Filterreinigungsintervall
    {HS11, HS92, HS93, HS14, HS46, HS94, nullptr, nullptr, HS81, HS85, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _water_change_interval: Wasserwechselintervall

    // Runtime tracking sensors
    {HS11, HS95, HS96, HS14, HS46, HS97, nullptr, nullptr, HS98, HS49, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _uptime: Betriebszeit
    {HS11, HS99, HS100, HS14, HS46, HS101, nullptr, nullptr, HS98, HS49, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _pumptime: Pumpenlaufzeit
    {HS11, HS102, HS103, HS14, HS46, HS104, nullptr, nullptr, HS98, HS49, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _heatertime: Heizungslaufzeit
    {HS11, HS105, HS106, HS14, HS46, HS107, nullptr, nullptr, HS98, HS49, HS50, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _airtime: AirJet Laufzeit

    // Temperature sensors
    // {"sensor", "_temperature_f", "Temperatur (F)", "sensor.", "/message", "{{ value_json.TMPF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HS11, HS108, HS109, HS14, HS4, HS110, nullptr, nullptr, nullptr, HS111, HS112, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _temp_c: Aktuelle Temperatur
    // {"sensor", "_virtual_temp_f", "Virtuelle Temperatur (F)", "sensor.", "/message", "{{ value_json.VTMF | round(2) }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"sensor", "_virtual_temp_c", "Virtuelle Temperatur", "sensor.", "/message", "{{ value_json.VTMC | round(2) }}", nullptr, nullptr, nullptr, "°C", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"sensor", "_target_temp_f", "Solltemperatur (F)", "sensor.", "/message", "{{ value_json.TGTF }}", nullptr, nullptr, nullptr, "°F", "temperature", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HS11, HS113, HS114, HS14, HS4, HS115, nullptr, nullptr, nullptr, HS111, HS112, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _target_temp_c: Zieltemperatur
    {HS11, HS116, HS117, HS14, HS4, HS118, nullptr, nullptr, nullptr, HS111, HS112, HS32, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _amb_temp_c: Umgebungstemperatur
    {HS0, HS119, HS117, HS3, HS4, HS118, HS6, HS120, HS121, HS111, HS112, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS122, HS123, HS10, nullptr}, // number _amb_temp_c_manual: Umgebungstemperatur

    // Status sensors
    // {"binary_sensor", "_lock", "Tastensperre", "binary_sensor.", "/message", "{% if value_json.LCK == 1 %}OFF{% else %}ON{% endif %}", nullptr, nullptr, nullptr, nullptr, "lock", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // {"binary_sensor", "_heater", "Heizung", "binary_sensor.", "/message", "{% if value_json.RED == 1 %}ON{% else %}OFF{% endif %}", nullptr, nullptr, nullptr, nullptr, "heat", nullptr, "700", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HS124, HS125, HS126, HS127, HS4, HS128, nullptr, nullptr, HS129, nullptr, nullptr, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // binary_sensor _ready: Bereit
    // {"binary_sensor", "_connection", "Verbindung", "binary_sensor.", "/Status", nullptr, nullptr, nullptr, nullptr, nullptr, "connectivity", nullptr, nullptr, "Alive", "Dead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Control switches
    {HS130, HS131, HS132, HS133, HS4, HS134, HS6, nullptr, HS135, nullptr, HS130, nullptr, HS25, HS136, HS137, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _heat_regulation: Heizung
    {HS130, HS139, HS140, HS133, HS4, HS141, HS6, nullptr, HS142, nullptr, HS130, nullptr, HS25, HS143, HS144, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _jets: HydroJet
    {HS130, HS145, HS146, HS133, HS4, HS147, HS6, nullptr, HS148, nullptr, HS130, nullptr, HS25, HS149, HS150, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _airbubbles: AirJet
    {HS130, HS151, HS152, HS133, HS4, HS153, HS6, nullptr, HS154, nullptr, HS130, nullptr, HS25, HS155, HS156, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _pump: Filterpumpe
    {HS130, HS157, HS158, HS133, HS4, HS159, HS6, nullptr, HS160, nullptr, nullptr, nullptr, HS25, HS161, HS162, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _unit: °F/°C
    // {"switch", "_ctrl", "Steuerung übernehmen", "switch.", "/message", "{{ value_json.GOD }}", "/command", nullptr, "mdi:steering", nullptr, nullptr, nullptr, "700", "{CMD:17,VALUE:true}", "{CMD:17,VALUE:false}", "1", "0", nullptr, nullptr, nullptr, nullptr, "0"},
    {HS130, HS61, HS163, HS133, HS4, HS164, HS6, nullptr, HS165, nullptr, HS130, nullptr, HS25, HS166, HS167, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _power: Ein/Aus
    {HS130, HS168, HS169, HS133, HS4, HS170, HS6, nullptr, HS171, nullptr, nullptr, nullptr, HS25, HS172, HS173, HS138, HS8, nullptr, nullptr, nullptr, nullptr, nullptr}, // switch _lock: Tastensperre

    // Reset buttons
    {HS174, HS175, HS176, HS177, nullptr, nullptr, HS6, nullptr, HS178, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS179, nullptr, nullptr, nullptr, nullptr}, // button _reset_chlorine: Timer Chlor zurücksetzen
    {HS174, HS180, HS181, HS177, nullptr, nullptr, HS6, nullptr, HS178, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS182, nullptr, nullptr, nullptr, nullptr}, // button _reset_filter: Timer Filterwechsel zurücksetzen
    {HS174, HS183, HS184, HS177, nullptr, nullptr, HS6, nullptr, HS185, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS186, nullptr, nullptr, nullptr, nullptr}, // button _reset_clean_filter: Timer Filterreinigung zurücksetzen
    {HS174, HS187, HS188, HS177, nullptr, nullptr, HS6, nullptr, HS81, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS189, nullptr, nullptr, nullptr, nullptr}, // button _reset_water_change: Timer Wasserwechsel zurücksetzen
    {HS174, HS190, HS191, HS177, nullptr, nullptr, HS6, nullptr, HS178, nullptr, HS192, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS193, nullptr, nullptr, nullptr, nullptr}, // button _restart_esp: WifiWhirl Modul neustarten

    // Temperature control
    {HS0, HS194, HS195, HS3, HS4, HS115, HS6, HS196, HS121, HS111, HS112, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS197, HS198, HS10, nullptr}, // number _target_temp_c_set: Zieltemperatur einstellen

    // Water quality - settable number entities
    {HS0, HS199, HS200, HS3, HS46, HS201, HS6, HS202, HS203, HS204, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS8, HS205, HS206, nullptr}, // number _ph_value: pH-Wert
    {HS0, HS207, HS208, HS3, HS46, HS209, HS6, HS210, HS211, HS212, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS8, HS213, HS206, nullptr}, // number _cl_value: Chlorgehalt
    {HS0, HS214, HS215, HS3, HS46, HS216, HS6, HS217, HS218, HS212, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS8, HS219, HS206, nullptr}, // number _cya_value: Cyanursäure
    {HS0, HS220, HS221, HS3, HS46, HS222, HS6, HS223, HS224, HS212, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, HS8, HS225, HS206, nullptr}, // number _alk_value: Alkalität

    // Water quality - timestamp sensors
    {HS11, HS226, HS227, HS14, HS46, HS228, nullptr, nullptr, HS203, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _ph_time: pH zuletzt gemessen
    {HS11, HS229, HS230, HS14, HS46, HS231, nullptr, nullptr, HS211, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _cl_time: Chlor zuletzt gemessen
    {HS11, HS232, HS233, HS14, HS46, HS234, nullptr, nullptr, HS218, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _cya_time: Cyanursäure zuletzt gemessen
    {HS11, HS235, HS236, HS14, HS46, HS237, nullptr, nullptr, HS224, nullptr, HS17, nullptr, HS25, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _alk_time: Alkalität zuletzt gemessen

    // Smart Schedule: inputs are staged in the module, "Starten" applies them
    {SS0, SS1, SS2, SS3, SS4, SS5, SS6, SS7, SS8, SS9, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, SS10, SS11, SS12, nullptr}, // number _ss_temp: Smart Schedule Zieltemperatur
    {SS13, SS14, SS15, SS16, SS4, SS17, SS6, SS18, SS19, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // text _ss_time: Smart Schedule Zielzeit (JJJJ-MM-TT HH:MM)
    {SS56, SS57, SS58, SS59, SS4, SS60, SS6, SS61, SS19, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // datetime _ss_datetime: Smart Schedule Zielzeit
    {SS20, SS21, SS22, SS23, SS4, SS24, SS6, SS25, SS26, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // select _ss_after: Smart Schedule nach der Zielzeit
    {SS27, SS28, SS29, SS30, nullptr, nullptr, SS6, nullptr, SS31, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, SS32, nullptr, nullptr, nullptr, nullptr}, // button _ss_start: Smart Schedule starten
    {SS27, SS33, SS34, SS30, nullptr, nullptr, SS6, nullptr, SS35, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, SS36, nullptr, nullptr, nullptr, nullptr}, // button _ss_cancel: Smart Schedule abbrechen
    {SS37, SS38, SS39, SS40, SS4, SS41, nullptr, nullptr, SS42, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // binary_sensor _ss_active: Smart Schedule aktiv
    {SS43, SS44, SS45, SS46, SS4, SS47, nullptr, nullptr, SS48, nullptr, SS49, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _ss_heat_start: Smart Schedule Heizstart
    {SS43, SS50, SS51, SS46, SS4, SS52, nullptr, nullptr, SS53, SS54, SS55, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, // sensor _ss_cost: Smart Schedule Kosten

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
    
    if (payloadSize == 0)
    {
        return false;
    }
    
    if (!mqttClient->connected())
    {
        Serial.print(F(" - MQTT not connected"));
        return false;
    }

    if (!mqttClient->beginPublish(topic, payloadSize, true))
    {
        Serial.print(F(" - MQTT beginPublish failed"));
        return false;
    }

    serializeJson(doc, *mqttClient);

    bool published = mqttClient->endPublish();
    if (!published)
    {
        Serial.print(F(" - MQTT endPublish failed"));
    }
    
    // Ensure data is sent to network stack
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
bool setupHA()
{
    haDiscoveryInProgress = true;
    bwc->dsp->audiofrequency = 0;
    bwc->loop();
    
    Serial.println(F("========================================"));
    Serial.println(F("HA: HOME ASSISTANT DISCOVERY STARTING"));
    Serial.println(F("========================================"));
    Serial.print(F("HA: Free heap: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    
    #ifdef ESP8266
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    #else
    WiFi.setSleep(false);
    #endif
    Serial.println(F("HA: WiFi sleep mode disabled during discovery"));
    
    // Generate unique chip ID for device identification
    char mychipid_str[16];
#if defined(ESP8266)
    snprintf(mychipid_str, sizeof(mychipid_str), "%u", ESP.getChipId());
#elif defined(ESP32)
    // Use MAC-based ID for ESP32 (no direct chip ID available)
    uint64_t chipid_mac = ESP.getEfuseMac();
    snprintf(mychipid_str, sizeof(mychipid_str), "%04X%08X", (uint16_t)(chipid_mac >> 32), (uint32_t)chipid_mac);
#endif

    // Device metadata - reusable across all entities. On the heap only while
    // discovery runs: as statics these documents (plus climate_doc) held 3.3 KB
    // of RAM for good, and the MQTT socket ran short of memory.
    DynamicJsonDocument devicedoc(500);
    // Entity payload; the climate entity below reuses it (the largest, 1700)
    DynamicJsonDocument doc(1700);
    if (devicedoc.capacity() == 0 || doc.capacity() == 0)
    {
        Serial.println(F("HA: not enough memory for discovery, retry on the next connect"));
        haDiscoveryInProgress = false;
        return false;
    }
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


    const size_t totalEntities = sizeof(entities) / sizeof(entities[0]);
    size_t successCount = 0;
    size_t failureCount = 0;
    const size_t BATCH_SIZE = 5; // OPTIMIZED: Increased from 3 to 5 due to reduced MQTT buffer (768 bytes)
    
    Serial.print(F("HA: Processing "));
    Serial.print(totalEntities);
    Serial.print(F(" entities in batches of "));
    Serial.print(BATCH_SIZE);
    Serial.println(F("..."));
    
    delay(100);

    for (size_t i = 0; i < totalEntities; i++)
    {
        if (i % BATCH_SIZE == 0 && i > 0) {
            Serial.print(F("HA: Batch "));
            Serial.print(i / BATCH_SIZE);
            Serial.print(F(" completed. Free heap: "));
            Serial.print(ESP.getFreeHeap());
            Serial.print(F(" bytes, Max block: "));
            Serial.print(ESP.getMaxFreeBlockSize());
            Serial.println(F(" bytes"));
            
            mqttClient->loop();
            bwc->loop();
            delay(50);
            mqttClient->loop();
            bwc->loop();
            delay(200);
            bwc->loop();
            delay(200);
            
            if (ESP.getMaxFreeBlockSize() < 3000) {
                Serial.print(F("HA: CRITICAL - Heap fragmented! Max block: "));
                Serial.print(ESP.getMaxFreeBlockSize());
                Serial.println(F(" bytes. Discovery stopped, retry on the next connect"));
                // no restart: discovery runs after every boot, so a module short of
                // memory restarted forever. The broker keeps the retained configs.
                haDiscoveryInProgress = false;
                return false;
            }
        }
        
        Serial.print(F(">>> Entity #"));
        Serial.print(i + 1);
        Serial.print(F(": Heap="));
        Serial.print(ESP.getFreeHeap());
        Serial.print(F(", MaxBlock="));
        Serial.print(ESP.getMaxFreeBlockSize());
        Serial.print(F(", Frag="));
        Serial.print(100 - (ESP.getMaxFreeBlockSize() * 100 / ESP.getFreeHeap()));
        Serial.println(F("%"));
        
        if (ESP.getMaxFreeBlockSize() < 3500) {
            Serial.print(F("HA: WARNING - Low contiguous memory before entity #"));
            Serial.print(i + 1);
            Serial.print(F(". Max block: "));
            Serial.print(ESP.getMaxFreeBlockSize());
            Serial.println(F(" bytes. Discovery stopped, retry on the next connect"));
            haDiscoveryInProgress = false; // no restart, see above
            return false;
        }
        
        // Iterate over every entity definition and publish its Home-Assistant
        // auto-discovery payload
        doc.clear();
        PGM_P pgm_p;

        //--------------------------------------------------------------------------------------------------
        /**
         * Pull the *entity_id_suffix* pointer out of PROGMEM. Because the
         * ESP32/ESP8266 store string literals in flash, we cannot access them
         * directly with a normal `const char *` - we must first read the
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
         *    (bwc->weatherEnabled()) already supplies the ambient temperature.
         * 2. `_jets` exists only for the Maldives-2021 model variant.
         */
        //--------------------------------------------------------------------------------------------------
        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0 && bwc->weatherEnabled())
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

        // Add step attribute for water quality number entities (pH and chlorine need 0.1 step)
        pgm_p = (PGM_P)pgm_read_ptr(&entities[i].entity_id_suffix);
        strcpy_P(ram_buffer, pgm_p);
        if (strcmp(ram_buffer, "_ph_value") == 0 || strcmp(ram_buffer, "_cl_value") == 0)
        {
            doc[FPSTR(_step_key)] = 0.1;
        }
        else if (strcmp(ram_buffer, "_cya_value") == 0 || strcmp(ram_buffer, "_alk_value") == 0)
        {
            doc[FPSTR(_step_key)] = 1;
        }
        else if (strcmp(ram_buffer, "_ss_after") == 0)
        {
            JsonArray options = doc.createNestedArray(F("options")); // must match SS_AFTER in mqtt_manager.cpp
            options.add(F("Alles aus"));
            options.add(F("Nur Filterpumpe"));
            options.add(F("Heizung hält Temperatur"));
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

        // Publish the entity
        bool publishResult = false;
        
        if (strcmp(ram_buffer, "_amb_temp_c_manual") == 0)
        {
            Serial.print(F("HA: Publishing entity "));
            Serial.print(component_ram);
            Serial.print(F(" -> _amb_temp_c"));
            publishResult = publishHaEntity(component_ram, "_amb_temp_c", doc);
        }
        else
        {
            Serial.print(F("HA: Publishing entity "));
            Serial.print(component_ram);
            Serial.print(F(" -> "));
            Serial.print(ram_buffer);
            publishResult = publishHaEntity(component_ram, ram_buffer, doc);
        }
        
        if (publishResult)
        {
            Serial.println(F(" OK"));
            successCount++;
            mqttClient->loop();
        }
        else
        {
            Serial.println(F(" FAILED"));
            failureCount++;
            mqttClient->loop();
            continue;
        }
        
        // Small delay between entities
        delay(50);
        
        // Process MQTT messages every 5 entities
        if ((i + 1) % 5 == 0) {
            mqttClient->loop();
        }
    }

    Serial.print(F("HA: Entity summary - Success: "));
    Serial.print(successCount);
    Serial.print(F(", Failed: "));
    Serial.println(failureCount);

    // Climate entity configuration
    {
        JsonDocument &climate_doc = doc;
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
        climate_doc[FPSTR(_mode_stat_tpl)] = F("{% if value_json.RED == 1 or value_json.GRN == 1 %}heat{% elif value_json.FLT == 1 %}fan_only{% else %}off{% endif %}");
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
        Serial.print(F("HA: Publishing climate entity -> _climate"));
        
        bool climateResult = publishHaEntity("climate", "_climate", climate_doc);
        if (climateResult)
        {
            Serial.println(F(" OK"));
        }
        else
        {
            Serial.println(F(" FAILED"));
        }
    }
    
    // ESP8266 stays in WIFI_NONE_SLEEP (set in startWiFi(), and above):
    // modem sleep adds latency and frame loss that breaks large HTTP transfers,
    // and the unit is mains powered.
    #ifndef ESP8266
    WiFi.setSleep(true);
    #endif
    
    haDiscoveryInProgress = false;
    
    Serial.println(F("========================================"));
    Serial.println(F("HA: HOME ASSISTANT DISCOVERY COMPLETE"));
    Serial.println(F("========================================"));
    Serial.print(F("HA: Free heap: "));
    Serial.println(ESP.getFreeHeap());
    return true;
}