#include "commands.h"
#include "store.h"
#include "resp_parser.h"
#include <sys/socket.h>
#include <mutex>
#include <chrono>

void handle_command(int client_fd, std::vector<std::string> &parsed)
{
    if (parsed[0] == "PING")
    {
        send(client_fd, "+PONG\r\n", 7, 0);
    }
    else if (parsed[0] == "ECHO")
    {
        std::string response = build_bulk_string(parsed[1]);
        send(client_fd, response.c_str(), response.length(), 0);
    }
    else if (parsed[0] == "SET")
    {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        if (kv_store.count(parsed[1]) && !std::holds_alternative<std::string>(kv_store[parsed[1]]))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }

        long long expiry_time = 0;
        if (parsed.size() > 3)
        {
            if (parsed[3] == "PX")
            {
                expiry_time = now + std::stoll(parsed[4]);
                kv_store_expiry[parsed[1]] = expiry_time;
            }
            else if (parsed[3] == "EX")
            {
                expiry_time = now + std::stoll(parsed[4]) * 1000;
                kv_store_expiry[parsed[1]] = expiry_time;
            }
        }
        else if (kv_store_expiry.count(parsed[1]))
        {
            kv_store_expiry.erase(parsed[1]);
        }
        kv_store[parsed[1]] = std::string(parsed[2]);
        send(client_fd, "+OK\r\n", 5, 0);
    }
    else if (parsed[0] == "GET")
    {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (!kv_store.count(parsed[1]) || (kv_store.count(parsed[1])) && (!std::holds_alternative<std::string>(kv_store[parsed[1]])))
        {
            send(client_fd, "$-1\r\n", 5, 0);
        }
        else
        {
            if (kv_store_expiry.count(parsed[1]) && kv_store_expiry[parsed[1]] < now)
            {
                kv_store.erase(parsed[1]);
                kv_store_expiry.erase(parsed[1]);
                send(client_fd, "$-1\r\n", 5, 0);
                return;
            }

            std::string value = std::get<std::string>(kv_store[parsed[1]]);
            std::string response = build_bulk_string(value);
            send(client_fd, response.c_str(), response.length(), 0);
        }
    }
    else if (parsed[0] == "RPUSH")
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (kv_store.count(parsed[1]) && std::holds_alternative<std::string>(kv_store[parsed[1]]))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }
        if (!kv_store.count(parsed[1]))
        {
            kv_store[parsed[1]] = std::deque<std::string>();
        }

        for (int i = 2; i < parsed.size(); i++)
        {
            std::get<std::deque<std::string>>(kv_store[parsed[1]]).push_back(parsed[i]);
            std::string response = ":" + std::to_string(std::get<std::deque<std::string>>(kv_store[parsed[1]]).size()) + "\r\n";
            send(client_fd, response.c_str(), response.length(), 0);
        }
        }
}