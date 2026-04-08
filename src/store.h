#include <unordered_map>
#include <mutex>
#include <string>
#include <variant>
#include <deque>
#include <condition_variable>
#include <queue>
#include <map>
#include <utility>

struct StreamComparator
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        long long ms_a = std::stoll(a.substr(0, a.find('-')));
        long long ms_b = std::stoll(b.substr(0, b.find('-')));
        if (ms_a != ms_b)
            return ms_a < ms_b;
        long long seq_a = std::stoll(a.substr(a.find('-') + 1));
        long long seq_b = std::stoll(b.substr(b.find('-') + 1));
        return seq_a < seq_b;
    }
};

extern std::mutex kv_store_mutex;
extern std::condition_variable kv_store_cv;
extern std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>,
                                                    std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>>
    kv_store;
extern std::unordered_map<std::string, long long> kv_store_expiry;
extern std::unordered_map<std::string, std::queue<std::condition_variable *>> waiting_clients;
extern std::unordered_map<std::string, std::queue<std::condition_variable *>> blocking_clients;
extern std::unordered_map<int, bool> inTransaction;
extern std::unordered_map<int, std::queue<std::vector<std::string>>> transaction_commands;
extern std::unordered_map<int, std::vector<std::string>> transaction_responses;
extern bool isReplica = false;