#include "utils.h"

std::vector<std::string> util::split(const std::string & input, const char split)
{
    std::vector<std::string> return_element;
    std::string output;
    for (const char elem: input)
    {
        if (elem != split)
        {
            output.append(&elem,1);
        }
        else
        {
            return_element.push_back(output);
            output.clear();
        }
    }
    return_element.push_back(output);
    output.clear();

    return return_element;
}

std::vector<uint8_t> util::to_array(const std::string & input)
{
    std::vector<uint8_t> return_value;
    for (const auto & entry: input)
    {
        return_value.push_back(static_cast<uint8_t>(entry));
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