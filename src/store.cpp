#include "store.h"

std::mutex kv_store_mutex;
std::unordered_map<std::string, std::string> kv_store;
std::unordered_map<std::string, long long> kv_store_expiry;