#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <getopt.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <utmpx.h>
#include <poll.h>
#include "common.h"

#define MAX_CONSECUTIVE_SERVER_MSG 10
#define MAX_CONSECUTIVE_GUI_MSG 50

namespace {
    uint64_t session_id;
    int server_sock, gui_sock;
    std::string player_name;
    char *server;
    char *server_port = NULL;
    char *gui_port = NULL;
    char *gui_addr_arg = NULL;
    char default_gui_addr[] = "localhost";
    char default_server_port[] = "2021";
    char default_gui_port[] = "20210";

    void get_args(int argc, char *argv[]) {
        int opt;
        while ((opt = getopt(argc, argv, "n:p:i:r:")) != -1) {
            switch (opt) {
                case 'n':
                    player_name = optarg;
                    if (player_name.length() > 20)
                        fatal("player name too long");
                    for (char c : player_name)
                        if (c > 126 || c < 33)
                            fatal("invalid characters in player name");
                    break;
                case 'p':
                    server_port = optarg;
                    break;
                case 'i':
                    gui_addr_arg = optarg;
                    break;
                case 'r':
                    gui_port = optarg;
                    break;
                default:
                    fatal("Arguments: game_server [-n player_name] [-p n] [-i gui_server] [-r n]\n");
            }
        }
        if (gui_addr_arg == NULL)
            gui_addr_arg = default_gui_addr;
        if (server_port == NULL)
            server_port = default_server_port;
        if (gui_port == NULL)
            gui_port = default_gui_port;
    }

    void init_hints(addrinfo &addr_hints, int protocol, int sock_type) {
        (void) memset(&addr_hints, 0, sizeof(struct addrinfo));
        addr_hints.ai_family = AF_UNSPEC;
        addr_hints.ai_socktype = sock_type;
        addr_hints.ai_protocol = protocol;
        addr_hints.ai_flags = 0;
        addr_hints.ai_addrlen = 0;
        addr_hints.ai_addr = NULL;
        addr_hints.ai_canonname = NULL;
        addr_hints.ai_next = NULL;
    }
}

void net_init() {
    addrinfo addr_hints_server{}, addr_hints_gui{};
    addrinfo *addr_server_result, *addr_gui_result;

    init_hints(addr_hints_server, IPPROTO_UDP, SOCK_DGRAM);

    if (getaddrinfo(server, server_port, &addr_hints_server, &addr_server_result) != 0)
        syserr("getaddrinfo server");

    server_sock = socket(addr_server_result->ai_family,
                         addr_server_result->ai_socktype,
                         addr_server_result->ai_protocol);
    if (server_sock < 0)
        syserr("socket server");

    if (connect(server_sock, addr_server_result->ai_addr, addr_server_result->ai_addrlen))
        syserr("connect server");

    freeaddrinfo(addr_server_result);

    if (fcntl(server_sock, F_SETFL, fcntl(server_sock, F_GETFL, 0) | O_NONBLOCK) == -1)
        syserr("fcntl server");


    init_hints(addr_hints_gui, IPPROTO_TCP, SOCK_STREAM);

    if (getaddrinfo(gui_addr_arg, gui_port, &addr_hints_gui, &addr_gui_result) != 0)
        syserr("getaddrinfo gui");

    gui_sock = socket(addr_gui_result->ai_family,
                      addr_gui_result->ai_socktype,
                      addr_gui_result->ai_protocol);
    if (gui_sock < 0)
        syserr("socket gui");

    if (connect(gui_sock, addr_gui_result->ai_addr, addr_gui_result->ai_addrlen))
        syserr("connect gui");

    freeaddrinfo(addr_gui_result);

    int on = 1;
    setsockopt(gui_sock,IPPROTO_TCP, TCP_NODELAY,
               (char *)&on, sizeof(int));

    if (fcntl(gui_sock, F_SETFL, fcntl(gui_sock, F_GETFL, 0) | O_NONBLOCK) == -1)
        syserr("fcntl gui");
}

