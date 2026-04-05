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
        std::cout << "RAW: [";
        for (char c : input)
        {
            if (c == '\r')
                std::cout << "\\r";
            else if (c == '\n')
                std::cout << "\\n";
            else
                std::cout << c;
        }
        std::cout << "]\n";
        std::vector<std::string> parsed = parse_resp(input);
        std::cout << "PARSED COUNT: " << parsed.size() << "\n";
        for (auto &s : parsed)
            std::cout << "PARSED: [" << s << "]\n";
        std::transform(parsed[0].begin(), parsed[0].end(), parsed[0].begin(), ::toupper); // Convert command to uppercase for case-insensitive comparison
        if (parsed[0] == "SET" && parsed.size() > 3)
        {
            std::transform(parsed[3].begin(), parsed[3].end(), parsed[3].begin(), ::toupper); // Convert expiry to uppercase for case-insensitive comparison
        }
        handle_command(client_fd, parsed);
    }
    close(client_fd);
}
