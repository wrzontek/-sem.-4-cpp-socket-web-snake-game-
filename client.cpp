#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include <unistd.h>
#include "crc.h"
#include "common.h"

#define DEFAULT_GUI_PORT 20210

namespace {
    int server_sock, gui_sock;
    std::string player_name;
    char *server_port = NULL;
    char *gui_port = NULL;
    char *gui_addr_arg = NULL;
    char default_gui_addr[] = "localhost";
    char default_server_port[] = "2021";
    char default_gui_port[] = "20210";

    // address serwera, gui


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

int main(int argc, char *argv[]) {
    if (argc < 2)
        fatal("Arguments: game_server [-n player_name] [-p n] [-i gui_server] [-r n]\n");

    addrinfo addr_hints_server{}, addr_hints_gui{};
    addrinfo *addr_server_result, *addr_gui_result;

//    sockaddr_in6 server_address;
//    sockaddr_in6 gui_address;
    player_name = ""; // puste oznacza obserwatora

    get_args(argc - 1, argv + 1);

    init_hints(addr_hints_server, IPPROTO_UDP, SOCK_DGRAM);

    if (getaddrinfo(argv[1], server_port, &addr_hints_server, &addr_server_result) != 0)
        syserr("getaddrinfo server");
    
    server_sock = socket(addr_server_result->ai_family,
                         addr_server_result->ai_socktype,
                         addr_server_result->ai_protocol);
    if (server_sock < 0)
        syserr("socket server");
    
    if (connect(server_sock, addr_server_result->ai_addr, addr_server_result->ai_addrlen))
        syserr("connect server");

    freeaddrinfo(addr_server_result);

//    char buf[10] = "012345678";
//    write(server_sock, buf, 10);

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

    return 123;
};