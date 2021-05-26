#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fcntl.h>
#include <getopt.h>
#include <cstring>
#include <map>
#include <algorithm>
#include <cmath>
#include <poll.h>
#include <variant>
#include <sys/timerfd.h>
#include "common.h"

#define DEFAULT_TURNING_SPEED 6
#define DEFAULT_ROUNDS_PER_SEC 50
#define DEFAULT_BOARD_WIDTH 640
#define DEFAULT_BOARD_HEIGHT 480

#define CLIENT_MAX (2 +  MAX_PLAYERS)
#define MAX_CONSECUTIVE_CLIENT_MSG 20

namespace {
    uint64_t my_rand;
    int client_socket;
    int port = DEFAULT_SERVER_PORT;
    int turning_speed = DEFAULT_TURNING_SPEED;
    int rounds_per_sec = DEFAULT_ROUNDS_PER_SEC;
    int board_width = DEFAULT_BOARD_WIDTH;
    int board_height = DEFAULT_BOARD_HEIGHT;

    /* https://stackoverflow.com/questions/31502120/sin-and-cos-give-unexpected-results-for-well-known-angles */
    inline double degree_to_radian(double d) {
        return (d / 180.0) * ((double) M_PI);
    }

    uint32_t get_random() {
        uint32_t result = (uint32_t) my_rand;
        my_rand = (my_rand * 279410273) % 4294967291;
        return result;
    }

    struct player_info {
        bool disconnected;
        int16_t number;
        uint64_t id;
        bool ready;
        bool in_game;
        double x;
        double y;
        int16_t direction;
        uint8_t turn_direction;
        sockaddr_in6 address;
    };

