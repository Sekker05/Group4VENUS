#include <libpynq.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#define R2 9.7 *1000     // Fixed resistor value, 10k ohms
#define R0 8* 1000     // Thermistor resistance at 25°C
#define T0 273.15 + 25   // 25°C in Kelvin
#define BETA 4050   // Beta value from thermistor datasheet

double r_to_t(double r_t) {

  double temperature_kelvin;
  double temperature_celsius;

  temperature_kelvin = (1 / T0) + 1/(log(r_t / R0) / BETA);

  temperature_celsius = temperature_kelvin - 273.15;

  return temperature_celsius;
}


int main(void) {

  pynq_init();
  adc_init();
  buttons_init();

  double v_out, v_ref;
  double r_t;
  double temperature;

  while(!get_button_state(BUTTON0)) {
    v_out = adc_read_channel(ADC0);
    v_ref = adc_read_channel(ADC1);
    r_t = (v_ref - v_out) * R2 / v_out;

    temperature = r_to_t(r_t);

    printf("V_out: %f, v_ref: %f, r_t: %f, temperature: %f C\n", v_out, v_ref, r_t, temperature);
   
    sleep_msec(1000);
  }

  adc_destroy();
  buttons_destroy();
  pynq_destroy();

  return 0;
}