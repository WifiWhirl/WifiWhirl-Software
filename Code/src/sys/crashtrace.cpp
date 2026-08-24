#include <Arduino.h>
#include "sys/sys.h"

#ifdef ESP8266
extern "C"
{
#include "user_interface.h" // struct rst_info
}
#else
#include "esp_attr.h" // RTC_NOINIT_ATTR, IRAM_ATTR (arduino_panic_info_t comes via Arduino.h)
#endif

// Capture the call stack on a crash (exception OR soft watchdog) into RTC user
// memory, which survives a reset and is safe to touch from the crash handler
// (no heap, no flash, no Serial). Read it back on the next boot and expose it
// via /support so a full backtrace can be decoded with addr2line - the leaf PC
// (epc1) alone isn't enough to find the blocking caller.

namespace
{
    constexpr uint32_t TRACE_MAGIC = 0xBCAA2701;
    constexpr uint32_t RTC_OFFSET = 64; // 4-byte-block offset into RTC user mem (512B total)

#ifdef ESP8266
    // Anything in the ESP8266 code address space is a plausible return address.
    inline bool looksLikeCode(uint32_t v)
    {
        return (v >= 0x40100000 && v < 0x40300000);
    }
#endif

    struct CrashTrace
    {
        uint32_t magic;
        uint32_t reason;   // rst_info->reason (3 = soft wdt, 2 = exception)
        uint32_t exccause;
        uint32_t epc1;
        uint32_t excvaddr;
        uint32_t count;    // number of stack addresses captured
        uint32_t addr[26]; // captured return addresses, outermost-first as found
    };
    static_assert(sizeof(CrashTrace) <= 512 - RTC_OFFSET * 4, "CrashTrace exceeds RTC user memory");

    CrashTrace g_trace; // loaded once at boot
    bool g_valid = false;
}

#ifdef ESP8266

// Overrides the weak symbol in the ESP8266 core postmortem handler.
extern "C" void custom_crash_callback(struct rst_info *rst_info, uint32_t stack, uint32_t stack_end)
{
    CrashTrace t;
    t.magic = TRACE_MAGIC;
    t.reason = rst_info->reason;
    t.exccause = rst_info->exccause;
    t.epc1 = rst_info->epc1;
    t.excvaddr = rst_info->excvaddr;
    t.count = 0;
    for (uint32_t a = stack; a < stack_end && t.count < 26; a += 4)
    {
        uint32_t v = *(uint32_t *)a;
        if (looksLikeCode(v))
            t.addr[t.count++] = v;
    }
    ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t *)&t, sizeof(t));
}

// Call once early in setup(): pull the last crash trace out of RTC memory.
void crashtrace_load()
{
    CrashTrace t;
    if (ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t *)&t, sizeof(t)) && t.magic == TRACE_MAGIC)
    {
        g_trace = t;
        g_valid = true;
        // One-shot: clear the magic in RTC so later clean reboots don't keep
        // re-reporting this same crash (matches the ESP32 path above).
        t.magic = 0;
        ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t *)&t, sizeof(t));
    }
}

#else // ESP32

// Stock arduino-esp32 ships with the IDF core dump disabled, but its core
// already installs a panic wrapper that captures the crashing backtrace and
// forwards it to a registered callback. We register one to copy that backtrace
// into RTC_NOINIT memory (which survives the reset) and read it back on the
// next boot for /support - keeping the same CrashTrace/JSON shape as ESP8266.
//
// The callback runs in panic context, so it stays IRAM-safe (no flash, heap or
// Serial). reason is hard-set to "exception"; esp_reset_reason() at
// boot in diagnostics already reports the precise wdt/panic/brownout cause.

RTC_NOINIT_ATTR static CrashTrace s_rtcTrace;

static void IRAM_ATTR panicHandler(arduino_panic_info_t *info, void *arg)
{
    s_rtcTrace.magic = TRACE_MAGIC;
    s_rtcTrace.reason = 2; // matches the ESP8266 "exception" reason code
    s_rtcTrace.exccause = 0;
    s_rtcTrace.epc1 = (uint32_t)info->pc;
    s_rtcTrace.excvaddr = 0;
    uint32_t n = info->backtrace_len;
    if (n > 26)
        n = 26;
    for (uint32_t i = 0; i < n; i++)
        s_rtcTrace.addr[i] = info->backtrace[i];
    s_rtcTrace.count = n;
}

// Call once early in setup(): register the panic hook and pull any saved trace.
void crashtrace_load()
{
    set_arduino_panic_handler(panicHandler, nullptr);
    if (s_rtcTrace.magic == TRACE_MAGIC)
    {
        g_trace = s_rtcTrace;
        g_valid = true;
        s_rtcTrace.magic = 0; // one-shot: don't re-report on the next clean boot
    }
}

#endif

// Append a "crashTrace" JSON member (no leading comma) for /support.
void crashtrace_appendJson(String &json)
{
    json += F("\"crashTrace\":");
    if (!g_valid)
    {
        json += F("null");
        return;
    }
    json += F("{\"reason\":");
    json += String(g_trace.reason);
    json += F(",\"exccause\":");
    json += String(g_trace.exccause);
    json += F(",\"epc1\":\"0x");
    json += String(g_trace.epc1, HEX);
    json += F("\",\"excvaddr\":\"0x");
    json += String(g_trace.excvaddr, HEX);
    json += F("\",\"stack\":[");
    // Clamp to the array size: count comes from RTC memory, which on a corrupt or
    // uninitialized read (magic coincidentally matching) can be garbage > 26.
    uint32_t n = g_trace.count > 26 ? 26 : g_trace.count;
    for (uint32_t i = 0; i < n; i++)
    {
        if (i)
            json += ',';
        json += F("\"0x");
        json += String(g_trace.addr[i], HEX);
        json += F("\"");
    }
    json += F("]}");
}
