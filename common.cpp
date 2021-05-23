#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include "common.h"

void syserr(const char *fmt, ...)
{
    va_list fmt_args;
    int err = errno;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end (fmt_args);
    fprintf(stderr," (%d; %s)\n", err, strerror(err));
    exit(EXIT_FAILURE);
}

void fatal(const char *fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end (fmt_args);

    fprintf(stderr,"\n");
    exit(EXIT_FAILURE);
}

void create_timer(int &fd, int timer_type, int rounds_per_sec) {
    itimerspec new_value{};
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
        syserr("clock_gettime");

    if (timer_type == TIMER_ROUND) {
        new_value.it_value.tv_sec = now.tv_sec;
        new_value.it_value.tv_nsec = now.tv_nsec;
        new_value.it_interval.tv_sec = 1;
        new_value.it_interval.tv_nsec = 1000000000 / rounds_per_sec;
    } else if (timer_type == TIMER_TIMEOUT) {
        new_value.it_value.tv_sec = now.tv_sec + CLIENT_TIMEOUT_SECONDS;
        new_value.it_value.tv_nsec = now.tv_nsec;
        new_value.it_interval.tv_sec = CLIENT_TIMEOUT_SECONDS;
        new_value.it_interval.tv_nsec = 0;
    } else { // TIMER_SEND_TIMEOUT
        new_value.it_value.tv_sec = now.tv_sec;
        new_value.it_value.tv_nsec = now.tv_nsec + UPDATE_NANOSECOND_INTERVAL;
        new_value.it_interval.tv_sec = 1; // TODO 0
        new_value.it_interval.tv_nsec = UPDATE_NANOSECOND_INTERVAL;
    }

    fd = timerfd_create(CLOCK_REALTIME, 0);
    if (fd == -1)
        syserr("timerfd_create");

    if (timerfd_settime(fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)
        syserr("timerfd_settime create");
}