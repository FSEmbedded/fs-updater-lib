#include "utils.h"

std::vector<std::string> util::split(const std::string & input, const char split)
{
    std::vector<std::string> return_element;
    unsigned int position = 0;
    unsigned int position_substr = 0;

    while (true)
    {
        position = input.find(split, position);
        
        if (position != std::string::npos)
        {
            return_element.push_back(input.substr(position_substr, position));
            position_substr = position;
        }
        else
        {
            break;
        }
    } 

    return return_element;
}

std::vector<unsigned char> util::to_array(const std::string & input)
{
    std::vector<unsigned char> return_value;
    for (const auto & entry: input)
    {
        return_value.push_back(static_cast<unsigned char>(entry));
    }

    return return_value;
}

unsigned char util::to_uchar(const std::string & input)
{
    const int number = std::stoi(input);

    if ( !((number <= UCHAR_MAX) && (number >= 0)) )
    {
        std::string error_msg("Number in input is :");
        error_msg += std::to_string(number);
        error_msg += std::string(" max allowed is: ") + std::to_string(UCHAR_MAX);
        error_msg += std::string(" smalles allowed is 0");

        throw(std::overflow_error(error_msg));
    }

    return static_cast<unsigned char>(number);
}