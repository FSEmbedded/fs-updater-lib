#pragma once
#include <exception>
#include <string>

namespace fs
{
    class BaseFSUpdateException : public std::exception
    {
        protected:
            std::string error_msg;

        public:
            const char * what() const throw () 
            {
                return this->error_msg.c_str();
            }
    };
};