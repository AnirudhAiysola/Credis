#include "commands.h"
#include "store.h"
#include "server.h"
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back("+PONG\r\n");
            return;
        }
        if (!isReplica)
            send(client_fd, "+PONG\r\n", 7, 0);
    };

    m["ECHO"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::string response = build_bulk_string(parsed[1]);
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
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
            if (!isReplica)
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back("+OK\r\n");
            return;
        }
        for (int i = 0; i < replica_fds.size(); i++)
        {
            int replica_fd = replica_fds[i];
            std::string command = "*" + std::to_string(parsed.size()) + "\r\n";
            for (const std::string &arg : parsed)
            {
                command += build_bulk_string(arg);
            }
            send(replica_fd, command.c_str(), command.length(), 0);
        }

        if (!isReplica)
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
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

        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
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
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
        send(client_fd, response.c_str(), response.length(), 0);
    };

    m["LPOP"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        std::string response;

        auto it = kv_store.find(parsed[1]);
        if (it == kv_store.end())
        {
            response = "$-1\r\n";
        }
        else if (!std::holds_alternative<std::deque<std::string>>(it->second))
        {
            response = "-ERR Operation against a key holding the wrong kind of value\r\n";
        }
        else
        {
            std::deque<std::string> &dq = std::get<std::deque<std::string>>(it->second);
            if (dq.empty())
            {
                response = "$-1\r\n";
            }
            else if (parsed.size() == 2)
            {
                std::string value = dq.front();
                dq.pop_front();
                response = build_bulk_string(value);
            }
            else
            {
                int n = dq.size();
                int count = std::stoi(parsed[2]);
                if (count > n)
                    count = n;
                response = "*" + std::to_string(count) + "\r\n";
                while (count--)
                {
                    std::string value = dq.front();
                    dq.pop_front();
                    response += build_bulk_string(value);
                }
            }
        }

        if (inTransaction[client_fd])
            transaction_responses[client_fd].push_back(response);
        else
            send(client_fd, response.c_str(), response.size(), 0);
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
        // if (inTransaction[client_fd])
        // {
        //     transaction_responses[client_fd].push_back(response);
        //     return;
        // }
        send(client_fd, response.c_str(), response.length(), 0);
    };
    m["TYPE"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        std::string response;
        if (!kv_store.count(parsed[1]))
            response = "+none\r\n";
        else if (std::holds_alternative<std::string>(kv_store[parsed[1]]))
            response = "+string\r\n";
        else if (std::holds_alternative<std::deque<std::string>>(kv_store[parsed[1]]))
            response = "+list\r\n";
        else if (std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[parsed[1]]))
            response = "+stream\r\n";

        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
        else
        {
            send(client_fd, response.c_str(), response.length(), 0);
        }
    };
    m["XADD"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        std::transform(parsed[0].begin(), parsed[0].end(), parsed[0].begin(), ::toupper);

        if (kv_store.count(parsed[1]) && !std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[parsed[1]]))
        {
            send(client_fd, "-ERR Operation against a key holding the wrong kind of value\r\n", 63, 0);
            return;
        }

        if (!kv_store.count(parsed[1]))
        {
            kv_store[parsed[1]] = std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>();
        }
        std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator> &stream = std::get<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[parsed[1]]);

        if (parsed[2] == "0-0")
        {
            send(client_fd, "-ERR The ID specified in XADD must be greater than 0-0\r\n", 56, 0);
            return;
        }

        if (parsed[2].back() != '*')
        {
            long long new_ms = std::stoll(parsed[2].substr(0, parsed[2].find('-')));
            long long new_seq = std::stoll(parsed[2].substr(parsed[2].find('-') + 1));
            if (!stream.empty())
            {
                auto lastEntry = stream.rbegin()->first;
                size_t dash = lastEntry.find('-');
                long long ms = std::stoll(lastEntry.substr(0, dash));
                long long seq = std::stoll(lastEntry.substr(dash + 1));

                if (new_ms < ms || (new_ms == ms && new_seq <= seq))
                {
                    send(client_fd, "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n", 83, 0);
                    return;
                }
            }
        }

        std::string entryId;
        if (parsed[2] == "*")
        {
            long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            long long new_seq = 0;
            if (!stream.empty())
            {
                auto lastEntry = stream.rbegin()->first;
                long long last_ms = std::stoll(lastEntry.substr(0, lastEntry.find('-')));
                long long last_seq = std::stoll(lastEntry.substr(lastEntry.find('-') + 1));
                if (now == last_ms)
                    new_seq = last_seq + 1;
            }
            entryId = std::to_string(now) + "-" + std::to_string(new_seq);
        }
        else if (parsed[2].back() == '*')
        {
            // auto generate sequence
            std::string ms_part = parsed[2].substr(0, parsed[2].find('-'));
            std::cout << "ms_part: " << ms_part << std::endl;
            long long new_ms = std::stoll(ms_part);
            long long new_seq = (new_ms == 0) ? 1 : 0;

            if (!stream.empty())
            {
                auto lastEntry = stream.rbegin()->first;
                size_t dash = lastEntry.find('-');
                long long last_ms = std::stoll(lastEntry.substr(0, dash));
                long long last_seq = std::stoll(lastEntry.substr(dash + 1));
                if (new_ms == last_ms)
                    new_seq = last_seq + 1;
            }
            entryId = ms_part + "-" + std::to_string(new_seq);
        }
        else
        {
            entryId = parsed[2];
        }

        for (int i = 3; i < parsed.size(); i += 2)
        {
            stream[entryId].push_back({parsed[i], parsed[i + 1]});
        }

        std::string response = build_bulk_string(entryId);
        // notify blocked clients
        while (!blocking_clients[parsed[1]].empty())
        {
            blocking_clients[parsed[1]].front()->notify_one();
            blocking_clients[parsed[1]].pop();
        }
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
        send(client_fd, response.c_str(), response.length(), 0);
    };
    m["XRANGE"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        if (!kv_store.count(parsed[1]) || (kv_store.count(parsed[1]) &&
                                           !std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[parsed[1]])))
        {
            send(client_fd, "*0\r\n", 4, 0);
            return;
        }
        std::string start = parsed[2];
        std::string end = parsed[3];

        std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator> &stream = std::get<std::map<std::string,
                                                                                                                              std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[parsed[1]]);

        if (start != "-" && start.find('-') == std::string::npos)
            start += "-0";
        if (end != "+" && end.find('-') == std::string::npos)
            end += "-99999999999";

        auto it = start != "-" ? stream.lower_bound(start) : stream.begin();
        auto it_end = end == "+" ? stream.end() : stream.upper_bound(end);
        std::string entries;

        int count = 0;
        while (it != it_end)
        {
            std::string entry = "*2\r\n";
            entry += build_bulk_string(it->first);

            // field value array
            std::string fields = "*" + std::to_string(it->second.size() * 2) + "\r\n";
            for (auto &kv : it->second)
            {
                fields += build_bulk_string(kv.first);
                fields += build_bulk_string(kv.second);
            }
            entry += fields;
            entries += entry;
            count++;
            it++;
        }
        std::string response = "*" + std::to_string(count) + "\r\n" + entries;
        if (inTransaction[client_fd])
        {
            transaction_responses[client_fd].push_back(response);
            return;
        }
        send(client_fd, response.c_str(), response.size(), 0);
    };
    m["XREAD"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::unique_lock<std::mutex> lock(kv_store_mutex);
        std::transform(parsed[0].begin(), parsed[0].end(), parsed[0].begin(), ::toupper);
        std::transform(parsed[1].begin(), parsed[1].end(), parsed[1].begin(), ::toupper);
        std::vector<std::string> keys, ids;

        int i = parsed[1] == "STREAMS" ? 2 : 4;

        int len = (parsed.size() - i) / 2;
        while (len--)
        {
            keys.push_back(parsed[i]);
            i++;
        }
        while (i < parsed.size())
        {
            ids.push_back(parsed[i]);
            i++;
        }

        for (int i = 0; i < keys.size(); i++)
        {

            if (!kv_store.count(keys[i]) || ((kv_store.count(keys[i]) &&
                                              !std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]]))) &&
                                                parsed[1] == "STREAMS")
            {
                send(client_fd, "*0\r\n", 4, 0);
                return;
            }
        }

        if (parsed[1] == "BLOCK")
        {
            std::vector<std::condition_variable> cv(keys.size());
            for (int i = 0; i < keys.size(); i++)
            {
                if (ids[i] == "$")
                {
                    if (kv_store.count(keys[i]) &&
                        std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]]))
                    {
                        auto &stream = std::get<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]]);
                        ids[i] = stream.empty() ? "" : stream.rbegin()->first;
                    }
                }

                blocking_clients[keys[i]].push(&cv[i]);

                long long timeout_val = std::stoll(parsed[2]);

                if (timeout_val == 0)
                {
                    cv[i].wait(lock, [&]
                               {
                        if(!kv_store.count(keys[i]) || kv_store.count(keys[i]) && !std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]])) {
                            return false;
                        }
                        std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator> &stream = std::get<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]]);
                        auto it = stream.upper_bound(ids[i]);
                        return it != stream.end(); });

                    std::vector<std::string> single_key_vec{keys[i]};
                    std::vector<std::string> single_id_vec{ids[i]};
                    std::string response = build_array_response(single_key_vec, single_id_vec);
                    send(client_fd, response.c_str(), response.size(), 0);
                }
                else
                {
                    bool found = cv[i].wait_for(lock, std::chrono::milliseconds(timeout_val), [&]
                                                {
                    if(!kv_store.count(keys[i]) || kv_store.count(keys[i]) && !std::holds_alternative<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]])) {
                        return false;
                    }
                    std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator> &stream = std::get<std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>(kv_store[keys[i]]);
                    auto it = stream.upper_bound(ids[i]);
                    return it != stream.end(); });
                    if (!found)
                    {
                        send(client_fd, "*-1\r\n", 5, 0);
                    }
                    else
                    {
                        std::vector<std::string> single_key_vec{keys[i]};
                        std::vector<std::string> single_id_vec{ids[i]};
                        std::string response = build_array_response(single_key_vec, single_id_vec);
                        send(client_fd, response.c_str(), response.size(), 0);
                    }
                }
            }
        }
        else
        {
            std::string response = build_array_response(keys, ids);
            send(client_fd, response.c_str(), response.size(), 0);
        }
    };
    m["INCR"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);

        std::string response;

        if (!kv_store.count(parsed[1]))
        {
            kv_store[parsed[1]] = "1";
            response = ":1\r\n";
        }
        else if (!std::holds_alternative<std::string>(kv_store[parsed[1]]))
        {
            response = "-ERR value is not an integer or out of range\r\n";
        }
        else
        {
            try
            {
                long long num = std::stoll(std::get<std::string>(kv_store[parsed[1]]));
                num++;
                kv_store[parsed[1]] = std::to_string(num);
                response = ":" + std::to_string(num) + "\r\n";
            }
            catch (const std::exception &e)
            {
                response = "-ERR value is not an integer or out of range\r\n";
            }
        }

        if (inTransaction[client_fd])
            transaction_responses[client_fd].push_back(response);
        else
            send(client_fd, response.c_str(), response.length(), 0);
    };
    m["MULTI"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (inTransaction[client_fd])
        {
            send(client_fd, "-ERR MULTI calls can not be nested\r\n", 35, 0);
            return;
        }
        inTransaction[client_fd] = true;
        send(client_fd, "+OK\r\n", 5, 0);
        return;
    };
    m["EXEC"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::unique_lock<std::mutex> lock(kv_store_mutex);

        if (!inTransaction[client_fd])
        {
            send(client_fd, "-ERR EXEC without MULTI\r\n", 25, 0);
            return;
        }
        if (transaction_commands[client_fd].empty())
        {
            send(client_fd, "*0\r\n", 4, 0);
            inTransaction.erase(client_fd);
            transaction_commands.erase(client_fd);
            return;
        }
        // command_map[parsed[0]](client_fd, parsed)
        lock.unlock();
        while (!transaction_commands[client_fd].empty())
        {
            std::vector<std::string> v = transaction_commands[client_fd].front();
            transaction_commands[client_fd].pop();
            command_map[v[0]](client_fd, v);
        }
        std::string outer = "*" + std::to_string(transaction_responses[client_fd].size()) + "\r\n";
        for (auto &r : transaction_responses[client_fd])
            outer += r;

        send(client_fd, outer.c_str(), outer.size(), 0);

        inTransaction.erase(client_fd);
        transaction_commands.erase(client_fd);
        transaction_responses.erase(client_fd);
    };
    m["DISCARD"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::lock_guard<std::mutex> lock(kv_store_mutex);
        if (!inTransaction[client_fd])
        {
            std::string response = "-ERR DISCARD without MULTI\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
            return;
        }
        inTransaction.erase(client_fd);
        if (transaction_commands.count(client_fd))
            transaction_commands.erase(client_fd);
        if (transaction_responses.count(client_fd))
            transaction_responses.erase(client_fd);
        send(client_fd, "+OK\r\n", 5, 0);
    };
    m["INFO"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::string content = "";
        if (!isReplica)
            content += "role:master\r\n";
        else
            content += "role:slave\r\n";
        content += "master_replid:8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb\r\n";
        content += "master_repl_offset:0\r\n";

        std::string response = build_bulk_string(content);
        send(client_fd, response.c_str(), response.size(), 0);
    };
    m["REPLCONF"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::transform(parsed[1].begin(), parsed[1].end(), parsed[1].begin(), ::toupper);
        if (isReplica && parsed.size() >= 3 && parsed[1] == "GETACK")
        {
            std::string offset = std::to_string(byte_counter);
            std::string response = "*3\r\n$8\r\nREPLCONF\r\n$3\r\nACK\r\n$" +
                                   std::to_string(offset.size()) + "\r\n" + offset + "\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        }
        else
        {
            std::string response = "+OK\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        }
    };
    m["PSYNC"] = [](int client_fd, std::vector<std::string> &parsed)
    {
        std::string hex = "524544495330303131fa0972656469732d76657205372e322e30fa0a72656469732d62697473c040fa056374696d65c26d08bc65fa08757365642d6d656dc2b0c41000fa08616f662d62617365c000fff06e3bfec0ff5aa2";
        std::string bytes;
        for (int i = 0; i < hex.size(); i += 2)
        {
            bytes += (char)std::stoi(hex.substr(i, 2), nullptr, 16);
        }
        std::string response = "+FULLRESYNC 8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb 0\r\n";
        send(client_fd, response.c_str(), response.size(), 0);

        std::string byteResponse = "$" + std::to_string(bytes.size()) + "\r\n" + bytes;
        send(client_fd, byteResponse.c_str(), byteResponse.size(), 0);
        replica_fds.push_back(client_fd);
    };

    return m;
}();

void handle_command(int client_fd, std::vector<std::string> &parsed)
{
    if (inTransaction[client_fd] && parsed[0] != "EXEC" && parsed[0] != "DISCARD" && parsed[0] != "MULTI")
    {
        transaction_commands[client_fd].push(parsed);
        send(client_fd, "+QUEUED\r\n", 9, 0);
        return;
    }
    auto it = command_map.find(parsed[0]);
    if (it != command_map.end())
        it->second(client_fd, parsed);
    else
        send(client_fd, "-ERR unknown command\r\n", 22, 0);
}

// // cmake --build build && ./build/redis