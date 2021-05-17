#include <iostream>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <regex>
#include <set>
#include <fcntl.h>
#include <cstdarg>
#include <getopt.h>
#include "err.h"

#define DEFAULT_PORT 2021
#define DEFAULT_TURNING_SPEED 6
#define DEFAULT_ROUNDS_PER_SEC 50
#define DEFAULT_BOARD_WIDTH 640
#define DEFAULT_BOARD_HEIGHT 480

int64_t my_rand;
namespace {
    /* funkcja syserr z laboratoriów */
    void syserr(const char *fmt, ...) {
        va_list fmt_args;
        int err;

        fprintf(stderr, "ERROR: ");
        err = errno;

        va_start(fmt_args, fmt);
        if (vfprintf(stderr, fmt, fmt_args) < 0) {
            fprintf(stderr, " (also error in syserr) ");
        }
        va_end(fmt_args);
        fprintf(stderr, " (%d; %s)\n", err, strerror(err));
        exit(EXIT_FAILURE);
    }

    int64_t get_random() {
        my_rand = (my_rand * 279410273) % 4294967291;
        return my_rand;
    }


}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int turning_speed = DEFAULT_TURNING_SPEED;
    int rounds_per_sec = DEFAULT_ROUNDS_PER_SEC;
    int board_width = DEFAULT_BOARD_WIDTH;
    int board_height = DEFAULT_BOARD_HEIGHT;
    my_rand = time(NULL);

    int opt;
    while ((opt = getopt(argc, argv, "p:s:t:v:w:h:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                my_rand = atoi(optarg);
                break;
            case 't':
                turning_speed = atoi(optarg);
                break;
            case 'v':
                rounds_per_sec = atoi(optarg);
                break;
            case 'w':
                board_width = atoi(optarg);
                break;
            case 'h':
                board_height = atoi(optarg);
                break;
            default:
                syserr("Usage: %s [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n");
        }
    }


    return 123;
}