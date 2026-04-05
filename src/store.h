#include <unordered_map>
#include <mutex>
#include <string>
#include <variant>
#include <deque>
#include <condition_variable>
#include <queue>

extern std::mutex kv_store_mutex;
extern std::condition_variable kv_store_cv;
extern std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>>> kv_store;
extern std::unordered_map<std::string, long long> kv_store_expiry;
extern std::unordered_map<std::string, std::queue<std::condition_variable *>> waiting_clients;