#include "server.h"
#include "store.h"
#include "resp_parser.h"
#include "commands.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>

/**
 * @brief Builds a RESP bulk string response
 *
 * @param str
 * @return std::string
 */

std::string build_bulk_string(std::string &str)
{
    int n = str.size();
    return "$" + std::to_string(n) + "\r\n" + str + "\r\n";
}

/**
 * @brief Handles a client connection
 *
 * @param client_fd
 */

void handle_client(int client_fd)
{
    char buffer[1024];
    while (recv(client_fd, buffer, sizeof(buffer), 0) > 0)
    {
        std::string input(buffer);
        std::vector<std::string> parsed = parse_resp(input);
        std::transform(parsed[0].begin(), parsed[0].end(), parsed[0].begin(), ::toupper); // Convert command to uppercase for case-insensitive comparison
        std::transform(parsed[2].begin(), parsed[2].end(), parsed[2].begin(), ::toupper); // Convert expiry to uppercase for case-insensitive comparison
        handle_command(client_fd, parsed);
    }
    close(client_fd);
}
