#include <unordered_map>
#include <mutex>
#include <string>
#include <variant>
#include <deque>

extern std::mutex kv_store_mutex;
extern std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>>> kv_store;
extern std::unordered_map<std::string, long long> kv_store_expiry;