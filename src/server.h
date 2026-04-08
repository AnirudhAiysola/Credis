#pragma once
#include <string>
#include <vector>
#include <unordered_map>

std::string build_bulk_string(const std::string &str);
void handle_client(int client_fd, std::string initial_data = "");
std::string build_array_response(std::vector<std::string> &keys, std::vector<std::string> &ids);
