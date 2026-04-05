// #include "commands.h"
// #include "store.h"
// #include "resp_parser.h"
// #include <sys/socket.h>
// #include <mutex>
// #include <chrono>
// #include <iostream>

// static std::unordered_map<std::string, CommandHandler> command_map;

// void handle_command(int client_fd, std::vector<std::string> &parsed)
// {
//     if (parsed[0] == "PING")
//     {
//         send(client_fd, "+PONG\r\n", 7, 0);
//     }
//     else if (parsed[0] == "ECHO")
//     {
//         std::string response = build_bulk_string(parsed[1]);
//         send(client_fd, response.c_str(), response.length(), 0);
//     }
//     else if (parsed[0] == "SET")
//     {
//         long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
//                             std::chrono::system_clock::now().time_since_epoch())
//                             .count();
//         std::lock_guard<std::mutex> lock(kv_store_mutex);

//         if (kv_store.count(parsed[1]) && !std::holds_alternative<std::string>(kv_store[parsed[1]]))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }

//         long long expiry_time = 0;
//         if (parsed.size() > 3)
//         {
//             if (parsed[3] == "PX")
//             {
//                 expiry_time = now + std::stoll(parsed[4]);
//                 kv_store_expiry[parsed[1]] = expiry_time;
//             }
//             else if (parsed[3] == "EX")
//             {
//                 expiry_time = now + std::stoll(parsed[4]) * 1000;
//                 kv_store_expiry[parsed[1]] = expiry_time;
//             }
//         }
//         else if (kv_store_expiry.count(parsed[1]))
//         {
//             kv_store_expiry.erase(parsed[1]);
//         }
//         kv_store[parsed[1]] = std::string(parsed[2]);
//         send(client_fd, "+OK\r\n", 5, 0);
//     }
//     else if (parsed[0] == "GET")
//     {
//         long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
//                             std::chrono::system_clock::now().time_since_epoch())
//                             .count();
//         std::lock_guard<std::mutex> lock(kv_store_mutex);
//         if (!kv_store.count(parsed[1]) || (kv_store.count(parsed[1])) && (!std::holds_alternative<std::string>(kv_store[parsed[1]])))
//         {
//             send(client_fd, "$-1\r\n", 5, 0);
//         }
//         else
//         {
//             if (kv_store_expiry.count(parsed[1]) && kv_store_expiry[parsed[1]] < now)
//             {
//                 kv_store.erase(parsed[1]);
//                 kv_store_expiry.erase(parsed[1]);
//                 send(client_fd, "$-1\r\n", 5, 0);
//                 return;
//             }

//             std::string value = std::get<std::string>(kv_store[parsed[1]]);
//             std::string response = build_bulk_string(value);
//             send(client_fd, response.c_str(), response.length(), 0);
//         }
//     }
//     else if (parsed[0] == "RPUSH")
//     {
//         std::lock_guard<std::mutex> lock(kv_store_mutex);
//         if (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }
//         if (!kv_store.count(parsed[1]))
//         {
//             kv_store[parsed[1]] = std::deque<std::string>();
//         }

//         for (int i = 2; i < parsed.size(); i++)
//         {
//             std::get<std::deque<std::string>>(kv_store[parsed[1]]).push_back(parsed[i]);
//         }
//         std::string response = ":" + std::to_string(std::get<std::deque<std::string>>(kv_store[parsed[1]]).size()) + "\r\n";
//         // notify one waiting client if exists
//         if (!waiting_clients[parsed[1]].empty())
//         {
//             waiting_clients[parsed[1]].front()->notify_one();
//             waiting_clients[parsed[1]].pop();
//         }
//         send(client_fd, response.c_str(), response.length(), 0);
//     }
//     else if (parsed[0] == "LRANGE")
//     {
//         std::lock_guard<std::mutex> lock(kv_store_mutex);
//         if (!kv_store.count(parsed[1]) || (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]])))
//         {
//             send(client_fd, "*0\r\n", 4, 0);
//             return;
//         }
//         std::cout << "LRANGE parsed size: " << parsed.size() << "\n";
//         for (auto &s : parsed)
//             std::cout << "[" << s << "]\n";
//         int L = std::stoi(parsed[2]);
//         int R = std::stoi(parsed[3]);
//         std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
//         int n = dq.size();
//         if (L < 0)
//             L = n + L;
//         if (R < 0)
//             R = n + R;
//         if (L < 0)
//             L = 0;
//         if (R < 0)
//         {
//             R = 0;
//         }
//         if (L >= n)
//             L = n - 1;
//         if (R >= n)
//             R = n - 1;
//         if (L > R)
//         {
//             send(client_fd, "*0\r\n", 4, 0);
//             return;
//         }

