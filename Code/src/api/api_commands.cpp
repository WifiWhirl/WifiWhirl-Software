#include "api/api.h"
#include "web/web.h"
#include "net/net.h"
#include "cloud_task.h"

static const uint64_t MAX_COMMAND_EPOCH_SECONDS = 4102444800ULL; // 2100-01-01
static const uint32_t MAX_COMMAND_INTERVAL_SECONDS = 366UL * 24UL * 60UL * 60UL;
static const size_t MAX_COMMAND_TEXT_LENGTH = 96;
static const size_t MAX_CMDQ_UPLOAD_BYTES = 4096;
// Ceiling for the upload parse pool. It must exceed the payload's text size: an
// array-heavy JSON expands in the pool well past its text form, so equal sizes
// would NoMemory-reject valid uploads (or silently drop QEN/EN, defaulting all
// commands to enabled). A worst-case queue - MAXCOMMANDS entries with 96-char
// TXT - needs roughly 4-5 KB, so this covers it with margin. The actual pool is
// scaled to the payload (see handle_cmdq_file): always demanding the ceiling
// failed to allocate on a fragmented heap, which surfaced as "Invalid JSON".
static const size_t CMDQ_UPLOAD_JSON_CAPACITY = 6144;

class ChunkedResponsePrint : public Print
{
public:
    ~ChunkedResponsePrint()
    {
        flush();
    }

    size_t write(uint8_t b) override
    {
        return write(&b, 1);
    }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        size_t written = 0;
        while (written < size)
        {
            size_t space = sizeof(_buffer) - 1 - _len;
            if (space == 0)
            {
                flush();
                space = sizeof(_buffer) - 1;
            }
            size_t n = size - written;
            if (n > space)
                n = space;
            memcpy(_buffer + _len, buffer + written, n);
            _len += n;
            written += n;
        }
        return written;
    }

    void flush() override
    {
        if (_len == 0)
            return;
        _buffer[_len] = '\0';
        server->sendContent(_buffer);
        _len = 0;
    }

private:
    char _buffer[256];
    size_t _len = 0;
};

static bool readIntegerOrBool(const JsonVariantConst &value, int64_t &out)
{
    if (value.isNull() || value.is<const char *>())
        return false;
    if (value.is<bool>())
    {
        out = value.as<bool>() ? 1 : 0;
        return true;
    }
    if (!value.is<int64_t>())
        return false;
    out = value.as<int64_t>();
    return true;
}

static bool readRequiredInteger(const JsonObjectConst &obj, const __FlashStringHelper *key, int64_t &out, String &error)
{
    if (!obj.containsKey(key) || !readIntegerOrBool(obj[key], out))
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    return true;
}

static bool validBoolValue(int64_t value)
{
    return value == 0 || value == 1;
}

static bool validateCommandValue(Commands cmd, int64_t value, uint64_t xtime, String &error)
{
    switch (cmd)
    {
    case SETTARGET:
        if (!((value > 0 && value < 41) || (value > 50 && value < 105)))
            error = F("VALUE out of range for SETTARGET");
        break;
    case SETUNIT:
    case SETBUBBLES:
    case SETHEATER:
    case SETPUMP:
    case SETJETS:
    case SETGODMODE:
    case SETFULLPOWER:
    case SETENABLEBUTTONS:
        if (!validBoolValue(value))
            error = F("VALUE must be 0 or 1 for this command");
        break;
    case SETBRIGHTNESS:
        if (value < 0 || value > 8)
            error = F("VALUE out of range for SETBRIGHTNESS");
        break;
    case SETAMBIENTF:
        if (value < -40 || value > 140)
            error = F("VALUE out of range for SETAMBIENTF");
        break;
    case SETAMBIENTC:
        if (value < -40 || value > 60)
            error = F("VALUE out of range for SETAMBIENTC");
        break;
    case SETREADY:
        if (xtime == 0)
            error = F("XTIME must be set for SETREADY");
        break;
    case SETPHVALUE:
        if (value < 0 || value > 140)
            error = F("VALUE out of range for SETPHVALUE");
        break;
    case SETCLVALUE:
        if (value < 0 || value > 100)
            error = F("VALUE out of range for SETCLVALUE");
        break;
    case SETCYAVALUE:
        if (value < 0 || value > 1000)
            error = F("VALUE out of range for SETCYAVALUE");
        break;
    case SETALKVALUE:
        if (value < 0 || value > 300)
            error = F("VALUE out of range for SETALKVALUE");
        break;
    case GETTARGET:
        error = F("Unsupported command");
        break;
    default:
        break;
    }
    return error.length() == 0;
}

