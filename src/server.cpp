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

std::string build_bulk_string(const std::string &str)
{
    int n = str.size();
    return "$" + std::to_string(n) + "\r\n" + str + "\r\n";
}

/**
 * @brief Handles a client connection
 *
 * @param client_fd
 */

void handle_client(int client_fd, std::string initial_data)
{
    char buffer[4096];
    std::string accumulated = initial_data;
    std::cout << "Initial data: " << initial_data << "\n";

    int bytes;
    while ((bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
    {
        accumulated += std::string(buffer, bytes);
        std::cout << "ACCUMULATED: [";
        for (char c : accumulated)
        {
            if (c == '\r')
                std::cout << "\\r";
            else if (c == '\n')
                std::cout << "\\n";
            else
                std::cout << c;
        }
        std::cout << "]\n";

        std::cout << "FIRST CHAR: " << accumulated[0] << std::endl;
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
            if (client_fd == master_fd && message.find("GETACK") == std::string::npos)
            {
                byte_counter += message.size();
                std::cout << "Received " << message.size() << " bytes from master, total: " << byte_counter << std::endl;
            }
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

std::string build_array_response(std::vector<std::string> &keys, std::vector<std::string> &ids)
{
    std::string outer = "*" + std::to_string(keys.size()) + "\r\n";
    int p = 0; // for two pointer
    while (p < keys.size())
    {
        std::string key = keys[p];
        std::string id = ids[p];

        std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator> &stream = std::get<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[key]);

        auto it = stream.upper_bound(id);

        // Build entries first
        std::string entries = "";
        int entry_count = 0;

        while (it != stream.end())
        {
            std::string entry = "*2\r\n";
            entry += build_bulk_string(it->first);

            std::string fields = "*" + std::to_string(it->second.size() * 2) + "\r\n";
            for (auto &kv : it->second)
            {
                fields += build_bulk_string(kv.first);
                fields += build_bulk_string(kv.second);
            }
            entry += fields;
            entries += entry;
            entry_count++;
            it++;
        }

        // Wrap: [key, array-of-entries]
        std::string stream_block = "*2\r\n";
        stream_block += build_bulk_string(key);
        stream_block += "*" + std::to_string(entry_count) + "\r\n";
        stream_block += entries;

        outer += stream_block;
        p++;
    }
    return outer;
}