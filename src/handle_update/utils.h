/**
 * Util is for all needed functions that only fulfill supporting aspects.
 */

#pragma once

#include <string>
#include <vector>
#include <exception>
#include <stdexcept>
#include <climits>
#include <stdexcept>

namespace util
{
    ///////////////////////////////////////////////////////////////////////////
    /// utillity function collection
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Split the string at given char and return the parts as array with strings.
     * @param input String that that have to be split at given ASCII-element.
     * @param split ASCII-element where to split string.
     * @return Array of strings which are substrings for given splitted input.
     */
    std::vector<std::string> split(const std::string & input, const char split = ' ');

    /**
     * Convert a string to unsigned char.
     * @param input Input string.
     * @return Array of string where every char is converted to unsigned char.
     */
    std::vector<unsigned char> to_array(const std::string & input);

    /**
     * Convert a string to unsigned char.
     * The string must also be a number which is in the range of unsigned char.
     * @param input String element which is a number;
     * @return Unsigned char extracted of input string.
     * @throw std::overflow_error If number in string is larger than unsigned char.
     */
    unsigned char to_uchar(const std::string & input);
}