static bool buildCommandFromFields(int64_t cmdValue, int64_t value, int64_t xtimeValue,
                                   int64_t intervalValue, const String &text, bool force,
                                   command_que_item &item, String &error)
{
    if (cmdValue < SETTARGET || cmdValue > SETALKVALUE)
    {
        error = F("Unsupported command");
        return false;
    }
    if (xtimeValue < 0 || (uint64_t)xtimeValue > MAX_COMMAND_EPOCH_SECONDS)
    {
        error = F("XTIME out of range");
        return false;
    }
    if (intervalValue < 0 || (uint64_t)intervalValue > MAX_COMMAND_INTERVAL_SECONDS)
    {
        error = F("INTERVAL out of range");
        return false;
    }
    if (text.length() > MAX_COMMAND_TEXT_LENGTH)
    {
        error = F("TXT too long");
        return false;
    }

    Commands cmd = (Commands)cmdValue;
    if (!validateCommandValue(cmd, value, (uint64_t)xtimeValue, error))
        return false;

    item.cmd = cmd;
    item.val = value;
    item.xtime = (uint64_t)xtimeValue;
    item.interval = (uint32_t)intervalValue;
    item.text = text;
    item.force = force;
    return true;
}

// Shared command-object parser. When requireSchedule is true (the HTTP API,
// whose frontend always sends every field) XTIME and INTERVAL must be present;
// when false (webhook/MQTT machine clients) they default to "now"/0 like the
// legacy lax parser, but CMD/VALUE/ranges are still validated.
static bool parseCommandObject(const JsonVariantConst &src, command_que_item &item,
                               bool requireSchedule, String &error)
{
    if (!src.is<JsonObjectConst>())
    {
        error = F("Command must be a JSON object");
        return false;
    }

    JsonObjectConst obj = src.as<JsonObjectConst>();
    int64_t cmdValue = 0;
    int64_t value = 0;
    int64_t xtimeValue = (int64_t)std::time(nullptr);
    int64_t intervalValue = 0;
    if (!readRequiredInteger(obj, F("CMD"), cmdValue, error))
        return false;

    if (requireSchedule)
    {
        if (!readRequiredInteger(obj, F("VALUE"), value, error) ||
            !readRequiredInteger(obj, F("XTIME"), xtimeValue, error) ||
            !readRequiredInteger(obj, F("INTERVAL"), intervalValue, error))
        {
            return false;
        }
    }
    else
    {
        // Machine clients may omit VALUE/XTIME/INTERVAL (e.g. value-less commands
        // like REBOOTESP); default them but reject present-but-malformed fields.
        if (obj.containsKey(F("VALUE")) && !obj[F("VALUE")].isNull() &&
            !readIntegerOrBool(obj[F("VALUE")], value))
        {
            error = F("Missing or invalid VALUE");
            return false;
        }
        if (obj.containsKey(F("XTIME")) && !obj[F("XTIME")].isNull() &&
            !readIntegerOrBool(obj[F("XTIME")], xtimeValue))
        {
            error = F("Missing or invalid XTIME");
            return false;
        }
        if (obj.containsKey(F("INTERVAL")) && !obj[F("INTERVAL")].isNull() &&
            !readIntegerOrBool(obj[F("INTERVAL")], intervalValue))
        {
            error = F("Missing or invalid INTERVAL");
            return false;
        }
    }

    String text = "";
    if (obj.containsKey(F("TXT")) && !obj[F("TXT")].isNull())
    {
        if (!obj[F("TXT")].is<const char *>())
        {
            error = F("TXT must be a string");
            return false;
        }
        text = obj[F("TXT")].as<String>();
    }

    bool force = false;
    if (obj.containsKey(F("FORCE")) && !obj[F("FORCE")].isNull())
    {
        if (!obj[F("FORCE")].is<bool>())
        {
            error = F("FORCE must be boolean");
            return false;
        }
        force = obj[F("FORCE")].as<bool>();
    }

    return buildCommandFromFields(cmdValue, value, xtimeValue, intervalValue, text, force, item, error);
}

static bool parseCommandFromJsonStrict(const JsonVariantConst &src, command_que_item &item, String &error)
{
    return parseCommandObject(src, item, true, error);
}

