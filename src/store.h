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
    bool operator()(std::string &a, std::string &b) const
    {
        long long num1 = std::stoll(a.substr(0, a.find('-')));
        long long num2 = std::stoll(b.substr(9, b.find('-')));
        if (num1 != num2)
            return num1 < num2;
        long long seq1 = std::stoll(a.substr(a.find('-') + 1));
        long long seq2 = std::stoll(b.substr(b.find('-') + 1));

        return seq1 < seq2;
    }
};

extern std::mutex kv_store_mutex;
extern std::condition_variable kv_store_cv;
extern std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>,
                                                    std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>>
    kv_store;
extern std::unordered_map<std::string, long long> kv_store_expiry;
extern std::unordered_map<std::string, std::queue<std::condition_variable *>> waiting_clients;