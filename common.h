#ifndef SIK2_COMMON_H
#define SIK2_COMMON_H

#include <cstdint>
#include <sys/timerfd.h>
#define NOT_EATEN false
#define EATEN true
#define TURN_RIGHT 1
#define TURN_LEFT 2

#define TYPE_NEW_GAME 0
#define TYPE_PIXEL 1
#define TYPE_PLAYER_ELIMINATED 2
#define TYPE_GAME_OVER 3

#define BOARD_HEIGHT_MAX 4096
#define BOARD_WIDTH_MAX 4096
#define BOARD_HEIGHT_MIN 16
#define BOARD_WIDTH_MIN 16

#define NAME_LEN_MAX 20
#define MAX_PLAYERS 25
#define MAX_NAME_LEN 20

#define DEFAULT_SERVER_PORT 2021

#define DATAGRAM_MAX_SIZE 30000
// todo ^^ może inne
#define CLIENT_TIMEOUT_SECONDS 2

#define TIMER_ROUND 0
#define TIMER_TIMEOUT 1
#define TIMER_SEND_UPDATE 2
#define UPDATE_NANOSECOND_INTERVAL 30000000

/* syserr i fatal z labów */

/* Wypisuje informację o błędnym zakończeniu funkcji systemowej
i kończy działanie programu. */
extern void syserr(const char *fmt, ...);

/* Wypisuje informację o błędzie i kończy działanie programu. */
extern void fatal(const char *fmt, ...);

void create_timer(int &fd, int timer_type, int rounds_per_sec);

uint32_t crc32buf(const void *buf, size_t size);

struct __attribute__((__packed__)) client_msg {
    uint64_t session_id;
    uint8_t turn_direction;
    uint32_t next_expected_event_no;
    uint8_t player_name[NAME_LEN_MAX];
};

struct __attribute__((__packed__)) event_common {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;
};

struct __attribute__((__packed__)) event_new_game {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;

    uint32_t maxx;
    uint32_t maxy;
    uint8_t list_and_crc[(MAX_NAME_LEN + 1) * MAX_PLAYERS + 4];
};

struct __attribute__((__packed__)) event_pixel {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;

    uint8_t player_number;
    uint32_t x;
    uint32_t y;

    uint32_t crc32;
};

struct __attribute__((__packed__)) event_player_eliminated {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;

    uint8_t player_number;

    uint32_t crc32;
};

struct __attribute__((__packed__)) event_game_over {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;

    uint32_t crc32;
};

#endif //SIK2_COMMON_H
