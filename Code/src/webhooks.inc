/**
 * response for /hook/
 * web server adds command to cmdq "Webhook"
 * Examples:
 * Pump on: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":true}
 * Pump off: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":false}
 * Heating on: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":true}
 * Heating off: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":false}
 * 
 * See https://wifiwhirl.de/Integrationen/Webhooks/ for details
 */

void handleWebhook()
{
    if (!checkHttpGet(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = server->arg("send");
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    Commands command = doc[F("CMD")];
    int64_t value = doc[F("VALUE")];
    int64_t xtime = doc[F("XTIME")] | std::time(0);
    int64_t interval = doc[F("INTERVAL")] | 0;
    String txt = doc[F("TXT")] | "";
    command_que_item item;
    item.cmd = command;
    item.val = value;
    item.xtime = xtime;
    item.interval = interval;
    item.text = txt;
    bwc->add_command(item);

    server->send(200, F("text/plain"), String(item.cmd) + " " + String(item.val) + " " + String(item.xtime));
}

/**
 * response for /getstates/
 * Returns a JSON document with the current ON/OFF states of main components
 * Example response:
 * {"pump":true,"heater":false,"bubbles":false,"jets":true,"power":true,"lock":false}
 */

void handleGetStates()
{
    if (!checkHttpGet(server->method()))
        return;

    StaticJsonDocument<256> doc;

    // Map internal cio states to human-readable keys
    doc[F("pump")] = static_cast<bool>(bwc->cio->cio_states.pump);
    doc[F("heater")] = static_cast<bool>(bwc->cio->cio_states.heatred || bwc->cio->cio_states.heatgrn);
    doc[F("bubbles")] = static_cast<bool>(bwc->cio->cio_states.bubbles);
    doc[F("jets")] = static_cast<bool>(bwc->cio->cio_states.jets);
    doc[F("power")] = static_cast<bool>(bwc->cio->cio_states.power);
    doc[F("lock")] = static_cast<bool>(bwc->cio->cio_states.locked);

    String json;
    serializeJson(doc, json);

    server->send(200, F("application/json"), json);
}
