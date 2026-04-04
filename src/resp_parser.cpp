#include "resp_parser.h"
#include <sstream>
#include <iostream>

/**
 * @brief Parses a RESP message
 *
 * @param buffer
 * @return std::vector<std::string>
 */

bool isDataType(char &c)
{
    return (c == '+') || (c == '*') || (c == '-') || (c == ':') || (c == '$');
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
        if (!line.empty() && isDataType(line[0]))
        {
            continue;
        }
        // remove the trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::cout << "Parsed line: " << line << "\n";
        result.push_back(line);
    }
    return result;
}