//         std::string response = "*" + std::to_string(R - L + 1) + "\r\n";
//         for (int i = L; i <= R; i++)
//         {
//             response += build_bulk_string(dq[i]);
//         }
//         send(client_fd, response.c_str(), response.length(), 0);
//     }
//     else if (parsed[0] == "LPUSH")
//     {
//         std::lock_guard<std::mutex> lock(kv_store_mutex);

//         if (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }
//         if (!kv_store.count(parsed[1]))
//         {
//             kv_store[parsed[1]] = std::deque<std::string>();
//         }
//         std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
//         for (int i = 2; i < parsed.size(); i++)
//         {
//             dq.push_front(parsed[i]);
//         }
//         std::string response = ":" + std::to_string(dq.size()) + "\r\n";
//         // notify one waiting client if exists
//         if (!waiting_clients[parsed[1]].empty())
//         {
//             waiting_clients[parsed[1]].front()->notify_one();
//             waiting_clients[parsed[1]].pop();
//         }
//         send(client_fd, response.c_str(), response.length(), 0);
//     }
//     else if (parsed[0] == "LLEN")
//     {
//         std::lock_guard<std::mutex> lock(kv_store_mutex);
//         if (!kv_store.count(parsed[1]))
//         {
//             send(client_fd, ":0\r\n", 4, 0);
//             return;
//         }
//         if ((kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]])))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }
//         std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
//         std::string response = ":" + std::to_string(dq.size()) + "\r\n";
//         send(client_fd, response.c_str(), response.length(), 0);
//     }
//     else if (parsed[0] == "LPOP")
//     {
//         std::lock_guard<std::mutex> lock(kv_store_mutex);
//         if (!kv_store.count(parsed[1]))
//         {
//             send(client_fd, "$-1\r\n", 5, 0);
//             return;
//         }
//         if ((kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]])))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }
//         std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
//         if (dq.empty())
//         {
//             send(client_fd, "$-1\r\n", 5, 0);
//             return;
//         }
//         if (parsed.size() == 2)
//         {
//             // no count argument - return plain bulk string
//             std::string value = dq.front();
//             dq.pop_front();
//             std::string response = build_bulk_string(value);
//             send(client_fd, response.c_str(), response.length(), 0);
//         }
//         else
//         {
//             // count argument - return array
//             int n = dq.size();
//             int count = std::stoi(parsed[2]);
//             if (count > n)
//                 count = n;

//             std::string response = "*" + std::to_string(count) + "\r\n";
//             while (count--)
//             {
//                 std::string value = dq.front();
//                 dq.pop_front();
//                 response += build_bulk_string(value);
//             }
//             send(client_fd, response.c_str(), response.length(), 0);
//         }
//     }
//     else if (parsed[0] == "BLPOP")
//     {
//         std::unique_lock<std::mutex> lock(kv_store_mutex);

//         if (kv_store.count(parsed[1]) && !std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
//         {
//             send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
//             return;
//         }
//         else if (kv_store.count(parsed[1]) && std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]) && !std::get<std::deque<std::string>>(kv_store[parsed[1]]).empty())
//         {
//             std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);

//             std::string value = dq.front();
//             dq.pop_front();
//             std::string response = "*" + std::to_string(2) + "\r\n" + build_bulk_string(parsed[1]) + build_bulk_string(value);
//             send(client_fd, response.c_str(), response.length(), 0);
//             return;
//         }
//         // wait for an element to be pushed or timeoutelse
//         else
//         {
//             std::condition_variable cv;
//             waiting_clients[parsed[1]].push(&cv);
//             double timeout_val = std::stod(parsed[2]);

