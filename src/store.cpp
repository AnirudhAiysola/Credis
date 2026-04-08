#include "store.h"

std::mutex kv_store_mutex;
std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>,
                                             std::map<std::string, std::vector<std::pair<std::string, std::string>>, StreamComparator>>>
    kv_store;
std::unordered_map<std::string, long long> kv_store_expiry;
std::condition_variable kv_store_cv;
std::unordered_map<std::string, std::queue<std::condition_variable *>> waiting_clients;
std::unordered_map<std::string, std::queue<std::condition_variable *>> blocking_clients;
std::unordered_map<int, bool> inTransaction;
std::unordered_map<int, std::queue<std::vector<std::string>>> transaction_commands;
std::unordered_map<int, std::vector<std::string>> transaction_responses;