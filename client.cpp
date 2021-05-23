#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include "crc.h"
#include "common.h"

#define DEFAULT_GUI_PORT 20210

namespace {
    int server_sock, gui_sock;
    std::string player_name;
    uint16_t server_port = DEFAULT_SERVER_PORT;
    uint16_t gui_port = DEFAULT_GUI_PORT;
    char default_gui_addr[] = "localhost";
    char *gui_addr_arg = NULL;
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
                    server_port = atoi(optarg);
                    break;
                case 'i':
                    gui_addr_arg = optarg;
                    break;
                case 'r':
                    gui_port = atoi(optarg);
                    break;
                default:
                    fatal("Arguments: game_server [-n player_name] [-p n] [-i gui_server] [-r n]\n");
            }
        }
    }

    void init_hints(addrinfo &addr_hints) {
        (void) memset(&addr_hints, 0, sizeof(struct addrinfo));
        addr_hints.ai_family = AF_INET6; // IPv6
        addr_hints.ai_socktype = SOCK_DGRAM;
        addr_hints.ai_protocol = IPPROTO_UDP;
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

    addrinfo addr_hints_server, addr_hints_gui;
    addrinfo *addr_server_result, *addr_gui_result;

    sockaddr_in server_address;
    sockaddr_in gui_address;
    socklen_t rcva_len;
    size_t len;
    player_name = ""; // puste oznacza obserwatora

    get_args(argc - 1, argv + 1);

    if (gui_addr_arg == NULL)
        gui_addr_arg = default_gui_addr;

    init_hints(addr_hints_server);
    if (getaddrinfo(argv[1], NULL, &addr_hints_server, &addr_server_result) != 0)
        syserr("getaddrinfo server");

    init_hints(addr_hints_gui);
    if (getaddrinfo(gui_addr_arg, NULL, &addr_hints_gui, &addr_gui_result) != 0)
        syserr("getaddrinfo gui");

    server_address.sin_family = AF_INET6; // IPv6
    server_address.sin_addr.s_addr =
            ((struct sockaddr_in*) (addr_server_result->ai_addr))->sin_addr.s_addr;
    server_address.sin_port = htons(server_port);

    freeaddrinfo(addr_server_result);

    gui_address.sin_family = AF_INET6; // IPv6
    gui_address.sin_addr.s_addr =
            ((struct sockaddr_in*) (addr_gui_result->ai_addr))->sin_addr.s_addr;
    gui_address.sin_port = htons(gui_port);

    freeaddrinfo(addr_gui_result);
    
    std::cout << " sport: " << server_port << " gport: " << gui_port << std::endl;
    std::cout << "name: " << player_name << " serverport: " << server_address.sin_port
    << " guiport: " << gui_address.sin_port << std::endl;
    std::cout << " serveraddr: " << server_address.sin_addr.s_addr
                       << " guiaddr: " << gui_address.sin_addr.s_addr << std::endl;

    server_sock = socket(PF_INET6, SOCK_DGRAM, 0);
    gui_sock = socket(PF_INET6, SOCK_STREAM, 0);

    sendto(server_sock, player_name.c_str(), player_name.length(), 0,
           (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address));

    return 123;
};