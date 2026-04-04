#pragma once
#include <string>
#include <vector>

std::vector<std::string> parse_resp(const std::string &buffer);
std::string build_bulk_string(std::string &str);