//             if (timeout_val == 0)
//             {
//                 cv.wait(lock, [&]
//                         { return kv_store.count(parsed[1]) &&
//                                  std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]) &&
//                                  !std::get<std::deque<std::string>>(kv_store[parsed[1]]).empty(); });
//             }
//             else
//             {
//                 auto timeout = std::chrono::duration<double>(timeout_val);
//                 bool found = cv.wait_for(lock, timeout, [&]
//                                          { return kv_store.count(parsed[1]) &&
//                                                   std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]) &&
//                                                   !std::get<std::deque<std::string>>(kv_store[parsed[1]]).empty(); });
//                 if (!found)
//                 {
//                     send(client_fd, "*-1\r\n", 5, 0);
//                     return;
//                 }
//             }

//             // both paths reach here with element available
//             std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
//             std::string value = dq.front();
//             dq.pop_front();
//             std::string response = "*2\r\n" + build_bulk_string(parsed[1]) + build_bulk_string(value);
//             send(client_fd, response.c_str(), response.length(), 0);
//         }
//     }
// }

#include "commands.h"
#include "store.h"
#include "resp_parser.h"
#include <sys/socket.h>
#include <mutex>
#include <chrono>
#include <iostream>

static std::unordered_map<std::string, CommandHandler> command_map = []()
{
    std::unordered_map<std::string, CommandHandler> m;

    m["PING"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        send(client_fd, "+PONG\r\n", 7, 0);
    };

    m["ECHO"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::string response = build_bulk_string(parsed[1]);
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["SET"] = [](int client_fd, std::vector<std::string> &parsed)
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

        if (parsed.size() > 3)
        {
            if (parsed[3] == "PX")
                kv_store_expiry[parsed[1]] = now + std::stoll(parsed[4]);
            else if (parsed[3] == "EX")
                kv_store_expiry[parsed[1]] = now + std::stoll(parsed[4]) * 1000;
        }
        else if (kv_store_expiry.count(parsed[1]))
        {
            kv_store_expiry.erase(parsed[1]);
        }
        kv_store[parsed[1]] = std::string(parsed[2]);
        send(client_fd, "+OK\r\n", 5, 0);
    };

    m["GET"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it == kv_store.end() || !std::holds_alternative<std::string>(it->second))
        {
            send(client_fd, "$-1\r\n", 5, 0);
            return;
        }
        if (kv_store_expiry.count(parsed[1]) && kv_store_expiry[parsed[1]] < now)
        {
            kv_store.erase(parsed[1]);
            kv_store_expiry.erase(parsed[1]);
            send(client_fd, "$-1\r\n", 5, 0);
            return;
        }
        std::string value = std::get<std::string>(it->second);
        std::string response = build_bulk_string(value);
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["RPUSH"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it != kv_store.end() && !std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }
        if (it == kv_store.end())
            kv_store[parsed[1]] = std::deque<std::string>();

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        for (int i = 2; i < (int)parsed.size(); i++)
            dq.push_back(parsed[i]);

        std::string response = ":" + std::to_string(dq.size()) + "\r\n";

        if (!waiting_clients[parsed[1]].empty())
        {
            waiting_clients[parsed[1]].front()->notify_one();
            waiting_clients[parsed[1]].pop();
        }
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["LPUSH"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it != kv_store.end() && !std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }
        if (it == kv_store.end())
            kv_store[parsed[1]] = std::deque<std::string>();

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        for (int i = 2; i < (int)parsed.size(); i++)
            dq.push_front(parsed[i]);

        std::string response = ":" + std::to_string(dq.size()) + "\r\n";

        if (!waiting_clients[parsed[1]].empty())
        {
            waiting_clients[parsed[1]].front()->notify_one();
            waiting_clients[parsed[1]].pop();
        }
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["LRANGE"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it == kv_store.end() || !std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "*0\r\n", 4, 0);
            return;
        }

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(it->second);
        int n = dq.size();
        int L = std::stoi(parsed[2]);
        int R = std::stoi(parsed[3]);

        if (L < 0)
            L = n + L;
        if (R < 0)
            R = n + R;
        if (L < 0)
            L = 0;
        if (R < 0)
            R = 0;
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
            response += build_bulk_string(dq[i]);

        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["LLEN"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it == kv_store.end())
        {
            send(client_fd, ":0\r\n", 4, 0);
            return;
        }
        if (!std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(it->second);
        std::string response = ":" + std::to_string(dq.size()) + "\r\n";
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["LPOP"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it == kv_store.end())
        {
            send(client_fd, "$-1\r\n", 5, 0);
            return;
        }
        if (!std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(it->second);
        if (dq.empty())
        {
            send(client_fd, "$-1\r\n", 5, 0);
            return;
        }

        if (parsed.size() == 2)
        {
            std::string value = dq.front();
            dq.pop_front();
            std::string response = build_bulk_string(value);
            send(client_fd, response.c_str(), response.length(), 0);
        }
        else
        {
            int n = dq.size();
            int count = std::stoi(parsed[2]);
            if (count > n)
                count = n;

            std::string response = "*" + std::to_string(count) + "\r\n";
            while (count--)
            {
                std::string value = dq.front();
                dq.pop_front();
                response += build_bulk_string(value);
            }
            send(client_fd, response.c_str(), response.length(), 0);
        }
    };

    m["BLPOP"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::unique_lock<std::mutex> lock(kv_store_mutex);

        auto it = kv_store.find(parsed[1]);
        if (it != kv_store.end() && !std::holds_alternative<std::deque<std::string>>(it->second))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }

        if (it != kv_store.end() &&
            std::holds_alternative<std::deque<std::string>>(it->second) &&
            !std::get<std::deque<std::string>>(it->second).empty())
        {
            std::deque<std::string> &dq = std::get<std::deque<std::string>>(it->second);
            std::string value = dq.front();
            dq.pop_front();
            std::string response = "*2\r\n" + build_bulk_string(parsed[1]) + build_bulk_string(value);
            send(client_fd, response.c_str(), response.length(), 0);
            return;
        }

        std::condition_variable cv;
        waiting_clients[parsed[1]].push(&cv);
        double timeout_val = std::stod(parsed[2]);

        if (timeout_val == 0)
        {
            cv.wait(lock, [&]
                    { return kv_store.count(parsed[1]) &&
                             std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]) &&
                             !std::get<std::deque<std::string>>(kv_store[parsed[1]]).empty(); });
        }
        else
        {
            bool found = cv.wait_for(lock, std::chrono::duration<double>(timeout_val), [&]
                                     { return kv_store.count(parsed[1]) &&
                                              std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]) &&
                                              !std::get<std::deque<std::string>>(kv_store[parsed[1]]).empty(); });
            if (!found)
            {
                send(client_fd, "*-1\r\n", 5, 0);
                return;
            }
        }

        std::deque<std::string> &dq = std::get<std::deque<std::string>>(kv_store[parsed[1]]);
        std::string value = dq.front();
        dq.pop_front();
        std::string response = "*2\r\n" + build_bulk_string(parsed[1]) + build_bulk_string(value);
        send(client_fd, response.c_str(), response.length(), 0);
    };
    m["TYPE"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        if (!kv_store.count(parsed[1]))
        {
            send(client_fd, "+none\r\n", 7, 0);
            return;
        }
        if (std::holds_alternative<std::string>(kv_store[parsed[1]]))
            send(client_fd, "+string\r\n", 9, 0);
        else
            send(client_fd, "+list\r\n", 7, 0);
    };

    return m;
}();

void handle_command(int client_fd, std::vector<std::string> &parsed)
{
    auto it = command_map.find(parsed[0]);
    if (it != command_map.end())
        it->second(client_fd, parsed);
    else
        send(client_fd, "-ERR unknown command\r\n", 22, 0);
}

// // cmake --build build && ./build/redis