static bool readQueueIndex(const JsonObjectConst &obj, uint8_t &index, String &error)
{
    int64_t indexValue = 0;
    if (!readRequiredInteger(obj, F("IDX"), indexValue, error))
        return false;
    if (indexValue < 0 || indexValue >= MAXCOMMANDS)
    {
        error = F("IDX out of range");
        return false;
    }
    index = (uint8_t)indexValue;
    return true;
}

static bool readRequired01Field(const JsonObjectConst &obj, const __FlashStringHelper *key,
                                bool &out, String &error)
{
    if (!obj.containsKey(key))
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    JsonVariantConst value = obj[key];
    if (value.is<bool>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    if (value.is<int>())
    {
        int numeric = value.as<int>();
        if (numeric == 0 || numeric == 1)
        {
            out = numeric == 1;
            return true;
        }
    }
    error = String(F("Missing or invalid ")) + String(key);
    return false;
}

static bool read01(const JsonVariantConst &value, bool &out)
{
    if (value.is<bool>())
        return false;
    if (value.is<int>())
    {
        int numeric = value.as<int>();
        if (numeric == 0 || numeric == 1)
        {
            out = numeric == 1;
            return true;
        }
    }
    return false;
}

static bool validate01Array(const JsonArrayConst &arr, size_t len)
{
    if (arr.size() != len)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        bool ignored;
        if (!read01(arr[i], ignored))
            return false;
    }
    return true;
}

/**
 * Validate a command queue item from a JSON command object for machine clients
 * (webhook/MQTT). CMD and VALUE are required and range-checked; XTIME defaults to
 * now and INTERVAL to 0 when omitted, but are validated when present.
 * @param src JSON variant with CMD/VALUE and optional XTIME/INTERVAL/TXT/FORCE
 * @param item output populated on success
 * @param error human-readable reason on failure
 * @return true when the command is valid
 */
bool parseCommandValidated(const JsonVariantConst &src, command_que_item &item, String &error)
{
    if (!parseCommandObject(src, item, false, error))
        return false;
    // Webhook/MQTT clients use this for live control (XTIME defaults to now), so
    // those keep running while the queue is off. A repeating command is a planned
    // automation though - it belongs to the queue switch like any other.
    item.scheduled = item.interval > 0;
    return true;
}

/**
 * Build the "OTHER" status JSON (MQTT state, model, weather, WiFi info, FW, loop rate)
 * Resets the loop counter after reading it. Used by the poll endpoint and MQTT.
 * @param rtn output string receiving the serialized JSON
 */
static void buildOtherInfo(JsonObject doc)
{
    doc[F("CONTENT")] = F("OTHER");
    doc[F("MQTT")] = mqttClient ? mqttClient->state() : -1;
    doc[F("HASJETS")] = bwc->hasjets;
    doc[F("HASGOD")] = bwc->hasgod;
    doc[F("MODEL")] = bwc->getModel();
    doc[F("WEATHER")] = bwc->getWeather();
    doc[F("RSSI")] = WiFi.RSSI();
    doc[F("IP")] = WiFi.localIP().toString();
    doc[F("SSID")] = WiFi.SSID();
    doc[F("FW")] = FW_VERSION;
    doc[F("BUILDENV")] = PIO_ENV_NAME;
    doc[F("NEWFW")] = updateAvailableVersion(); // "" unless a newer version is known
    doc[F("loopfq")] = bwc->loop_count;
    bwc->loop_count = 0;
}

static size_t writeOtherInfo(Print &out)
{
    StaticJsonDocument<640> doc;
    buildOtherInfo(doc.to<JsonObject>());
    return serializeJson(doc, out);
}

void getOtherInfo(String &rtn)
{
    StaticJsonDocument<640> doc;
    buildOtherInfo(doc.to<JsonObject>());

    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize other\"}");
    }
}

/**
 * HTTP polling endpoint: returns a JSON array of [STATES, TIMES, OTHER].
 * The client polls this at a regular interval; the live transport for the SPA.
 */
void handleGetPollData()
{
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, F("application/json"), "");

    ChunkedResponsePrint out;
    out.print('[');
    if (bwc->writeJSONStates(out) == 0)
        out.print(F("{\"error\":\"Failed to serialize states\"}"));
    out.print(',');
    if (bwc->writeJSONTimes(out) == 0)
        out.print(F("{\"error\":\"Failed to serialize times\"}"));
    out.print(',');
    if (writeOtherInfo(out) == 0)
        out.print(F("{\"error\":\"Failed to serialize other\"}"));
    out.print(']');
    out.flush();
    server->sendContent("");
}

