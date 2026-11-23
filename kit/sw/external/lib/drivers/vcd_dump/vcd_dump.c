#include <stdbool.h>
#include "gpio.h"
#include "vcd_dump.h"

#define VCD_TRIGGER_GPIO 0

void dump_on(void) {
    gpio_cfg_t cfg = {.pin = VCD_TRIGGER_GPIO, .mode = GpioModeOutPushPull};
    gpio_config(cfg);
    gpio_write(VCD_TRIGGER_GPIO, true);
}

void dump_off(void) {
    gpio_write(VCD_TRIGGER_GPIO, false);
}
