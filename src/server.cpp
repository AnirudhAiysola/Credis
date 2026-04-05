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
    char buffer[4096];
    std::string accumulated;

    int bytes;
    while ((bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
    {
        accumulated += std::string(buffer, bytes);

        // keep parsing complete messages from accumulated buffer
        while (true)
        {
            // need at least a * and a number
            if (accumulated.empty() || accumulated[0] != '*')
                break;

            // find how many elements
            size_t first_crlf = accumulated.find("\r\n");
            if (first_crlf == std::string::npos)
                break;

            int num_elements = std::stoi(accumulated.substr(1, first_crlf - 1));

            // count \r\n occurrences - need 2*N+1 for a complete message
            int crlf_count = 0;
            size_t pos = 0;
            size_t end = std::string::npos;
            while ((pos = accumulated.find("\r\n", pos)) != std::string::npos)
            {
                crlf_count++;
                pos += 2;
                if (crlf_count == 2 * num_elements + 1)
                {
                    end = pos;
                    break;
                }
            }

            if (end == std::string::npos)
                break; // incomplete message

            // extract one complete message
            std::string message = accumulated.substr(0, end);
            accumulated = accumulated.substr(end); // keep the rest

            std::vector<std::string> parsed = parse_resp(message);
            if (parsed.empty())
                continue;

            std::transform(parsed[0].begin(), parsed[0].end(), parsed[0].begin(), ::toupper);
            if (parsed[0] == "SET" && parsed.size() > 3)
                std::transform(parsed[3].begin(), parsed[3].end(), parsed[3].begin(), ::toupper);

            handle_command(client_fd, parsed);
        }
    }
    close(client_fd);
}