void init_poll(pollfd client[]) {
    for (int i = 0; i <= 2; i++) {
        client[i].events = POLLIN;
        client[i].revents = 0;
    }

    client[0].fd = server_sock;
    client[1].fd = gui_sock;

    create_timer(client[2].fd, TIMER_SEND_UPDATE, -1);
}

std::string my_getline(std::string &buf_str) {
    if (buf_str.empty())
        return buf_str;

    std::size_t pos = buf_str.find('\n');
    if (pos == std::string::npos) {
        // nie znaleziono \n
        return "";
    }
    else {
        std::string line = buf_str.substr(0, pos);
        buf_str.erase(0, pos + 1);
        return line;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        fatal("Arguments: game_server [-n player_name] [-p n] [-i gui_server] [-r n]\n");

    timeval tv{};
    gettimeofday(&tv,NULL);
    session_id = 1000000 * tv.tv_sec + tv.tv_usec;

    player_name = ""; // puste oznacza obserwatora
    server = argv[1];
    get_args(argc - 1, argv + 1);

    net_init();

/*    rcv_len = read(gui_sock, command_buf, sizeof(command_buf) - 1);
    if (rcv_len < 0) {
        syserr("read");
    }

    printf("read from socket: %zd bytes: %s\n", rcv_len, command_buf);*/

    pollfd client[3];   // 0 - serwer, 1 - gui, 2 - timer
    init_poll(client);

    uint8_t turn_direction = 0;
    // set czy tam mapa graczy
    uint32_t maxx;
    uint32_t maxy;
    uint32_t expected_event_no = 0;
    int last_key_down = 0;
    std::string player_names_str;
    int player_count;

    client_msg msg_to_server = {htobe64(session_id), turn_direction, htobe32(expected_event_no), 0};
    for (int i = 0; i < player_name.size(); i++)
        msg_to_server.player_name[i] = player_name[i];

    write(server_sock, (char *)&msg_to_server, 13 + player_name.size());

    char command_buf[20];    // na "LEFT_KEY_DOWN\n" itp
    std::vector<int8_t >buf(DATAGRAM_MAX_SIZE);
    std::cout << buf.size();
    //char *buf = (char *)std::calloc(DATAGRAM_MAX_SIZE, sizeof(int8_t));
    //char buf[500];
    if (buf.data() == NULL)
        syserr("calloc");

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) {
        int ret = poll(client, 3, -1);

        if (ret <= 0)   // timer powinien nas budzić co UPDATE_NANOSECOND_INTERVAL
            syserr("poll or timer");

        if (client[0].revents & POLLIN) {
            for (int t = 0; t < MAX_CONSECUTIVE_SERVER_MSG; t++) {
                // limit żeby gra była responsive na input gracza
                std::cout << "WIADOMOŚĆ OD SERWERA\n";
                ret = read(client[0].fd, buf.data(), buf.size());

                if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    std::cout << "BRAK" << std::endl;
                    // brak komunikatów do odebrania więc zerujemy revents
                    client[0].revents = 0;
                    break;
                }

                auto event_buf = buf.data();
                while (ret > 0) {
                    std::cout << "ret: " << ret << std::endl;
                    //event_common *event_c = (event_common *)event_buf;
                    uint32_t len = be32toh(*(uint32_t *) event_buf);
                    uint32_t event_no = be32toh(*(uint32_t *) (event_buf + 4));
                    uint8_t event_type = *(uint8_t *) (event_buf + 8);
                    std::cout << "len, number, type: " << len << ", " << event_no << ", " << (int)event_type << std::endl;

                    uint32_t sent_crc32 = be32toh(*(uint32_t *) (event_buf + len + 4));
                    uint32_t my_crc32 = crc32buf(event_buf, len + 4);

                    std::cout << "crc32(sent, mine):" << sent_crc32 << " " << my_crc32 << std::endl;

                    //if (my_crc32 != sent_crc32)
                    //    break;

                    if (event_type == TYPE_NEW_GAME) {
                        if (event_no != 0) {
                            exit(1); // todo
                        }
                        maxx = be32toh(*(uint32_t *) (event_buf + 9));
                        maxy = be32toh(*(uint32_t *) (event_buf + 13));
                        std::cout << "NEW GAME\n";
                        std::cout << "X, Y: " << maxx << " " << maxy << std::endl;
                        player_count = 0;
                        player_names_str = "";
                        auto *player_names = event_buf + 17;
                        int i = 0;
                        while (player_names[i] != '\0') {
                            // spacja to separator
                            auto c = player_names[i];
                            if (i > len - 13 || ((c < 33 || c > 126) && c != ' ')) {
                                //exit(1); // todo
                            }
                            player_names_str += c;
                            if (c == ' ')
                                player_count ++;
                            i++;
                        }
                        player_count++;
                        std::cout << player_names_str << std::endl;
                        std::cout << "player count: " << player_count << std::endl;

                    }
                    else if (event_type == TYPE_PLAYER_ELIMINATED) {
                            std::cout << "PLAYER ELIMINATED\n";
                    }
                    else if (event_type == TYPE_GAME_OVER) {
                        expected_event_no = 0;
                        std::cout << "NEW GAME\n";
                    }
                    else if (event_type == TYPE_PIXEL) {
                        std::cout << "PIXEL\n";
                    }
                    else {
                        std::cout << "UNKNOWN, ignoring\n";
                        break;
                    }

                    // jeśli poprawny evencik i event_no = expected to wysyłamy do gui i expected++
                    // i sprawdzamy czy kolejny event mamy zapisany, jak tak to wysyłamy wszystkie takie

                    // jeśli poprawny evencik ale event_no > expected to zapisujemy se i wyślemy potem

                    event_buf += (8 + len);
                    ret -= (8 + (int)len);
                }
            }
        }

        if (client[1].revents & POLLIN) {
            std::cout << "WIADOMOŚĆ OD GUI\n";
            for (int t = 0; t < MAX_CONSECUTIVE_GUI_MSG; t++) {
                // limit żeby gui nas nie sparaliżowało
                ret = read(client[1].fd, command_buf, sizeof(command_buf) - 1);

                if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    std::cout << "BRAK" << std::endl;
                    // brak komunikatów do odebrania więc zerujemy revents
                    client[1].revents = 0;
                    break;
                }


                std::string command_buf_str(command_buf, ret);

                while (ret > 0) {
                    std::string command = my_getline(command_buf_str);

                    std::cout << ret << " " << command << std::endl;

                    if (command.empty())
                        break;

                    if (command == "LEFT_KEY_DOWN") {
                        turn_direction = TURN_LEFT;
                        last_key_down = TURN_LEFT;
                    } else if (command == "RIGHT_KEY_DOWN") {
                        turn_direction = TURN_RIGHT;
                        last_key_down = TURN_RIGHT;
                    } else if ((command == "LEFT_KEY_UP" && last_key_down == TURN_LEFT) ||
                               (command == "RIGHT_KEY_UP" && last_key_down == TURN_RIGHT)) {
                        turn_direction = 0;
                        last_key_down = 0;
                    }

                    ret -= ((int)command.size() + 1);
                }
            }
        }

        if (client[2].revents & POLLIN) {
            client[2].revents = 0;
            //std::cout << "timer up, sending to server\n";
            uint64_t exp;
            ret = read(client[2].fd, &exp, sizeof(uint64_t));
            if (ret != sizeof(uint64_t))
                syserr("timer");

            msg_to_server = {htobe64(session_id), turn_direction, htobe32(expected_event_no), 0};
            for (int i = 0; i < player_name.size(); i++)
                msg_to_server.player_name[i] = player_name[i];

            write(server_sock, (char *)&msg_to_server, 13 + player_name.size());
        }
    }
#pragma clang diagnostic pop

    return 0;
};