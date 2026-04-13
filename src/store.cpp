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
bool isReplica = false;
std::vector<int> replica_fds;
std::string master = "";
long long byte_counter = 0;
int master_fd = -1;
std::unordered_map<int, long long> replica_ack_counts;
long long master_byte_counter = 0;
std::unordered_map<std::string, std::unordered_set<int>> subscribers;
std::unordered_map<int, std::unordered_set<std::string>> client_subscriptions;
std::unordered_set<std::string> sub_commands = {"SUBSCRIBE", "UNSUBSCRIBE", "PSUBSCRIBE", "PUNSUBSCRIBE", "PING", "QUIT", "RESET"};