/**
 * HTTP command endpoint: accepts the same JSON command format as WebSocket.
 * Used as a fallback when WebSocket connections are not available.
 * Expects POST body: {"CMD":n,"VALUE":v,"XTIME":t,"INTERVAL":i,"TXT":"","FORCE":bool}
 */
void handleSendCommand()
{
    // Only accept POST requests
    if (server->method() != HTTP_POST)
    {
        server->send(405, F("text/plain"), F("Method Not Allowed"));
        return;
    }

    // Parse the JSON command from the request body
    StaticJsonDocument<256> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing command"));
        return;
    }

    command_que_item item;
    String parseError;
    if (!parseCommandFromJsonStrict(doc.as<JsonVariantConst>(), item, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Queue full (max. 20 commands)"));
        return;
    }

    server->send(200, F("text/plain"), String(item.cmd) + " " + String(item.val) + " " + String(item.xtime));
}

/**
 * response for /getcommands/
 * web server prints a json document
 */
void handleGetCommandQueue()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    bwc->getJSONCommandQueue(json);
    server->send(200, F("application/json"), json);
}

/**
 * response for /addcommand/
 * add a command to the queue
 */
void handleAddCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item;
    String parseError;
    if (!parseCommandFromJsonStrict(doc.as<JsonVariantConst>(), item, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }
    item.scheduled = true; // a planned automation: parked while the queue is off
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Queue full (max. 20 commands)"));
        return;
    }

    CloudTask::notifyQueueChanged();
    server->send(200, F("text/plain"), "");
}

/**
 * response for /editcommand/
 * replace a command in the queue with new command
 */
void handleEditCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item;
    String parseError;
    if (!parseCommandFromJsonStrict(doc.as<JsonVariantConst>(), item, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }

    item.scheduled = true; // still a planned automation after the edit

    uint8_t index = 0;
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    if (!readQueueIndex(obj, index, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }
    if (!bwc->edit_command(index, item))
    {
        server->send(404, F("text/plain"), F("Command index not found"));
        return;
    }

    CloudTask::notifyQueueChanged();
    server->send(200, F("text/plain"), "");
}

/**
 * response for /delcommand/
 * replace a command in the queue with new command
 */
void handleDelCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    String parseError;
    uint8_t index = 0;
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    if (!readQueueIndex(obj, index, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }
    if (!bwc->del_command(index))
    {
        server->send(404, F("text/plain"), F("Command index not found"));
        return;
    }

    CloudTask::notifyQueueChanged();
    server->send(200, F("text/plain"), "");
}

/**
 * response for /setcommands/
 * Update command queue metadata without changing command contents.
 * Body may contain QEN and/or IDX + EN.
 */
void handleSetCommandQueue()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<128> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error || !doc.is<JsonObject>())
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    String parseError;

    bool hasQen = obj.containsKey(F("QEN"));
    bool hasCmd = obj.containsKey(F("IDX")) || obj.containsKey(F("EN"));
    if (!hasQen && !hasCmd)
    {
        server->send(400, F("text/plain"), F("No queue changes requested"));
        return;
    }

    // Validate the whole request before mutating anything, otherwise a request
    // that sets QEN but fails IDX/EN validation still globally disables the queue
    // (and persists it) while returning a 400 the client reads as "no change".
    bool queueEnabled = false;
    if (hasQen && !readRequired01Field(obj, F("QEN"), queueEnabled, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }

    uint8_t index = 0;
    bool enabled = false;
    if (hasCmd && (!readQueueIndex(obj, index, parseError) ||
                   !readRequired01Field(obj, F("EN"), enabled, parseError)))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }

    // Apply the per-command change first (it's the only one that can still 404 on
    // a valid-but-out-of-queue index); only touch QEN once everything succeeds.
    if (hasCmd && !bwc->set_command_enabled(index, enabled))
    {
        server->send(404, F("text/plain"), F("Command index not found"));
        return;
    }
    if (hasQen)
        bwc->set_command_queue_enabled(queueEnabled);

    CloudTask::notifyQueueChanged();
    server->send(200, F("text/plain"), "");
}

