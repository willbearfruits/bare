#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Machine "personality" sensors: CPU thermal sensor (MSR), ThinkPad embedded controller (fan, temp, LEDs). */
void sensors_init(void);
bool sensors_is_thinkpad(void);
const char *sensors_machine(void);   /* DMI manufacturer + product, or "" */
int  sensors_cpu_temp(void);         /* °C or -1 */
int  sensors_fan_rpm(void);          /* or -1 */
void sensors_led(int led, int mode); /* ThinkPad EC LED: 0 power, 10 lid logo; mode 0 off 1 on 2 blink */
