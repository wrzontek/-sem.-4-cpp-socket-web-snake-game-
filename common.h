#ifndef SIK2_COMMON_H
#define SIK2_COMMON_H

#include <cstdint>
#define NOT_EATEN false
#define EATEN true
#define TURN_RIGHT 1
#define TURN_LEFT 2

#define TYPE_NEW_GAME 0
#define TYPE_PIXEL 1
#define TYPE_PLAYER_ELIMINATED 2
#define TYPE_GAME_OVER 3

#define NAME_LEN_MAX 20
#define MAX_PLAYERS 25
#define MAX_NAME_LEN 20

#define DEFAULT_SERVER_PORT 2021

/* syserr i fatal z labów */

/* Wypisuje informację o błędnym zakończeniu funkcji systemowej
i kończy działanie programu. */
extern void syserr(const char *fmt, ...);

/* Wypisuje informację o błędzie i kończy działanie programu. */
extern void fatal(const char *fmt, ...);

struct __attribute__((__packed__)) client_msg {
    uint64_t session_id;
    uint8_t turn_direction;
    uint32_t next_expected_event_no;
    uint8_t player_name[NAME_LEN_MAX];
};

struct __attribute__((__packed__)) event_new_game {
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;

    uint32_t maxx;
    uint32_t maxy;
    uint8_t list_and_crc[21 * MAX_PLAYERS + 4];
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