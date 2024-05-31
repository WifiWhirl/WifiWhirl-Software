/**
 * response for /hook/
 * web server adds command to cmdq "Webhook"
 * Examples:
 * Pump on: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":true}
 * Pump off: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":false}
 * Heating on: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":true}
 * Heating off: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":false}
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