    void get_args(int argc, char *argv[]) {
        int opt;
        while ((opt = getopt(argc, argv, "p:s:t:v:w:h:")) != -1) {
            switch (opt) {
                case 'p':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        port = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (port < 2 || port > 65535)
                            fatal("invalid port argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                case 's':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        my_rand = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (my_rand < 1 || my_rand > 4294967295)
                            fatal("invalid seed argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                case 't':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        turning_speed = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (turning_speed < 1 || turning_speed > 90)
                            fatal("invalid turning speed argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                case 'v':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        rounds_per_sec = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (rounds_per_sec < 1 || rounds_per_sec > 250)
                            fatal("invalid rounds per second argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                case 'w':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        board_width = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (board_width < BOARD_WIDTH_MIN || board_width > BOARD_WIDTH_MAX)
                            fatal("invalid board width argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                case 'h':
                    try {
                        std::string arg = optarg;
                        std::size_t pos;
                        board_height = std::stoi(arg, &pos);
                        if (pos < arg.size())
                            fatal("Trailing characters after number argument");
                        if (board_height < BOARD_HEIGHT_MIN || board_height > BOARD_HEIGHT_MAX)
                            fatal("invalid board height argument");
                    } catch (std::invalid_argument const &ex) {
                        fatal("Invalid number argument");
                    } catch (std::out_of_range const &ex) {
                        fatal("Number argument out of range");
                    }
                    break;
                default:
                    fatal("Arguments: [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n");
            }
        }

        if (argc - optind != 0)
            fatal("Arguments: [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n");

    }

    bool check_game_over(std::map<std::string, player_info> &players) {
        int alive_players = 0;
        for (auto &pair2: players) {
            if (pair2.second.in_game)
                alive_players++;
        }

        return alive_players == 1;
    }

    void send_to_all(std::map<std::string, player_info> &players,
                     std::map<uint64_t, sockaddr_in6> &observers,
                     char *event, size_t size) {
        std::cout << "SENDING!!!\n";

        uint32_t len = be32toh(*(uint32_t *) event);
        uint32_t event_no = be32toh(*(uint32_t *) (event + 4));
        uint8_t event_type = *(uint8_t *) (event + 8);
        std::cout << "len, number, type: " << len << ", " << event_no << ", " << (int) event_type << std::endl;
        uint32_t sent_crc32 = be32toh(*(uint32_t *) (event + len + 4));
        uint32_t my_crc32 = crc32buf(event, len + 4);

        std::cout << "crc32(sent, mine):" << sent_crc32 << " " << my_crc32 << std::endl;

        for (auto &pair: players) {
            player_info &client_player = pair.second;
            if (client_player.disconnected)
                continue;

            std::cout << "Sending to player\n";
            sendto(client_socket, event, size, 0,
                   (sockaddr *) &client_player.address, sizeof(client_player.address));
        }
        for (auto &pair: observers) {
            auto addr = pair.second;
            std::cout << "Sending to observer\n";
            sendto(client_socket, event, size, 0,
                   (sockaddr *) &(addr), sizeof(addr));
        }
    }

    void handle_player_elimination(player_info &player, std::map<std::string, player_info> &players,
                                   bool &game_in_progress, std::map<uint64_t, sockaddr_in6> &observers,
                                   std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
        player.in_game = false;
        event_player_eliminated event_elimination{htobe32((uint32_t) sizeof(event_player_eliminated) - 8),
                                                  htobe32((uint32_t) game_events.size()),
                                                  TYPE_PLAYER_ELIMINATED, (uint8_t)player.number, 0};
        event_elimination.crc32 = htobe32(crc32buf((char *) &event_elimination, sizeof(event_player_eliminated) - 4));
        game_events.emplace_back(event_elimination);

        send_to_all(players, observers, (char *) &event_elimination, sizeof(event_elimination));

        if (check_game_over(players)) {
            game_in_progress = false;
            event_game_over event_game_over{htobe32((uint32_t) sizeof(event_game_over) - 8),
                                            htobe32((uint32_t) game_events.size()),
                                            TYPE_GAME_OVER, 0};
            event_game_over.crc32 = htobe32(crc32buf((char *) &event_game_over, sizeof(event_game_over) - 4));
            game_events.emplace_back(event_game_over);

            send_to_all(players, observers, (char *) &event_game_over, sizeof(event_game_over));
        }
    }

    void send_new_game(std::map<std::string, player_info> &players,
                       std::map<uint64_t, sockaddr_in6> &observers,
                       std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
        std::cout << "SENDING NEW GAME\n";
        std::string player_list;
        static char placeholder = (char) 246; // placeholder char do zmiany na \0

        for (auto &pair : players) {
            std::string name = pair.first;
            player_list += name + placeholder;
        }
        uint32_t player_list_size = player_list.size();

        char *player_list_data = player_list.data();
        for (int i = 0; i < player_list_size; i++) {
            if (player_list_data[i] == placeholder)
                player_list_data[i] = '\0';
        }

        uint32_t len = 13 + player_list_size;

        event_new_game event_new_game{htobe32(len),
                                      htobe32(game_events.size()), TYPE_NEW_GAME,
                                      htobe32(board_width), htobe32(board_height), 0};

        for (int i = 0; i < player_list_size; i++)
            event_new_game.list_and_crc[i] = player_list_data[i];

        uint32_t crc32 = htobe32(crc32buf((char *) &event_new_game, len + 4));

        memcpy((char *) &event_new_game + len + 4, &crc32, sizeof(uint32_t));

        game_events.emplace_back(event_new_game);

        send_to_all(players, observers, (char *) &event_new_game, len + 8);
    }


    void init_game(uint32_t &game_id, std::map<std::string, player_info> &players,
                   std::map<uint64_t, sockaddr_in6> &observers, std::vector<bool> &board,
                   std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events,
                   bool &game_in_progress) {
        game_id = get_random();
        for (int i = 0; i < board.size(); i++)
            board[i] = NOT_EATEN;

        send_new_game(players, observers, game_events);

        int n = 0;
        for (auto &pair: players) {
            player_info &player = pair.second;
            player.in_game = true;
            player.number = n;
            std::cout << "ruch gracza " << player.number << std::endl;
            n++;
            uint32_t x = player.x = (get_random() % (board_width - 1) + 0.5);
            uint32_t y = player.y = (get_random() % (board_height - 1) + 0.5);
            player.direction = get_random() % 360;

            std::cout << x << " " << y << "  " << y * board_width + x << " <> " << board.size() << std::endl;
            if (board[y * board_width + x] == EATEN || y > (board_height - 1) || x > (board_width - 1)) {
                std::cout << "ELIMINATED\n";
                handle_player_elimination(player, players, game_in_progress, observers, game_events);
                if (!game_in_progress)
                    return;
            } else {
                std::cout << "PIXEL\n";
                board[y * board_width + x] = EATEN;
                event_pixel event_pixel{htobe32(sizeof(event_pixel) - 8),
                                        htobe32((uint32_t) game_events.size()),
                                        TYPE_PIXEL, (uint8_t) player.number,
                                        htobe32(x), htobe32(y), 0};
                event_pixel.crc32 = htobe32(crc32buf((char *) &event_pixel, sizeof(event_pixel) - 4));
                game_events.emplace_back(event_pixel);

                send_to_all(players, observers, (char *) &event_pixel, sizeof(event_pixel));
            }
        }
    }

    void do_turn(uint32_t &game_id, std::map<std::string, player_info> &players,
                 std::map<uint64_t, sockaddr_in6> &observers, std::vector<bool> &board,
                 std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events,
                 bool &game_in_progess) {

        for (auto &pair: players) {
            player_info &player = pair.second;
            if (player.in_game) {
                std::cout << "ruch gracza " << player.number << std::endl;
                if (player.turn_direction == TURN_RIGHT)
                    player.direction += turning_speed;
                else if (player.turn_direction == TURN_LEFT)
                    player.direction -= turning_speed;

                uint32_t old_x = player.x;
                uint32_t old_y = player.y;

                double direction = degree_to_radian(player.direction);
                uint32_t x = (player.x += std::cos(direction));
                uint32_t y = (player.y += std::sin(direction));

                if (x == old_x && y == old_y)
                    continue;

                std::cout << x << " " << y << "  " << y * board_width + x << " <> " << board.size() << std::endl;
                if (y > (board_height - 1) || x > (board_width - 1) || board[y * board_width + x] == EATEN) {
                    handle_player_elimination(player, players, game_in_progess, observers, game_events);
                    if (!game_in_progess)
                        return;
                } else {
                    board[y * board_width + x] = EATEN;
                    event_pixel event_pixel{htobe32(sizeof(event_pixel) - 8),
                                            htobe32((uint32_t) game_events.size()),
                                            TYPE_PIXEL, (uint8_t) player.number,
                                            htobe32(x), htobe32(y), 0};

                    event_pixel.crc32 = htobe32(crc32buf((char *) &event_pixel, sizeof(event_pixel) - 4));
                    game_events.emplace_back(event_pixel);

                    send_to_all(players, observers, (char *) &event_pixel, sizeof(event_pixel));
                }
            }
        }
    }

    void update_timer(pollfd &client) {
        itimerspec new_value{};
        timespec now{};
        if (clock_gettime(CLOCK_REALTIME, &now) == -1)
            syserr("clock_gettime");

        new_value.it_value.tv_sec = now.tv_sec + CLIENT_TIMEOUT_SECONDS;
        new_value.it_value.tv_nsec = now.tv_nsec;
        new_value.it_interval.tv_sec = CLIENT_TIMEOUT_SECONDS;
        new_value.it_interval.tv_nsec = 0;

        if (timerfd_settime(client.fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)
            syserr("timerfd_settime update");

        uint64_t exp;
        if (client.revents & POLLIN) {
            client.revents = 0;
            read(client.fd, &exp, sizeof(uint64_t));
        }
    }


    void send_history(uint32_t expected_event_no, sockaddr_in6 client_address, std::vector<int8_t> &buf_vector,
                      std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
        static char event_buf[sizeof(event_new_game)];  // na jeden event, new_game to największy możliwy

        if (expected_event_no < game_events.size()) {
            std::cout << "SENDING HISTORY!!!\n";
            int total_size = 0;
            int size = 0;

            for (int i = expected_event_no; i < game_events.size(); i++) {
                auto event_variant = game_events[i];
                if (auto new_game_p = std::get_if<event_new_game>(&event_variant)) {
                    event_new_game new_game = *new_game_p;
                    uint32_t len = htobe32(new_game.len);
                    memcpy(event_buf, (const char *) &new_game, len + 8);
                    size = (int) len + 8;
                }
                else if (auto pixel_p = std::get_if<event_pixel>(&event_variant)) {
                    event_pixel pixel = *pixel_p;
                    memcpy(event_buf, (const char *) &pixel, sizeof(pixel));
                    size = sizeof(event_pixel);
                }
                else if (auto elim_p = std::get_if<event_player_eliminated>(&event_variant)) {
                    event_player_eliminated elim = *elim_p;
                    memcpy(event_buf, (const char *) &elim, sizeof(elim));
                    size = sizeof(event_player_eliminated);
                }
                else if (auto game_over_p = std::get_if<event_game_over>(&event_variant)) {
                    event_game_over game_over = *game_over_p;
                    memcpy(event_buf, (const char *) &game_over, sizeof(game_over));
                    size = sizeof(event_game_over);
                }

                if (total_size + size <= DATAGRAM_MAX_SIZE) {
                    memcpy(buf_vector.data() + total_size, event_buf, size);
                    total_size += size;
                }
                else {
                    sendto(client_socket, (char *) buf_vector.data(), total_size, 0,
                           (sockaddr *) (&client_address), sizeof(client_address));

                    send_history(i, client_address, buf_vector, game_events);
                }
            }

            uint32_t len = be32toh(*(uint32_t *) buf_vector.data());
            uint32_t event_no = be32toh(*(uint32_t *) (buf_vector.data() + 4));
            uint8_t event_type = *(uint8_t *) (buf_vector.data() + 8);
            std::cout << "len, number, type: " << len << ", " << event_no << ", " << (int) event_type << std::endl;
            uint32_t sent_crc32 = be32toh(*(uint32_t *) (buf_vector.data() + len + 4));
            std::cout << "crc32:" << sent_crc32 << std::endl;

            sendto(client_socket, (char *) buf_vector.data(), total_size, 0,
                   (sockaddr *) (&client_address), sizeof(client_address));
        }
    }

    void kick_timeouted_clients(pollfd *poll_arr, std::map<std::string, player_info> &players,
                                std::map<uint64_t, sockaddr_in6> &observers,
                                std::map<uint64_t, uint16_t> &client_poll_position,
                                std::map<uint64_t, std::string> &player_ids,
                                bool game_in_progress, int &ready_players) {
        for (int i = 2; i < CLIENT_MAX; i++) {
            if (poll_arr[i].revents & POLLIN) {
                uint64_t exp;
                read(poll_arr[i].fd, &exp, sizeof(uint64_t));
                poll_arr[i].revents = 0;
                std::cout << "disconnecting client\n";
                poll_arr[i].fd = -1;
                uint64_t client_session_id;
                for (std::pair pair : client_poll_position) {
                    if (pair.second == i) {
                        std::cout << "znaleziono delikwenta\n";
                        client_session_id = pair.first;
                        break;
                    }
                }
                if (observers.find(client_session_id) != observers.end()) {
                    // wywalamy observera
                    observers.erase(client_session_id);
                } else {
                    // wywalamy gracza
                    std::string name = player_ids[client_session_id];
                    player_info &player = players[name];
                    player.disconnected = true;
                    if (!game_in_progress) {
                        if (player.ready)
                            ready_players--;
                        player_ids.erase(client_session_id);
                        players.erase(name);
                    }
                }
                client_poll_position.erase(client_session_id);
            }
        }
    }

}

int main(int argc, char *argv[]) {
    my_rand = time(NULL);

    get_args(argc, argv);

    uint32_t game_id;
    std::map<std::string, player_info> players; // mapujemy nazwę do informacji
    int ready_players = 0;
    std::map<uint64_t, sockaddr_in6> observers; // session id do adresu
    std::map<uint64_t, uint16_t> client_poll_position; // pun not intended, session id do pozycji w pollu
    std::map<uint64_t, std::string> player_ids; // session id do nazwy gracza
    std::vector<bool> board;
    board.resize(board_width * board_height);
    std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> game_events;
    bool game_in_progress = false;

    /*
     * poll_arr[0] na odbieranie i wysyłanie komunikatów
     * poll_arr[1] na timer rundy
     * pozostałe na timery klientów (do zrywania połączeń przy braku komunikacji przez 2s)
    */

    pollfd poll_arr[CLIENT_MAX];
    for (int i = 0; i < CLIENT_MAX; i++) {
        poll_arr[i].fd = -1;
        poll_arr[i].events = POLLIN;
        poll_arr[i].revents = 0;
    }

    sockaddr_in6 serveraddr{};
    sockaddr_in6 client_address{};
    create_timer(poll_arr[1].fd, TIMER_ROUND, rounds_per_sec);

    client_socket = poll_arr[0].fd = socket(PF_INET6, SOCK_DGRAM, 0);
    if (poll_arr[0].fd == -1)
        syserr("socket");

    int on = 1;
    if (setsockopt(poll_arr[0].fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &on, sizeof(on)) < 0)
        syserr("setsockopt");

    if (fcntl(poll_arr[0].fd, F_SETFL, fcntl(poll_arr[0].fd, F_GETFL, 0) | O_NONBLOCK) == -1)
        syserr("fcntl");

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(port);
    serveraddr.sin6_addr = in6addr_any;

    if (bind(poll_arr[0].fd, (struct sockaddr *) &serveraddr,
             (socklen_t) sizeof(serveraddr)) == -1)
        syserr("bind serveraddr");

    client_msg in_msg{};
    std::vector<int8_t >buf(DATAGRAM_MAX_SIZE);

    if (buf.data() == NULL)
        syserr("vector alloc");

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) { // pracujemy aż coś się mocno nie zepsuje
        int ret = poll(poll_arr, CLIENT_MAX, -1);

        if (ret <= 0) // zawsze powinien nas budzić co najmniej timer tury
            syserr("poll or timer");

        if (poll_arr[0].revents & POLLIN) {
            /* Komunikat od klienta */
            std::cout << "Komunikat od klienta\n";

            /* pętla for a nie while(true) żeby serwer nie był sparaliżowany np
             * masą połączęń i odłączeń obserwatorów którym trzeba wysłać sporą historię */
            for (int t = 0; t < MAX_CONSECUTIVE_CLIENT_MSG; t++) {
                socklen_t rcva_len = (socklen_t) sizeof(client_address);
                memset(&in_msg, 0, sizeof(client_msg));
                ret = recvfrom(poll_arr[0].fd, (char *) &in_msg, sizeof(client_msg), 0,
                               (struct sockaddr *) &client_address, &rcva_len);

                if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // brak komunikatów do odebrania więc zerujemy revents
                    poll_arr[0].revents = 0;
                    break;
                }

                if (in_msg.turn_direction > 2)
                    continue;

                uint64_t session_id = be64toh(in_msg.session_id);
                uint32_t expected_event_no = be32toh(in_msg.next_expected_event_no);
                uint8_t turn_direction = in_msg.turn_direction;

                std::cout << "id: " << session_id << " expected event: " << expected_event_no << " direction: " << (int)turn_direction << std::endl;
                std::string name;
                bool invalid_msg = false;
                for (int i = 0; i < ret - 13; i++) {
                    uint8_t c = in_msg.player_name[i];
                    if (c < 33 || c > 126) {
                        invalid_msg = true;
                        break;
                    }
                    name += c;
                }
                if (invalid_msg || name.length() > MAX_NAME_LEN)
                    continue;

                std::cout << name << std::endl;

                if (name.empty()) {
                    std::cout << "OBSERWATOR\n";
                    /* obserwator */
                    if (observers.find(session_id) == observers.end()) {
                        /* nowy obserwator */
                        observers.insert(std::pair(session_id, client_address));

                        send_history(expected_event_no, client_address, buf, game_events);

                        for (int i = 2; i < CLIENT_MAX; i++) {
                            if (poll_arr[i].fd == -1) {
                                create_timer(poll_arr[i].fd, TIMER_TIMEOUT, -1);
                                client_poll_position.insert(std::pair(session_id, i));
                                break;
                            }
                        }
                    }
                    else {
                        /* stary obserwator */
                        send_history(expected_event_no, client_address, buf, game_events);

                        int poll_position = client_poll_position[session_id];
                        update_timer(poll_arr[poll_position]);
                    }
                    continue;
                }

                if (players.find(name) == players.end()) {
                    /* nowy gracz */
                    std::cout << "NOWY GRACZ\n";
                    if (player_ids.find(session_id) != player_ids.end())
                        continue;   // ignorujemy, znana sesja nie może ot tak zmienić nazwy gracza

                    player_ids.insert(std::pair(session_id, name));
                    player_info new_player_info{false, -1, session_id, false,false,
                                                0, 0, 0, turn_direction, client_address};

                    if (new_player_info.turn_direction != 0) {
                        new_player_info.ready = true;
                        ready_players++;
                    }

                    players.insert(std::pair(name, new_player_info));

                    send_history(expected_event_no, client_address, buf, game_events);

                    for (int i = 2; i < CLIENT_MAX; i++) {
                        if (poll_arr[i].fd == -1) {
                            create_timer(poll_arr[i].fd, TIMER_TIMEOUT, -1);
                            client_poll_position.insert(std::pair(session_id, i));
                            break;
                        }
                    }
                }
                else {
                    /* znany gracz */
                    std::cout << "ZNANY GRACZ\n";
                    player_info &player = players[name];
                    if (session_id < player.id || players[name].disconnected)
                        continue;
                    else {
                        // update id i związanych struktur danych
                        int64_t old_id = player.id;
                        int16_t poll_position = client_poll_position[old_id];
                        player.id = session_id;

                        client_poll_position.erase(old_id);
                        client_poll_position.insert(std::pair(session_id, poll_position));
                        player_ids.erase(old_id);
                        player_ids.insert(std::pair(session_id, name));
                    }

                    player.turn_direction = turn_direction;
                    if (!player.ready && turn_direction != 0) {
                        player.ready = true;
                        ready_players++;
                    }
                    int poll_position = client_poll_position[session_id];
                    update_timer(poll_arr[poll_position]);

                    send_history(expected_event_no, client_address, buf, game_events);
                }
            }
        }

        kick_timeouted_clients(poll_arr, players, observers, client_poll_position,
                               player_ids, game_in_progress, ready_players);

        if (poll_arr[1].revents & POLLIN) {
            /* Czas przeliczyć turę */
            std::cout << "TURA\n";
            uint64_t exp;
            read(poll_arr[1].fd, &exp, sizeof(uint64_t));
            poll_arr[1].revents = 0;

            if (game_in_progress) {
                do_turn(game_id, players, observers, board, game_events, game_in_progress);
            } else if (ready_players >= 2 && ready_players == player_ids.size()) {
                game_in_progress = true;
                init_game(game_id, players, observers, board, game_events, game_in_progress);
            }

            bool any_player_in_game = false;
            for (auto &pair : players) {
                if (pair.second.in_game) {
                    any_player_in_game = true;
                    break;
                }
            }

            if (!game_in_progress && any_player_in_game) {
                // gra się właśnie zakończyła
                std::cout << "\n\n GAME OVER \n\n";
                game_events.clear();
                for (auto &pair : players) {
                    player_info &player = pair.second;
                    std::cout << pair.first << std::endl;
                    player.ready = false;
                    player.in_game = false;
                    if (player.disconnected) {
                        std::cout << pair.first << std::endl;
                        poll_arr[client_poll_position[player.id]].revents = 0;
                        poll_arr[client_poll_position[player.id]].fd = -1;
                        client_poll_position.erase(player.id);
                        players.erase(pair.first);
                        player_ids.erase(player.id);
                    }
                }
                ready_players = 0;
            }
        }
    }
#pragma clang diagnostic pop
    return 0;
};