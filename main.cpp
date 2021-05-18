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
#include <sys/timerfd.h>
#include <set>


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
#define CLIENT_MAX (2 +  MAX_PLAYERS)


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

    struct

    struct worm_info {
        double x;
        double y;
        int16_t direction;
    };

    struct __attribute__((__packed__)) client_msg {
        uint64_t session_id;
        uint8_t turn_direction;
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

    void init_game(uint32_t &game_id, std::set<std::string> &players,
                   std::map<std::string, worm_info> &player_worms,
                   std::vector<bool> &board) {
        game_id = get_random();
        for (int i = 0; i < board.size(); i++)
            board[i] = NOT_EATEN;

        // todo NEW_GAME wysłać wszystkim

        for (const std::string& player : players) {
            double x = (player_worms[player].x = get_random() + 0.5);
            double y = (player_worms[player].y = get_random() + 0.5);
            player_worms[player].direction = get_random() % 360;
            if (board[y * board_width + x] == EATEN || y > (board_height - 1) || x > (board_width - 1)) {
                // todo PLAYER_ELIMINATED wysłać wszystkim
            }
            else {
                board[y * board_width + x] = EATEN;
                // todo PIXEL wysłać wszystkim
            }
        }
    }

    void do_turn(uint32_t &game_id, std::set<std::string> &players,
                 std::map<std::string, worm_info> &player_worms,
                 std::map<std::string, uint8_t> &turn_direction,
                 std::vector<bool> &board) {

        for (const std::string& player : players) {
            if (player_worms.find(player) != player_worms.end()) {
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
                    // todo PLAYER_ELIMINATED wysłać wszystkim
                } else {
                    board[y * board_width + x] = EATEN;
                    // todo PIXEL wysłać wszystkim
                }
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
    std::set<std::string> player_names;
    std::set<std::string> ready_players = 0;
    std::set<uint64_t> observer_ids;
    std::set<uint64_t> player_ids;
    std::map<std::string, worm_info> player_worms;
    std::map<std::string, uint8_t> turn_direction;
    std::vector<bool> board;
    board.resize(board_width * board_height);
    std::vector<std::variant<event_newgame, event_pixel, event_player_eliminated, event_gameover>> game_events;

    /*
     * client[0] na obsługę komunikatów
     * client[1] na timer rundy
     * pozostałe na timery graczy (do zrywania połączeń przy braku komunikacji przez 2s)
    */

    pollfd client[CLIENT_MAX];
    sockaddr_in6 serveraddr{};
    int msgsock;

    for (int i = 0; i < CLIENT_MAX; i++) {
        client[i].fd = -1;
        client[i].events = POLLIN;
        client[i].revents = 0;
    }

    itimerspec new_value{};
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
        syserr("clock_gettime");


    new_value.it_value.tv_sec = now.tv_sec;
    new_value.it_value.tv_nsec = now.tv_nsec;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 1000000000 / rounds_per_sec;

    client[1].fd = timerfd_create(CLOCK_REALTIME, 0);
    if (client[1].fd == -1)
        syserr("timerfd_create");

    if (timerfd_settime(client[1].fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)
        syserr("timerfd_settime");

    client[0].fd = socket(PF_INET6, SOCK_DGRAM, 0);
    if (client[0].fd == -1)
        syserr("socket");

    int on = 1;
    if (setsockopt(client[0].fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &on, sizeof(on)) < 0)
        syserr("setsockopt");

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(port);
    serveraddr.sin6_addr = in6addr_any;

    if (bind(client[0].fd, (struct sockaddr*)&serveraddr,
             (socklen_t)sizeof(serveraddr)) == -1)
        syserr("bind serveraddr");

    //if (listen(client[0].fd, 10) == -1)
    //    syserr("listen");
    char in_buf[sizeof(client_msg)];
    client_msg in_msg{};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) { // pracujemy aż coś się mocno nie zepsuje
        for (int i = 0; i < CLIENT_MAX; i++) {
            client[i].revents = 0;
        }

        int ret = poll(client, CLIENT_MAX, 2000); // jeżeli przez 2 sekundy nic do nas nie przyjdzie to trzeba wywalić graczy

        if (ret == -1) {
            syserr("poll");
        }
        else if (ret == 0) {
            // wywalamy graczy
        }
        else {
            if (client[0].revents & POLLIN) {
                /* Komunikat od klienta */
                std::cout << "Komunikat od klienta\n";
                // przetwarzamy wszystkie datagramy co doszły
                // jak niepoprawny komunikat to ignore
                // jak nieznany gracz to akceptujemy, udpejt struktur danych
                // jak znany gracz i mniejsze session_id to ignore
                // jak znany gracz i >= session_id to git, updejt struktur danych
                while (true) {
                    // TODO ŻEBY NIE BLOKOWAŁO
                    ret = recvfrom(client[0].fd, ((char *)&in_msg), sizeof(client_msg), 0, NULL, NULL);
                    if (ret < 0)
                        continue;   // jeżeli error to trudno, żyje się dalej
                    if (ret == 0)
                        break;
                    bool invalid_msg = false;
                    in_msg.session_id = be64toh(in_msg.session_id);
                    in_msg.next_expected_event_no = ntohl(in_msg.next_expected_event_no);

                    std::string name;
                    for (int i = 0; i < ret - 13; i++) {
                        char c = in_msg.player_name[i];
                        if (c < 33 || c > 126) {
                            invalid_msg = true;
                            break;
                        }
                        name += in_msg.player_name[i];
                    }
                    if (invalid_msg)
                        continue;

                    if (name.empty()) {
                        /* obserwator */
                        if (observer_ids.find(in_msg.session_id) == observer_ids.end()) {
                            /* nowy obserwator */
                            observer_ids.insert(in_msg.session_id);
                            // todo wysyłamy historię eventów mu, tworzymy timer itd
                        }
                        else {
                            /* stary obserwator */
                            // todo updejt timera komunikacji
                        }
                    }

                    if (player_names.find(name) == player_names.end()) {
                        /* nieznany gracz */
                        if (player_ids.find(in_msg.session_id) != player_ids.end())
                            continue;   // ignorujemy, znana sesja nie może ot tak zmienić nazwy gracza

                        // todo dodajemy gracza
                        player_ids.insert(in_msg.session_id);
                        player_names.insert(name);
                    }
                }
            }

            if (client[1].revents & POLLIN) {
                /* Czas przeliczyć turę */
                std::cout << "TURA\n";

                uint64_t exp;
                read(client[1].fd, &exp, sizeof(uint64_t));

                do_turn(game_id, player_names, player_worms, turn_direction, board);
            }

            // todo iteracja po timerach graczy
        }
    }
#pragma clang diagnostic pop




    //init_game(game_id, player_names, player_worms, board);
    return 123;
};