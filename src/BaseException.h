#pragma once
#include <exception>
#include <string>

/**
 * Standard base exception for the fs namespace.
 */
namespace fs
{
    /**
     * All classes which are interacting directly with fs framework
     * should throw exceptions based on this.
     */
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
}