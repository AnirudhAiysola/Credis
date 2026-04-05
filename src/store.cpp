#include "store.h"
#include <variant>
#include <deque>
#include <condition_variable>
#include <unordered_map>

std::mutex kv_store_mutex;
std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>>> kv_store;
std::unordered_map<std::string, long long> kv_store_expiry;
std::condition_variable kv_store_cv;
std::unordered_map<std::string, std::queue<std::condition_variable *>> waiting_clients;