/**
 * response for /cmdq_file/
 * Upload (validate + persist /cmdq.json and reload the queue) or download the
 * command queue file, selected via the "action"/"ACT" parameter
 */
void handle_cmdq_file()
{
    if (!checkHttpPost(server->method()))
        return;

    // Check for upload action via query parameter
    String action = server->arg(F("action"));

    if (action.equals("upload"))
    {
        // Upload: body contains raw JSON file content
        String data = getRequestBody();
        if (data.length() == 0)
        {
            server->send(400, F("text/plain"), F("No data received"));
            return;
        }
        if (data.length() > MAX_CMDQ_UPLOAD_BYTES)
        {
            server->send(413, F("text/plain"), F("Queue file too large"));
            return;
        }

        // Validate JSON format - cmdq.json structure:
        // {"LEN":n,"QEN":0|1,"CMD":[...],"VALUE":[...],"XTIME":[...],"INTERVAL":[...],"TXT":[...],"EN":[...]}
        // Scale the pool with the payload: a real 20-command queue is well under
        // 1 KB of JSON, so the flat ceiling was an oversized allocation that failed
        // on a fragmented heap - and surfaced as "Invalid JSON format" for a file
        // the device had exported itself seconds earlier.
        size_t poolSize = data.length() * 2 + 512;
        if (poolSize < 2048)
            poolSize = 2048;
        if (poolSize > CMDQ_UPLOAD_JSON_CAPACITY)
            poolSize = CMDQ_UPLOAD_JSON_CAPACITY;
        DynamicJsonDocument validateDoc(poolSize);
        DeserializationError validateError = deserializeJson(validateDoc, data);
        if (validateError == DeserializationError::NoMemory)
        {
            // Never blame the file for a heap problem.
            Serial.printf("CMDQ: upload needs more memory than available (bytes=%u, heap=%u)\n",
                          (unsigned)data.length(), (unsigned)ESP.getFreeHeap());
            server->send(507, F("text/plain"), F("Not enough memory to read the queue file - reboot and retry"));
            return;
        }
        if (validateError)
        {
            server->send(400, F("text/plain"), F("Invalid JSON format"));
            return;
        }

        // Check if root is an object
        if (!validateDoc.is<JsonObject>())
        {
            server->send(400, F("text/plain"), F("File must contain a JSON object"));
            return;
        }

        JsonObject root = validateDoc.as<JsonObject>();

        // Check for required fields
        if (!root.containsKey(F("LEN")) || !root.containsKey(F("CMD")) ||
            !root.containsKey(F("VALUE")) || !root.containsKey(F("XTIME")) ||
            !root.containsKey(F("INTERVAL")))
        {
            server->send(400, F("text/plain"), F("File is missing required fields (LEN, CMD, VALUE, XTIME, INTERVAL)"));
            return;
        }

        // Verify arrays have consistent length
        size_t len = root[F("LEN")].as<size_t>();
        if (len > MAXCOMMANDS)
        {
            server->send(400, F("text/plain"), F("Queue contains too many commands (max. 20)"));
            return;
        }

        if (!root[F("CMD")].is<JsonArray>() || !root[F("VALUE")].is<JsonArray>() ||
            !root[F("XTIME")].is<JsonArray>() || !root[F("INTERVAL")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("CMD, VALUE, XTIME, INTERVAL must be arrays"));
            return;
        }

        JsonArray cmdArr = root[F("CMD")].as<JsonArray>();
        JsonArray valArr = root[F("VALUE")].as<JsonArray>();
        JsonArray timeArr = root[F("XTIME")].as<JsonArray>();
        JsonArray intArr = root[F("INTERVAL")].as<JsonArray>();

        if (cmdArr.size() != len || valArr.size() != len ||
            timeArr.size() != len || intArr.size() != len)
        {
            server->send(400, F("text/plain"), F("Array lengths do not match LEN"));
            return;
        }

        if (root.containsKey(F("TXT")) && !root[F("TXT")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("TXT must be an array"));
            return;
        }

        if (root.containsKey(F("TXT")) && root[F("TXT")].as<JsonArray>().size() != len)
        {
            server->send(400, F("text/plain"), F("TXT length does not match LEN"));
            return;
        }

        JsonArray txtArr = root.containsKey(F("TXT")) ? root[F("TXT")].as<JsonArray>() : JsonArray();
        bool queueEnabled = true;
        if (root.containsKey(F("QEN")) && !read01(root[F("QEN")], queueEnabled))
        {
            server->send(400, F("text/plain"), F("QEN must be 0 or 1"));
            return;
        }
        root[F("QEN")] = queueEnabled ? 1 : 0;

        if (root.containsKey(F("EN")) && !root[F("EN")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("EN must be an array"));
            return;
        }
        JsonArrayConst uploadedEnabled;
        if (root.containsKey(F("EN")))
        {
            uploadedEnabled = root[F("EN")].as<JsonArrayConst>();
        }
        if (!uploadedEnabled.isNull() && !validate01Array(uploadedEnabled, len))
        {
            server->send(400, F("text/plain"), F("EN entries must be 0 or 1"));
            return;
        }

        JsonArray enabledArr;
        if (root.containsKey(F("EN")))
        {
            enabledArr = root[F("EN")].as<JsonArray>();
            for (size_t i = 0; i < len; i++)
            {
                bool enabled;
                read01(enabledArr[i], enabled);
                enabledArr[i] = enabled ? 1 : 0;
            }
        }
        else
        {
            enabledArr = root.createNestedArray(F("EN"));
            for (size_t i = 0; i < len; i++)
            {
                bool enabled = true;
                if (!uploadedEnabled.isNull())
                    read01(uploadedEnabled[i], enabled);
                enabledArr.add(enabled ? 1 : 0);
            }
        }
        for (size_t i = 0; i < len; i++)
        {
            int64_t cmdValue = 0;
            int64_t value = 0;
            int64_t xtimeValue = 0;
            int64_t intervalValue = 0;
            String parseError;
            if (!readIntegerOrBool(cmdArr[i], cmdValue) ||
                !readIntegerOrBool(valArr[i], value) ||
                !readIntegerOrBool(timeArr[i], xtimeValue) ||
                !readIntegerOrBool(intArr[i], intervalValue))
            {
                server->send(400, F("text/plain"), F("Queue contains invalid command fields"));
                return;
            }

            String text = "";
            if (!txtArr.isNull() && !txtArr[i].isNull())
            {
                if (!txtArr[i].is<const char *>())
                {
                    server->send(400, F("text/plain"), F("TXT entries must be strings"));
                    return;
                }
                text = txtArr[i].as<String>();
            }

            command_que_item item;
            if (!buildCommandFromFields(cmdValue, value, xtimeValue, intervalValue, text, false, item, parseError))
            {
                server->send(400, F("text/plain"), parseError);
                return;
            }
        }

        // Validation passed - save the file
        File file = LittleFS.open(F("/cmdq.json"), "w");
        if (!file)
        {
            Serial.println(F("FS: Failed to open /cmdq.json for write"));
            server->send(500, F("text/plain"), F("Error saving file"));
            return;
        }
        // Persist the parsed/validated document, not the raw upload, so any
        // unknown or extra bytes can't survive to flash.
        serializeJson(validateDoc, file);
        file.close();

        // Release this pool first: reloadCommandQueue() allocates its own document,
        // and holding both at once is what makes this handler run out of heap.
        validateDoc.clear();
        validateDoc.shrinkToFit();

        // Reload command queue in BWC
        bwc->reloadCommandQueue();

        server->send(200, F("text/plain"), F("OK"));
        return;
    }

    // Download: parse JSON body for action
    String message = getRequestBody();
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error processing request"));
        return;
    }

    action = doc[F("ACT")].as<String>();

    if (action.equals("download"))
    {
        // Send the live queue so exports are canonical even if an older
        // cmdq.json lacks newer defaulted fields.
        // No reserve() here: the export is a few hundred bytes, while a 4 KB
        // String block right before getJSONCommandQueue's 4 KB JsonDocument was
        // enough to starve the heap. The document then held no members and the
        // download was a file containing "{}".
        String content;
        bwc->getJSONCommandQueue(content);
        // Never hand the browser a truncated export it would happily save.
        if (content.indexOf(F("\"LEN\"")) < 0)
        {
            Serial.printf("CMDQ: export failed, heap=%u, payload=%s\n",
                          (unsigned)ESP.getFreeHeap(), content.c_str());
            server->send(500, F("text/plain"), F("Could not build the queue export"));
            return;
        }
        server->send(200, F("application/json"), content);
        return;
    }

    server->send(400, F("text/plain"), F("Unbekannte Aktion"));
}
