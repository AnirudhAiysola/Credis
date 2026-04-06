#include "resp_parser.h"
#include <sstream>
#include <iostream>

/**
 * @brief Parses a RESP message
 *
 * @param buffer
 * @return std::vector<std::string>
 */

bool isDataType(const char &c, const char &next_c)
{
    return (c == '*' && std::isdigit(next_c)) ||
           (c == '+' && std::isalpha(next_c)) ||
           (c == '-' && std::isalpha(next_c)) ||
           (c == ':') ||
           (c == '$');
}

/**
 * @brief Parses a RESP message
 *
 * @param buffer
 * @return std::vector<std::string>
 */
std::vector<std::string> parse_resp(const std::string &buffer)
{
    std::stringstream ss(buffer);
    std::string line;
    std::vector<std::string> result;

    while (std::getline(ss, line, '\n'))
    {
        if (!line.empty() && line.size() > 1 && isDataType(line[0], line[1]))
        {
            continue;
        }
        // remove the trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::cout << "Parsed line: " << line << "\n";
        if (!line.empty())
        {
            result.push_back(line);
        }
    }
    return result;
}