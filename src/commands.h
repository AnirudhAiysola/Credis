#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

using CommandHandler = std::function<void(int, std::vector<std::string> &)>;

std::string build_bulk_string(const std::string &str);
void handle_command(int client_fd, std::vector<std::string> &parsed);