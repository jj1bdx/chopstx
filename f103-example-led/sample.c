#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <chopstx.h>
#include "sys.h" /* for set_led */

static chopstx_mutex_t mtx;
static chopstx_cond_t cnd0;
static chopstx_cond_t cnd1;

chopstx_mutex_t usb_mtx;
chopstx_cond_t cnd_usb;

static uint8_t u, v;
static uint8_t m;		/* 0..100 */

static void *
pwm (void *arg)
{
  (void)arg;

  chopstx_mutex_lock (&mtx);
  chopstx_cond_wait (&cnd0, &mtx);
  chopstx_mutex_unlock (&mtx);

  while (1)
    {
      set_led (u&v);
      chopstx_usec_wait (m);
      set_led (0);
      chopstx_usec_wait (100-m);
    }

  return NULL;
}

static void *
blk (void *arg)
{
  (void)arg;

  chopstx_mutex_lock (&mtx);
  chopstx_cond_wait (&cnd1, &mtx);
  chopstx_mutex_unlock (&mtx);

  while (1)
    {
      v = 0;
      chopstx_usec_wait (200*1000);
      v = 1;
      chopstx_usec_wait (200*1000);
    }

  return NULL;
}

#define PRIO_PWM 3
#define PRIO_BLK 2

extern uint8_t __process1_stack_base__, __process1_stack_size__;
extern uint8_t __process2_stack_base__, __process2_stack_size__;

const uint32_t __stackaddr_pwm = (uint32_t)&__process1_stack_base__;
const size_t __stacksize_pwm = (size_t)&__process1_stack_size__;

const uint32_t __stackaddr_blk = (uint32_t)&__process2_stack_base__;
const size_t __stacksize_blk = (size_t)&__process2_stack_size__;

int
main (int argc, const char *argv[])
{
  (void)argc;
  (void)argv;

  chopstx_mutex_init (&mtx);
  chopstx_cond_init (&cnd0);
  chopstx_cond_init (&cnd1);

  chopstx_mutex_init (&usb_mtx);
  chopstx_cond_init (&cnd_usb);

  u = 0;
  m = 50;

  chopstx_create (PRIO_PWM, __stackaddr_pwm, __stacksize_pwm, pwm, NULL);
  chopstx_create (PRIO_BLK, __stackaddr_blk, __stacksize_blk, blk, NULL);

  chopstx_usec_wait (200*1000);

  chopstx_mutex_lock (&mtx);
  chopstx_cond_signal (&cnd0);
  chopstx_cond_signal (&cnd1);
  chopstx_mutex_unlock (&mtx);

  while (1) {
      u ^= 1;
	  chopstx_usec_wait (200*1000*6);
  }

  return 0;
}
