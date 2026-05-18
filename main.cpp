
#include "hardware/structs/scb.h"
#include <cstdio>
#include <cstdlib>
#include <hardware/exception.h>
#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <shizu.hpp>
#include <time.h>

void *get_psp() {
  void *psp;
  asm volatile("MRS %0, PSP" : "=r"(psp));
  return psp;
}
void *get_msp() {
  void *msp;
  asm volatile("MRS %0, MSP" : "=r"(msp));
  return msp;
}

void test_svc_handler() {
  printf("test_svc_handler\n");
  //  sleep_ms(100);
}

void hardfault_handler() {

  uint32_t volatile exc_lr;
  asm volatile("mov %[lr], lr" : [lr] "=r"(exc_lr));
  while (1) {
    printf("HardFault_Handler: exc_lr=%lx,psp=%p,msp=%p\n", exc_lr, get_psp(),
           get_msp());
    sleep_ms(500);
  }
}

int main(int argc, char const *argv[]) {
  stdio_init_all();
  exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hardfault_handler);
  sleep_ms(2000);
  shizu::init();
  while (1) {
    sleep_ms(500);
    printf("no_return\n");
  }
  return 0;
}
