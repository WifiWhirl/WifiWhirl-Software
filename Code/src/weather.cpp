#include "main.h"

String queryAmbientTemperature()
{
    // Track heap usage for debugging
    Serial.print(F("Weather: Starting query, free heap: "));
    Serial.println(ESP.getFreeHeap());

    // Use standard WiFiClient for HTTP connection
    WiFiClient client;
    HTTPClient http;
    http.setUserAgent(DEVICE_NAME);

    // Extract PLZ from settings
    String _plz;
    {
        // Scope-limited to ensure immediate cleanup
        DynamicJsonDocument doc(1024);
        String json;
        json.reserve(320);
        bwc->getJSONSettings(json);

        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, json);
        json.clear();
        json = String();

        if (error)
        {
            Serial.println(F("Weather: Failed to read config"));
            return "Error reading config";
        }

        _plz = doc[F("PLZ")].as<String>();
        // doc goes out of scope here and is destroyed
    }

    String const weatherURL = String(cloudApi) + "/v1/weather/plz/" + _plz + "/";
    Serial.print(F("Weather: Connecting to API: "));
    Serial.println(weatherURL);

    if (http.begin(client, weatherURL))
    {
        http.addHeader("X-WW-Firmware", FW_VERSION);
        http.addHeader("X-WW-Apikey", String(cloudApiKey));
        http.addHeader("Accept", "application/json");

        int httpResponseCode = http.GET();

        Serial.print(F("Weather: Response code: "));
        Serial.println(httpResponseCode);

        if (httpResponseCode == 200)
        {
            String payload = http.getString();
            http.end();
            client.stop();

            Serial.print(F("Weather: Parsing response, free heap: "));
            Serial.println(ESP.getFreeHeap());

            StaticJsonDocument<512> resp;
            DeserializationError error = deserializeJson(resp, payload);
            payload.clear();
            payload = String();

            if (error)
            {
                Serial.println(F("Weather: Parse error"));
                return "Error while getting weather data";
            }

            ambExpires = resp[F("expires")];
            int64_t _temperature = resp[F("temperature")];
            const char *_name = resp[F("name")];

            // Copy name before clearing document
            String result = String(_name);

            bwc->setAmbientTemperature(_temperature, true);

            Serial.print(F("Weather: Success, free heap: "));
            Serial.println(ESP.getFreeHeap());

            return result;
        }
        else if (httpResponseCode == 404)
        {
            Serial.print(F("Weather: Error code: "));
            Serial.println(httpResponseCode);
            http.end();
            client.stop();

            return "Error: ZIP code not found";
        }
        else
        {
            Serial.print(F("Weather: Error code: "));
            Serial.println(httpResponseCode);
            http.end();
            client.stop();

            return "Error while getting weather data";
        }
    }
    else
    {
        Serial.println(F("Weather: Could not connect to server"));
        http.end();
        client.stop();

        return "Error while getting weather data";
    }
}

void handleGetWeather()
{
    String ambient = queryAmbientTemperature();
    if (ambient.indexOf("Error") >= 0)
    {
        server->send(500, F("text/plain"), "Es ist ein Fehler aufgetreten. Bitte prüfe die PLZ.");
    }
    else
    {
        server->send(200, F("text/plain"), ambient);
    }
}
