//direct port manipulation memory adresses.
#ifdef ESP8266
#define PIN_OUT 0x60000300
#define PIN_OUT_SET 0x60000304
#define PIN_OUT_CLEAR 0x60000308

#define PIN_DIR 0x6000030C
#define PIN_DIR_OUTPUT 0x60000310
#define PIN_DIR_INPUT 0x60000314

#define PIN_IN 0x60000318
#else
// ESP32: same registers, bank 0 only, so the bus pins must be GPIO0..31.
// digitalRead()/digitalWrite() in core 3 are too slow for the ISRs and not IRAM-safe.
#include "soc/gpio_reg.h"
#define PIN_OUT GPIO_OUT_REG
#define PIN_OUT_SET GPIO_OUT_W1TS_REG
#define PIN_OUT_CLEAR GPIO_OUT_W1TC_REG

#define PIN_DIR GPIO_ENABLE_REG
#define PIN_DIR_OUTPUT GPIO_ENABLE_W1TS_REG
#define PIN_DIR_INPUT GPIO_ENABLE_W1TC_REG

#define PIN_IN GPIO_IN_REG
#endif
