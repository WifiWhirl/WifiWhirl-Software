#define FW_VERSION "2.0.0"

// Active PlatformIO build environment, injected at build time by
// inject_build_info.py. Fallback for builds without that script (e.g. IDE
// indexers).
#ifndef PIO_ENV_NAME
#define PIO_ENV_NAME "unknown"
#endif