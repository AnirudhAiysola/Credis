#include "commands.h"
#include "store.h"
#include "resp_parser.h"
#include <sys/socket.h>
#include <mutex>
#include <chrono>
#include <iostream>

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
        if (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
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
        }
        std::string response = ":" + std::to_string(std::get<std::deque<std::string>>(kv_store[parsed[1]]).size()) + "\r\n";
        send(client_fd, response.c_str(), response.length(), 0);
    }
    else if (parsed[0] == "LRANGE")
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (!kv_store.count(parsed[1]) || (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]])))
        {
            send(client_fd, "*0\r\n", 4, 0);
            return;
        }
        std::cout << "LRANGE parsed size: " << parsed.size() << "\n";
        for (auto &s : parsed)
            std::cout << "[" << s << "]\n";
        int L = std::stoi(parsed[2]);
        int R = std::stoi(parsed[3]);
        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        int n = dq.size();
        if (L < 0)
            L = n + L;
        if (R < 0)
            R = n + R;
        if (L < 0)
            L = 0;
        if (R < 0)
        {
            R = 0;
        }
        if (L >= n)
            L = n - 1;
        if (R >= n)
            R = n - 1;
        if (L > R)
        {
            send(client_fd, "*0\r\n", 4, 0);
            return;
        }

        std::string response = "*" + std::to_string(R - L + 1) + "\r\n";
        for (int i = L; i <= R; i++)
        {
            response += build_bulk_string(dq[i]);
        }
        send(client_fd, response.c_str(), response.length(), 0);
    }
    else if (parsed[0] == "LPUSH")
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        if (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }
        if (!kv_store.count(parsed[1]))
        {
            kv_store[parsed[1]] = std::deque<std::string>();
        }
        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        for (int i = 2; i < parsed.size(); i++)
        {
            dq.push_front(parsed[i]);
        }
        std::string response = ":" + std::to_string(dq.size()) + "\r\n";
        send(client_fd, response.c_str(), response.length(), 0);
    }
    else if (parsed[0] == "LLEN")
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (!kv_store.count(parsed[1]))
        {
            send(client_fd, ":0\r\n", 4, 0);
            return;
        }
        if ((kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]])))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }
        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        std::string response = ":" + std::to_string(dq.size()) + "\r\n";
        send(client_fd, response.c_str(), response.length(), 0);
    }
}