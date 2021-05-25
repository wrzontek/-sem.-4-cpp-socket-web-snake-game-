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
#include <cassert>

#define DEFAULT_TURNING_SPEED 6
#define DEFAULT_ROUNDS_PER_SEC 50
#define DEFAULT_BOARD_WIDTH 640
#define DEFAULT_BOARD_HEIGHT 480

#define CLIENT_MAX (2 +  MAX_PLAYERS)
#define MAX_CONSECUTIVE_CLIENT_MSG 20

namespace {
    uint64_t my_rand;
    int port, turning_speed, rounds_per_sec, board_width, board_height, send_socket;

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
                    port = atoi(optarg);
                    if (port < 2 || port > 65535)
                        fatal("invalid port argument");
                    break;
                case 's':
                    my_rand = atoi(optarg);
                    if (my_rand < 1 || my_rand > 4294967295)
                        fatal("invalid seed argument");
                    break;
                case 't':
                    turning_speed = atoi(optarg);
                    if (turning_speed < 1 || turning_speed > 90)
                        fatal("invalid turning speed argument");
                    break;
                case 'v':
                    rounds_per_sec = atoi(optarg);
                    if (rounds_per_sec < 1 || rounds_per_sec > 250)
                        fatal("invalid rounds per second argument");
                    break;
                case 'w':
                    board_width = atoi(optarg);
                    if (board_width < 1 || board_width > 4096)
                        fatal("invalid board width argument");
                    break;
                case 'h':
                    board_height = atoi(optarg);
                    if (board_height < 1 || board_height > 4096)
                        fatal("invalid board height argument");
                    break;
                default:
                    fatal("Arguments: [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]\n");
            }
        }
    }

    bool check_game_over(std::map<std::string, player_info> &players) {
        int alive_players = 0;
        for (auto &pair2: players) {
            if (pair2.second.in_game)
                alive_players++;
        }
        assert(alive_players > 0);

        return alive_players == 1;
    }

    void send_to_all(std::map<std::string, player_info> &players,
                     std::map<uint64_t, sockaddr_in6> &observers,
                     char *event, size_t size) {
        std::cout << "SENDING!!!\n";

        uint32_t len = be32toh(*(uint32_t *) event);
        uint32_t event_no = be32toh(*(uint32_t *) (event + 4));
        uint8_t event_type = *(uint8_t *) (event + 8);
        std::cout << "len, number, type: " << len << ", " << event_no << ", " << (int)event_type << std::endl;
        uint32_t sent_crc32 = be32toh(*(uint32_t *) (event + len + 4));
/*        event_common *event_b = (event_common *)event;
        uint32_t len = be32toh(event_b->len);
        uint32_t event_no = be32toh(event_b->event_no);
        uint8_t event_type = event_b->event_type;
        uint32_t sent_crc32 = be32toh(*(uint32_t *) (event + len + 4));*/

        std::cout << "crc32:" << sent_crc32 << std::endl;

        for (auto &pair: players) {
            player_info &client_player = pair.second;
            if (client_player.disconnected)
                continue;

            std::cout << "Sending to player\n";
            int dupa = sendto(send_socket, event, size, 0,
                   (sockaddr *)&client_player.address, sizeof(client_player.address));
            std::cout << dupa << "<>" << size << std::endl;
        }
        for (auto &pair: observers) {
            auto addr = pair.second;
            std::cout << "Sending to observer\n";
            sendto(send_socket, event, size, 0,
                   (sockaddr *)&(addr), sizeof(addr));
        }
    }

    void handle_player_elimination(player_info &player, std::map<std::string, player_info> &players,
                                   bool &game_in_progess, std::map<uint64_t, sockaddr_in6> &observers,
                                   std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
        player.in_game = false;
        event_player_eliminated event_elimination{htobe32(sizeof(event_player_eliminated) - 8),
                                                  htobe32((uint32_t)game_events.size()),
                                                  TYPE_PLAYER_ELIMINATED, (uint8_t)player.number, 0};
        event_elimination.crc32 = crc32buf((char *)&event_elimination, sizeof(event_player_eliminated) - 4);
        game_events.emplace_back(event_elimination);

        send_to_all(players, observers, (char *)&event_elimination, sizeof(event_elimination));

        if (check_game_over(players)) {
            game_in_progess = false;
            event_game_over event_game_over{htobe32(sizeof(event_game_over) - 8),
                                            htobe32((uint32_t)game_events.size()),
                                            TYPE_GAME_OVER, 0};
            event_game_over.crc32 = crc32buf((char *)&event_elimination, sizeof(event_player_eliminated) - 4);
            game_events.emplace_back(event_game_over);

            send_to_all(players, observers, (char *)&event_game_over, sizeof(event_game_over));
        }
    }

    void send_new_game(std::map<std::string, player_info> &players,
                       std::map<uint64_t, sockaddr_in6> &observers,
                       std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
        std::cout << "SENDING NEW GAME\n";
        std::string player_list;
        for (auto &pair : players) {
            std::string name = pair.first;
            player_list += name + " ";
        }
        player_list[player_list.size() - 1] = '\0';

        event_new_game event_new_game{htobe32(17 + player_list.size()),
                                      htobe32(game_events.size()), TYPE_NEW_GAME,
                                      htobe32(board_width), htobe32(board_height), 0};

        for (int i = 0; i < player_list.size(); i++)
            event_new_game.list_and_crc[i] = player_list[i];

        uint32_t crc32 = htobe32(crc32buf((char *)&event_new_game, 17 + player_list.size()));
        std::cout << "NEW GAME CRC32: " << be32toh(crc32) << std::endl;
        // todo tutaj te wskaźniki kopiowanka dziwne poprawić
        //memcpy((char *)&event_new_game + 17 + player_list.size(), &crc32, sizeof(uint32_t));

        game_events.emplace_back(event_new_game);

        send_to_all(players, observers, (char *)&event_new_game, 21 + player_list.size());
    }


    void init_game(uint32_t &game_id, std::map<std::string, player_info> &players,
                   std::map<uint64_t, sockaddr_in6> &observers, std::vector<bool> &board,
                   std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events,
                   bool &game_in_progess) {
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
                handle_player_elimination(player, players, game_in_progess, observers, game_events);
                if (!game_in_progess)
                    return;
            } else {
                std::cout << "PIXEL\n";
                board[y * board_width + x] = EATEN;
                event_pixel event_pixel{htobe32(sizeof(event_pixel) - 8),
                                        htobe32((uint32_t)game_events.size()),
                                        TYPE_PIXEL, (uint8_t)player.number,
                                        htobe32(x), htobe32(y), 0};
                event_pixel.crc32 = htobe32(crc32buf((char *)&event_pixel, sizeof(event_pixel) - 4));
                game_events.emplace_back(event_pixel);

                send_to_all(players, observers, (char *)&event_pixel, sizeof(event_pixel));
            }
        }
    }

    void do_turn(uint32_t &game_id, std::map<std::string, player_info> &players,
                 std::map<uint64_t, sockaddr_in6> &observers, std::vector<bool> &board,
                 std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events,
                 bool &game_in_progess) {

        for (auto &pair: players) {
            player_info &player = pair.second;
            std::cout << "ruch gracza " << player.number << std::endl;
            if (player.in_game) {
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
                if (board[y * board_width + x] == EATEN || y > (board_height - 1) || x > (board_width - 1)) {
                    handle_player_elimination(player, players, game_in_progess, observers, game_events);
                    if (!game_in_progess)
                        return;
                } else {
                    board[y * board_width + x] = EATEN;
                    event_pixel event_pixel{htobe32(sizeof(event_pixel) - 8),
                                            htobe32((uint32_t)game_events.size()),
                                            TYPE_PIXEL, (uint8_t)player.number,
                                            htobe32(x), htobe32(y), 0};
                    event_pixel.crc32 = htobe32(crc32buf((char *)&event_pixel, sizeof(event_player_eliminated) - 4));
                    game_events.emplace_back(event_pixel);

                    send_to_all(players, observers, (char *)&event_pixel, sizeof(event_pixel));
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
}

void send_history(uint32_t expected_event_no, sockaddr_in6 client_address, char *buf,
                  std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> &game_events) {
    static char event_buf[550];

    if (expected_event_no < game_events.size()) {
        int total_size = 0;
        int size = 0;

        for (int i = expected_event_no; i < game_events.size(); i++) {
            auto event_variant = game_events[1];
            if (auto new_game_p = std::get_if<event_new_game>(&event_variant)) {
                // TODO poprawić pewnie
                event_new_game new_game = *new_game_p;
                memcpy(event_buf, (const char *)&new_game, new_game.len + 8);
                size = new_game.len + 8;
            } else if (auto pixel_p = std::get_if<event_pixel>(&event_variant)) {
                event_pixel pixel = *pixel_p;
                memcpy(event_buf, (const char *)&pixel, sizeof(pixel));
                size = sizeof(pixel);
            } else if (auto elim_p = std::get_if<event_player_eliminated>(&event_variant)) {
                event_player_eliminated elim = *elim_p;
                memcpy(event_buf, (const char *)&elim, sizeof(elim));
                size = sizeof(elim);
            } else if (auto game_over_p = std::get_if<event_game_over>(&event_variant)) {
                event_game_over game_over = *game_over_p;
                memcpy(event_buf, (const char *)&game_over, sizeof(game_over));
                size = sizeof(game_over);
            }

            if (total_size + size <= DATAGRAM_MAX_SIZE) {
                memcpy(buf + total_size, (const char *)&buf, size);
                total_size += size;
            } else {
                sendto(send_socket, (char *)&buf, total_size, 0,
                       (sockaddr *)(&client_address), sizeof(client_address));

                send_history(i, client_address, buf, game_events);
            }
        }

        sendto(send_socket, (char *)&buf, total_size, 0,
               (sockaddr *)(&client_address), sizeof(client_address));
    }
}

int main(int argc, char *argv[]) {
    port = DEFAULT_SERVER_PORT;
    turning_speed = DEFAULT_TURNING_SPEED;
    rounds_per_sec = DEFAULT_ROUNDS_PER_SEC;
    board_width = DEFAULT_BOARD_WIDTH;
    board_height = DEFAULT_BOARD_HEIGHT;
    my_rand = time(NULL);

    get_args(argc, argv);

    uint32_t game_id;
    std::map<std::string, player_info> players; // mapujemy nazwę do informacji
    int ready_players = 0;
    std::map<uint64_t, sockaddr_in6> observers;
    std::map<uint64_t, uint16_t> client_poll_position; // pun not intended
    std::map<uint64_t, std::string> player_ids;
    std::vector<bool> board;
    board.resize(board_width * board_height);
    std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>> game_events;
    bool game_in_progress = false;

    /*
     * client[0] na obsługę komunikatów
     * client[1] na timer rundy
     * pozostałe na timery graczy (do zrywania połączeń przy braku komunikacji przez 2s)
    */

    pollfd client[CLIENT_MAX];
    sockaddr_in6 serveraddr{};
    sockaddr_in6 client_address{};

    for (int i = 0; i < CLIENT_MAX; i++) {
        client[i].fd = -1;
        client[i].events = POLLIN;
        client[i].revents = 0;
    }

    create_timer(client[1].fd, TIMER_ROUND, rounds_per_sec);

    send_socket = client[0].fd = socket(PF_INET6, SOCK_DGRAM, 0);
    if (client[0].fd == -1)
        syserr("socket");

    int on = 1;
    if (setsockopt(client[0].fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &on, sizeof(on)) < 0)
        syserr("setsockopt");

    if (fcntl(client[0].fd, F_SETFL, fcntl(client[0].fd, F_GETFL, 0) | O_NONBLOCK) == -1)
        syserr("fcntl");

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(port);
    serveraddr.sin6_addr = in6addr_any;

    if (bind(client[0].fd, (struct sockaddr *) &serveraddr,
             (socklen_t) sizeof(serveraddr)) == -1)
        syserr("bind serveraddr");

    client_msg in_msg{};
    uint64_t exp;
    char *buf = (char *)calloc(DATAGRAM_MAX_SIZE, 1);
    if (buf == NULL)
        syserr("calloc");

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) { // pracujemy aż coś się mocno nie zepsuje
        for (int i = 1; i < CLIENT_MAX; i++)
            client[i].revents = 0;

        int ret = poll(client, CLIENT_MAX, -1);

        if (ret <= 0) // zawsze będzie budzić poll co najmniej timer tury
            syserr("poll or timer");

        if (client[0].revents & POLLIN) {
            /* Komunikat od klienta */
            std::cout << "Komunikat od klienta\n";
            // przetwarzamy część/wszystkie datagramy co doszły
            // jak niepoprawny komunikat to ignorujemy
            // jak nieznany gracz to akceptujemy, update struktur danych
            // jak znany gracz i mniejsze session_id to ignorujemy
            // jak znany gracz i >= session_id to update struktur danych

            /* pętla for a nie while(true) żeby serwer nie był sparaliżowany np
             * masą połączęń i odłączeń obserwatorów którym trzeba wysłać sporą historię */
            for (int t = 0; t < MAX_CONSECUTIVE_CLIENT_MSG; t++) {
                socklen_t rcva_len = (socklen_t) sizeof(client_address);
                ret = recvfrom(client[0].fd, ((char *) &in_msg), sizeof(client_msg), 0,
                               (struct sockaddr *) &client_address, &rcva_len);

                if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // brak komunikatów do odebrania więc zerujemy revents
                    client[0].revents = 0;
                    break;
                }
                if (ret == -1)
                    syserr("error in gui");

                if (in_msg.turn_direction > 2)
                    continue;

                uint64_t session_id = in_msg.session_id = be64toh(in_msg.session_id);
                uint32_t next_event = in_msg.next_expected_event_no = be32toh(in_msg.next_expected_event_no);
                uint8_t turn_direction = in_msg.turn_direction;

                std::cout << "id: " << session_id << " expected event: " << next_event << " direction: " << (int)turn_direction << std::endl;
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

                        send_history(next_event, client_address, buf, game_events);

                        for (int i = 2; i < CLIENT_MAX; i++) {
                            if (client[i].fd == -1) {
                                create_timer(client[i].fd, TIMER_TIMEOUT, -1);
                                client_poll_position.insert(std::pair(session_id, i));
                                break;
                            }
                        }
                    } else {
                        /* stary obserwator */
                        send_history(next_event, client_address, buf, game_events);

                        int poll_position = client_poll_position[session_id];
                        update_timer(client[poll_position]);
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

                    send_history(next_event, client_address, buf, game_events);

                    for (int i = 2; i < CLIENT_MAX; i++) {
                        if (client[i].fd == -1) {
                            create_timer(client[i].fd, TIMER_TIMEOUT, -1);
                            client_poll_position.insert(std::pair(session_id, i));
                            break;
                        }
                    }
                } else {
                    /* znany gracz */
                    std::cout << "ZNANY GRACZ\n";
                    player_info &player = players[name];
                    if (session_id < player.id || players[name].disconnected)
                        continue;
                    else {
                        // update id i związanych struktur danych
                        int64_t old_id = player.id;
                        int16_t pp = client_poll_position[old_id];
                        player.id = session_id;

                        client_poll_position.erase(old_id);
                        client_poll_position.insert(std::pair(session_id, pp));
                        player_ids.erase(old_id);
                        player_ids.insert(std::pair(session_id, name));
                    }

                    player.turn_direction = turn_direction;
                    if (!player.ready && turn_direction != 0) {
                        player.ready = true;
                        ready_players++;
                    }
                    int poll_position = client_poll_position[session_id];
                    update_timer(client[poll_position]);

                    send_history(next_event, client_address, buf, game_events);
                }
            }
        }

        // iteracja po timerach graczy
        for (int i = 2; i < CLIENT_MAX; i++) {
            if (client[i].revents & POLLIN) {
                ret = read(client[i].fd, &exp, sizeof(uint64_t));
                std::cout << "disconnecting client\n";
                client[i].fd = -1;
                uint64_t client_session_id;
                for (std::pair pair : client_poll_position) {
                    if (pair.second == i) {
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
                        player_ids.erase(client_session_id);
                        players.erase(name);
                    }
                }
            }
        }


        if (client[1].revents & POLLIN) {
            /* Czas przeliczyć turę bądź sprawdzić czy zaczynamy grę */
            std::cout << "TURA\n";
            read(client[1].fd, &exp, sizeof(uint64_t));
            if (game_in_progress) {
                do_turn(game_id, players, observers, board, game_events, game_in_progress);
            } else if (ready_players >= 2 && ready_players == player_ids.size()) {
                game_in_progress = true;
                init_game(game_id, players, observers, board, game_events, game_in_progress);
            }

            bool player_in_game = false;
            for (auto &pair : players) {
                if (pair.second.in_game) {
                    player_in_game = true;
                    break;
                }
            }

            if (!game_in_progress && player_in_game) {
                // gra się właśnie zakończyła
                std::cout << "\n\n GAME OVER \n\n";
                game_events = std::vector<std::variant<event_new_game, event_pixel, event_player_eliminated, event_game_over>>();
                for (auto &pair : players) {
                    player_info &player = pair.second;
                    player.ready = false;
                    player.in_game = false;
                    if (player.disconnected) {
                        client[client_poll_position[player.id]].fd = -1;
                        client_poll_position.erase(player.id);
                        players.erase(player_ids[player.id]);
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