#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <string>
#include <fcntl.h>
#include <cstdarg>
#include <getopt.h>
#include <cstring>
#include <map>
#include <algorithm>
#include <cmath>
#include <poll.h>
#include <variant>

#define MAX_PLAYERS 25
#define DEFAULT_PORT 2021
#define DEFAULT_TURNING_SPEED 6
#define DEFAULT_ROUNDS_PER_SEC 50
#define DEFAULT_BOARD_WIDTH 640
#define DEFAULT_BOARD_HEIGHT 480
#define NOT_EATEN false
#define EATEN true
#define TURN_RIGHT 1
#define TURN_LEFT 2
#define NAME_LEN_MAX 20
#define CLIENT_MAX (2 + 2 * MAX_PLAYERS)


namespace {
    int64_t my_rand;
    int port, turning_speed, rounds_per_sec, board_width, board_height;

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

    /* funkcja fatal z laboratoriów */
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

    /* https://stackoverflow.com/questions/31502120/sin-and-cos-give-unexpected-results-for-well-known-angles */
    inline double degree_to_radian(double d) {
        return (d / 180.0) * ((double) M_PI);
    }

    int32_t get_random() {
        int32_t result = (int32_t)my_rand;
        my_rand = (my_rand * 279410273) % 4294967291;
        return result;
    }

    struct worm_info {
        double x;
        double y;
        int16_t direction;
    };

    struct __attribute__((__packed__)) client_msg {
        uint64_t session_id;
        uint8_t number;
        uint32_t next_expected_event_no;
        uint8_t player_name[NAME_LEN_MAX];
    };

    struct __attribute__((__packed__)) event_newgame {
        uint32_t len;
        uint32_t event_no;
        uint8_t event_type;

        uint32_t maxx;
        uint32_t maxy;
        uint8_t player_list[(NAME_LEN_MAX + 1) * MAX_PLAYERS];

        uint32_t crc32;
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

    struct __attribute__((__packed__)) event_gameover {
        uint32_t len;
        uint32_t event_no;
        uint8_t event_type;

        uint32_t crc32;
    };

    void get_args(int argc, char *argv[]) {
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
                    fatal("Usage: %s [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n");
            }
        }
    }

    void init_game(uint32_t &game_id, std::vector<std::string> &players,
                   std::map<std::string, worm_info> &player_worms,
                   std::vector<bool> &board) {
        game_id = get_random();
        for (int i = 0; i < board.size(); i++)
            board[i] = NOT_EATEN;

        // todo NEW_GAME wysyłamy
        std::sort(players.begin(), players.end()); // porządkujemy alfabetycznie

        for (std::string &player : players) {
            double x = (player_worms[player].x = get_random() + 0.5);
            double y = (player_worms[player].y = get_random() + 0.5);
            player_worms[player].direction = get_random() % 360;
            if (board[y * board_width + x] == EATEN || y > (board_height - 1) || x > (board_width - 1)) {
                // todo PLAYER_ELIMINATED
            }
            else {
                board[y * board_width + x] = EATEN;
                // todo PIXEL
            }
        }
    }

    void do_turn(uint32_t &game_id, std::vector<std::string> &players,
                 std::map<std::string, worm_info> &player_worms,
                 std::map<std::string, uint8_t> &turn_direction,
                 std::vector<bool> &board) {

        for (std::string &player : players) {
            if (turn_direction[player] == TURN_RIGHT)
                player_worms[player].direction += turning_speed;
            else if (turn_direction[player] == TURN_LEFT)
                player_worms[player].direction -= turning_speed;

            double old_x = player_worms[player].x;
            double old_y = player_worms[player].y;

            double direction = degree_to_radian(player_worms[player].direction);
            double x = (player_worms[player].x += std::cos(direction));
            double y = (player_worms[player].y += std::sin(direction));

            if (x == old_x && y == old_y)
                continue;

            if (board[y * board_width + x] == EATEN || y > (board_height - 1) || x > (board_width - 1)) {
                // todo PLAYER_ELIMINATED
            }
            else {
                board[y * board_width + x] = EATEN;
                // todo PIXEL
            }
        }
    }

}



// czy jest jakiś ładny sposób na oddzielenie player_name jednego komunikatu od session_id kolejnego? Albo raczej przeczytanie dokładnie jednego datagramu, bo te mogę mieć różne długości
int main(int argc, char *argv[]) {
    //std::cout << std::sin(degree_to_radian(0)) << " " << std::cos(degree_to_radian(0)) << std::endl;
    //std::cout << std::sin(degree_to_radian(90)) << " " << std::cos(degree_to_radian(90)) << std::endl;

    port = DEFAULT_PORT;
    turning_speed = DEFAULT_TURNING_SPEED;
    rounds_per_sec = DEFAULT_ROUNDS_PER_SEC;
    board_width = DEFAULT_BOARD_WIDTH;
    board_height = DEFAULT_BOARD_HEIGHT;
    my_rand = time(NULL);

    get_args(argc, argv);
    
    uint32_t game_id;
    std::vector<std::string> players;
    std::map<std::string, worm_info> player_worms;
    std::map<std::string, uint8_t> turn_direction;
    std::vector<bool> board;
    board.resize(board_width * board_height);
    std::vector<std::variant<event_newgame, event_pixel, event_player_eliminated, event_gameover>> game_events;

    /*
     * client[0] na obsługę nowych połączeń
     * client[1] na timer rundy
     * client[2k] na gracza, do odbierania jego komunikatów
     * client[2k+1] na timer gracza 2k, do kontroli czy nie przekroczył 2 sekund
     * maksymalnie 2 + 2 * MAX_PLAYERS klientów
    */

    pollfd client[CLIENT_MAX];
    sockaddr_in server;
    int ready_players = 0;
    for (int i = 0; i < CLIENT_MAX; i++) {
        client[i].fd = -1;
        client[i].events = POLLIN;
        client[i].revents = 0;
    }



    //init_game(game_id, players, player_worms, board);


    return 123;
};