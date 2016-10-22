#pragma once

#include <ets_sys.h>

int os_printf_plus(const char *format, ...)  __attribute__ ((format (printf, 1, 2)));
int ets_sprintf(const char *dst, const char *format, ...) __attribute__((format(printf, 2, 3)));
void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
void *ets_memcpy(void *dest, const void *src, size_t n);
void *ets_memset(void *s, int c, size_t n);
size_t ets_strlen(const char *s);
void ets_timer_arm_new(ETSTimer *a, int b, int c, int isMstimer);
void ets_timer_disarm(ETSTimer *a);
void ets_timer_setfn(ETSTimer *t, ETSTimerFunc *fn, void *parg);
void ets_delay_us(long us);

