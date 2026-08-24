#pragma once

#include "main.h"

// --- Diagnostics (diagnostics.cpp) ---
void handleSupportPackage();

// --- Crash trace (crashtrace.cpp) ---
void crashtrace_load();            // read last crash from RTC; call once in setup()
void crashtrace_appendJson(String &json); // append "crashTrace":{...} for /support

// --- Prometheus (prometheus.cpp) ---
void handlePrometheusMetrics();
