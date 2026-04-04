#include "store.h"
#include <variant>
#include <deque>

std::mutex kv_store_mutex;
std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>>> kv_store;
std::unordered_map<std::string, long long> kv_store_expiry;