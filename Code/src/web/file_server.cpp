#include "web/web.h"
#include "web_files.h"

/**
 * Map a filename extension to its HTTP content-type
 * @param filename the requested file name
 * @return the MIME type string (defaults to text/plain)
 */
String getContentType(const String &filename)
{
    if (filename.endsWith(".html"))
        return F("text/html");
    else if (filename.endsWith(".css"))
        return F("text/css");
    else if (filename.endsWith(".js"))
        return F("application/javascript");
    else if (filename.endsWith(".ico"))
        return F("image/x-icon");
    else if (filename.endsWith(".png"))
        return F("image/png");
    else if (filename.endsWith(".svg"))
        return F("image/svg+xml");
    else if (filename.endsWith(".eot"))
        return F("application/vnd.ms-fontobject");
    else if (filename.endsWith(".woff"))
        return F("application/font-woff");
    else if (filename.endsWith(".gz"))
        return F("application/x-gzip");
    else if (filename.endsWith(".json"))
        return F("application/json");
    return F("text/plain");
}

/**
 * Serve an embedded file from PROGMEM
 * Handles gzip content-encoding and chunked transfer to avoid watchdog resets
 * @param file Pointer to EmbeddedFile structure
 * @return true if file was served successfully
 */
bool serveEmbeddedFile(const EmbeddedFile *file)
{
    // Read file metadata from PROGMEM
    char contentTypeBuf[48];
    strncpy_P(contentTypeBuf, (PGM_P)pgm_read_ptr(&file->contentType), sizeof(contentTypeBuf) - 1);
    contentTypeBuf[sizeof(contentTypeBuf) - 1] = '\0';

    size_t fileSize = pgm_read_dword(&file->size);
    bool isGzipped = pgm_read_byte(&file->isGzipped);
    const uint8_t *data = (const uint8_t *)pgm_read_ptr(&file->data);

    // Set cache header for static assets
    server->sendHeader(F("Cache-Control"), F("max-age=3600"));

    // Set gzip content-encoding if file is compressed
    if (isGzipped)
    {
        server->sendHeader(F("Content-Encoding"), F("gzip"));
    }

    // Send response with chunked transfer to avoid memory issues
    // For large files, we send in chunks to feed watchdog
    const size_t CHUNK_SIZE = 1024;

    server->setContentLength(fileSize);
    server->send(200, (const char *)contentTypeBuf, (const char *)"");

    size_t bytesSent = 0;
    while (bytesSent < fileSize)
    {
        size_t chunkLen = min(CHUNK_SIZE, fileSize - bytesSent);

        // Copy chunk from PROGMEM to RAM buffer
        uint8_t buffer[CHUNK_SIZE];
        memcpy_P(buffer, data + bytesSent, chunkLen);

        server->sendContent_P((const char *)buffer, chunkLen);
        bytesSent += chunkLen;

        // Feed watchdog during large transfers
        yield();
    }

    Serial.print(F("HTTP > embedded file sent: "));
    Serial.print(fileSize);
    Serial.println(F(" bytes"));

    return true;
}

/**
 * send the right file to the client (if it exists)
 * First checks embedded files in PROGMEM, then falls back to LittleFS
 */
bool handleFileRead(String path)
{
    // Serial.println("HTTP > request: " + path);
    // If a folder is requested, send the index file
    if (path.endsWith("/"))
    {
        path += F("index.html");
    }
    // deny reading credentials
    if (path.equalsIgnoreCase("/mqtt.json") || path.equalsIgnoreCase("/wifi.json") ||
        path.equalsIgnoreCase("/device.json") || path.equalsIgnoreCase("/devuser.json"))
    {
        server->send(403, F("text/plain"), F("Permission denied."));
        // Serial.println(F("HTTP > file reading denied (credentials)."));
        return false;
    }

    // First, check if file is embedded in firmware (PROGMEM)
    const EmbeddedFile *embeddedFile = findEmbeddedFile(path);
    if (embeddedFile != nullptr)
    {
        return serveEmbeddedFile(embeddedFile);
    }

    // Fall back to LittleFS for config files and user uploads
    String contentType = getContentType(path); // Get the MIME type
    String pathWithGz = path + ".gz";
    if (LittleFS.exists(pathWithGz) || LittleFS.exists(path))
    {                                         // If the file exists, either as a compressed archive, or normal
        if (LittleFS.exists(pathWithGz))      // If there's a compressed version available
            path += ".gz";                    // Use the compressed version
        File file = LittleFS.open(path, "r"); // Open the file
        size_t fsize = file.size();

        // send cache header for static files
        if (path.endsWith(".css.gz") || path.endsWith(".css") || path.endsWith(".png.gz") || path.endsWith(".ico.gz") || path.endsWith(".js.gz") || path.endsWith(".eot.gz") || path.endsWith(".woff.gz") || path.endsWith(".html.gz"))
            server->sendHeader(F("Cache-Control"), F("max-age=3600"));

        size_t sent = server->streamFile(file, contentType); // Send it to the client

        file.close(); // Close the file again
        Serial.println(F("File size: ") + String(fsize));
        Serial.println(F("HTTP > LittleFS file sent: ") + path + F(" (") + sent + F(" bytes)"));
        return true;
    }
    // Serial.println("HTTP > file not found: " + path);   // If the file doesn't exist, return false
    return false;
}
