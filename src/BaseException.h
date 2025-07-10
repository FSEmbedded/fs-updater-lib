#pragma once
#include <fus_updater_lib/config.h>
#include <exception>
#include <string>

#if UPDATE_VERSION_TYPE_UINT64 == 1
typedef uint64_t version_t;
#elif UPDATE_VERSION_TYPE_STRING == 1
typedef std::string version_t;
#else
#error "No valid version type defined"
#endif

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
