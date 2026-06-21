#include "web/web.h"
#include "api/api.h"

/**
 * Send status data to web client in JSON format (because it is easy to decode on the other side)
 */
void sendWS()
{
    if (webSocket->connectedClients() == 0)
        return;
    // HeapSelectIram ephemeral;
    // Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
    // send states
    String json;
    json.reserve(320);

    bwc->getJSONStates(json);
    webSocket->broadcastTXT(json);
    // send times
    json.clear();
    bwc->getJSONTimes(json);
    webSocket->broadcastTXT(json);
    // send other info
    json.clear();
    getOtherInfo(json);
    webSocket->broadcastTXT(json);
    // send smart schedule
    json.clear();
    bwc->getJSONSmartSchedule(json);
    webSocket->broadcastTXT(json);
    // json = bwc->getDebugData();
    // webSocket->broadcastTXT(json);
    // time_t now = time(nullptr);
    // struct tm timeinfo;
    // gmtime_r(&now, &timeinfo);
    // Serial.print("Current time: ");
    // Serial.print(asctime(&timeinfo));
}

/**
 * Build the "OTHER" status JSON (MQTT state, model, weather, WiFi info, FW, loop rate)
 * Resets the loop counter after reading it
 * @param rtn output string receiving the serialized JSON
 */
void getOtherInfo(String &rtn)
{
    StaticJsonDocument<512> doc;
    // Set the values in the document
    doc[F("CONTENT")] = F("OTHER");
    doc[F("MQTT")] = mqttClient ? mqttClient->state() : -1;
    /*TODO: add these:*/
    //   doc[F("PressedButton")] = bwc->getPressedButton();
    doc[F("HASJETS")] = bwc->hasjets;
    doc[F("HASGOD")] = bwc->hasgod;
    doc[F("MODEL")] = bwc->getModel();
    doc[F("WEATHER")] = bwc->getWeather();
    doc[F("RSSI")] = WiFi.RSSI();
    doc[F("IP")] = WiFi.localIP().toString();
    doc[F("SSID")] = WiFi.SSID();
    doc[F("FW")] = FW_VERSION;
    doc[F("loopfq")] = bwc->loop_count;
    bwc->loop_count = 0;

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize other\"}");
    }
}

/**
 * (Re)create and start the WebSocket server on port 81
 * Enables heartbeat and registers the event handler
 */
void startWebSocket()
{
    HeapSelectIram ephemeral;
    Serial.printf("WS IRamheap %d\n", ESP.getFreeHeap());

    if (webSocket)
    {
        webSocket->close();
        delete webSocket;
    }
    webSocket = new WebSocketsServer(81);
    // When global auth is on, reject handshakes without a valid session cookie.
    // Browsers send the same-origin cookie automatically in the WS upgrade.
    if (globalAuthEnabled)
    {
        static const char *wsMandatory[] = {"Cookie"};
        webSocket->onValidateHttpHeader(wsCookieValidator, wsMandatory, 1);
    }
    webSocket->begin();
    webSocket->enableHeartbeat(3000, 3000, 1);
    webSocket->onEvent(webSocketEvent);
    // Serial.println(F("WebSocket > server started"));
}

/**
 * handle web socket events
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
    // When a WebSocket message is received
    switch (type)
    {
    // if the websocket is disconnected
    case WStype_DISCONNECTED:
        // Serial.printf("WebSocket > [%u] Disconnected!\r\n", num);
        break;

    // if a new websocket connection is established
    case WStype_CONNECTED:
    {
        // IPAddress ip = webSocket->remoteIP(num);
        // Serial.printf("WebSocket > [%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        sendWS();
    }
    break;

    // if new text data is received
    case WStype_TEXT:
    {
        // Serial.printf("WebSocket > [%u] get Text: %s\r\n", num, payload);
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println(F("WebSocket > JSON command failed"));
            return;
        }

        command_que_item item = parseCommandFromJson(doc);
        if (!bwc->add_command(item))
        {
            webSocket->sendTXT(num, "{\"ERR\":\"Warteschlange voll (max. 20 Befehle)\"}");
        }
    }
    break;

    default:
        break;
    }
}
