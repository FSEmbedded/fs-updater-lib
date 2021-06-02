#ifndef UTILS_H
#define UTILS_H 

#include <string>
#include <vector>
#include <exception>
#include <stdexcept>
#include <climits>

namespace util
{
    ///////////////////////////////////////////////////////////////////////////
    /// utillity function collection
    ///////////////////////////////////////////////////////////////////////////
    std::vector<std::string> split(const std::string & input, const char split = ' ');
    std::vector<unsigned char> to_array(const std::string & input);
    unsigned char to_uchar(const std::string & input);
};

#endif