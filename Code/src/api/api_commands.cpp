#include "main.h"

command_que_item parseCommandFromJson(const JsonVariantConst &src)
{
    command_que_item item;
    item.cmd = src[F("CMD")];
    item.val = src[F("VALUE")];
    item.xtime = src[F("XTIME")] | (int64_t)std::time(nullptr);
    item.interval = src[F("INTERVAL")] | (int64_t)0;
    item.text = src[F("TXT")] | "";
    item.force = src[F("FORCE")] | false;
    return item;
}

/**
 * HTTP polling endpoint: returns a JSON array of [STATES, TIMES, OTHER]
 * Used as a fallback when WebSocket connections are not available (e.g. iOS 26 Safari Browser).
 * The client polls this endpoint at regular intervals instead of using WebSocket.
 */
void handleGetPollData()
{
    // Build response as JSON array containing all three data objects
    String json;
    json.reserve(1200);
    json = F("[");

    // Append STATES JSON object
    String part;
    part.reserve(320);
    bwc->getJSONStates(part);
    json += part;
    json += F(",");

    // Append TIMES JSON object
    part.clear();
    bwc->getJSONTimes(part);
    json += part;
    json += F(",");

    // Append OTHER JSON object
    part.clear();
    getOtherInfo(part);
    json += part;

    json += F("]");

    // Send the combined JSON array response
    server->send(200, F("application/json"), json);
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
    DeserializationError error = deserializeJson(doc, server->arg("plain"));
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing command"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Warteschlange voll (max. 20 Befehle)"));
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
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Warteschlange voll (max. 20 Befehle)"));
        return;
    }

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
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    uint8_t index = doc[F("IDX")];
    bwc->edit_command(index, item);

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
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    uint8_t index = doc[F("IDX")];
    bwc->del_command(index);

    server->send(200, F("text/plain"), "");
}

void handle_cmdq_file()
{
    if (!checkHttpPost(server->method()))
        return;

    // Check for upload action via query parameter
    String action = server->arg(F("action"));

    if (action.equals("upload"))
    {
        // Upload: body contains raw JSON file content
        String data = server->arg("plain");
        if (data.length() == 0)
        {
            data = server->arg(0);
        }
        if (data.length() == 0)
        {
            server->send(400, F("text/plain"), F("Keine Daten empfangen"));
            return;
        }

        // Validate JSON format - cmdq.json structure:
        // {"LEN":n,"CMD":[...],"VALUE":[...],"XTIME":[...],"INTERVAL":[...],"TXT":[...]}
        size_t validateCapacity = data.length() + JSON_OBJECT_SIZE(6) + (JSON_ARRAY_SIZE(MAXCOMMANDS) * 5) + 512;
        DynamicJsonDocument validateDoc(validateCapacity);
        DeserializationError validateError = deserializeJson(validateDoc, data);
        if (validateError)
        {
            server->send(400, F("text/plain"), F("Ungültiges JSON-Format"));
            return;
        }

        // Check if root is an object
        if (!validateDoc.is<JsonObject>())
        {
            server->send(400, F("text/plain"), F("Datei muss ein JSON-Objekt enthalten"));
            return;
        }

        JsonObject root = validateDoc.as<JsonObject>();

        // Check for required fields
        if (!root.containsKey(F("LEN")) || !root.containsKey(F("CMD")) ||
            !root.containsKey(F("VALUE")) || !root.containsKey(F("XTIME")) ||
            !root.containsKey(F("INTERVAL")))
        {
            server->send(400, F("text/plain"), F("Datei fehlt erforderliche Felder (LEN, CMD, VALUE, XTIME, INTERVAL)"));
            return;
        }

        // Verify arrays have consistent length
        size_t len = root[F("LEN")].as<size_t>();
        if (len > MAXCOMMANDS)
        {
            server->send(400, F("text/plain"), F("Warteschlange enthält zu viele Befehle (max. 20)"));
            return;
        }

        if (!root[F("CMD")].is<JsonArray>() || !root[F("VALUE")].is<JsonArray>() ||
            !root[F("XTIME")].is<JsonArray>() || !root[F("INTERVAL")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("CMD, VALUE, XTIME, INTERVAL müssen Arrays sein"));
            return;
        }

        JsonArray cmdArr = root[F("CMD")].as<JsonArray>();
        JsonArray valArr = root[F("VALUE")].as<JsonArray>();
        JsonArray timeArr = root[F("XTIME")].as<JsonArray>();
        JsonArray intArr = root[F("INTERVAL")].as<JsonArray>();

        if (cmdArr.size() != len || valArr.size() != len ||
            timeArr.size() != len || intArr.size() != len)
        {
            server->send(400, F("text/plain"), F("Array-Längen stimmen nicht mit LEN überein"));
            return;
        }

        if (root.containsKey(F("TXT")) && !root[F("TXT")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("TXT muss ein Array sein"));
            return;
        }

        if (root.containsKey(F("TXT")) && root[F("TXT")].as<JsonArray>().size() != len)
        {
            server->send(400, F("text/plain"), F("TXT-Länge stimmt nicht mit LEN überein"));
            return;
        }

        // Validation passed - save the file
        File file = LittleFS.open(F("/cmdq.json"), "w");
        if (!file)
        {
            Serial.println(F("FS: Failed to open /cmdq.json for write"));
            server->send(500, F("text/plain"), F("Fehler beim Speichern der Datei"));
            return;
        }
        file.print(data);
        file.close();

        // Reload command queue in BWC
        bwc->reloadCommandQueue();

        server->send(200, F("text/plain"), F("OK"));
        return;
    }

    // Download: parse JSON body for action
    String message = server->arg(0);
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Fehler beim Verarbeiten der Anfrage"));
        return;
    }

    action = doc[F("ACT")].as<String>();

    if (action.equals("download"))
    {
        // Send cmdq.json content to client for download
        if (LittleFS.exists(F("/cmdq.json")))
        {
            File file = LittleFS.open(F("/cmdq.json"), "r");
            if (file)
            {
                String content = file.readString();
                file.close();
                server->send(200, F("application/json"), content);
                return;
            }
        }
        // Return empty array if file doesn't exist
        server->send(200, F("application/json"), F("[]"));
        return;
    }

    server->send(400, F("text/plain"), F("Unbekannte Aktion"));
}
