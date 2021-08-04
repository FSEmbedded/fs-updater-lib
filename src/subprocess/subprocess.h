#ifndef SUBPROCESS_H
#define SUBPROCESS_H

#include <string>
#include <filesystem>
#include <exception>

#define PIPE_PATTERN "subprocess_"
#define MAX_NUMBER_OF_TRIES 20
#define BUFFER_SIZE_READING_OUTPUT 512

namespace subprocess 
{
    ///////////////////////////////////////////////////////////////////////////
    /// Popen' exception definitions
    ///////////////////////////////////////////////////////////////////////////
    class SubprocessError : public std::exception
    {
        protected:
            std::string error_string;

        public:
            const char * what() const throw ()
            {
                return this->error_string.c_str();
            }
    };
    
    class BadNamedPipe : public SubprocessError
    {      
        public:
            BadNamedPipe(const std::string &path, const int local_errno)
            {
                this->error_string = std::string("Error while creating pipe: \"") + path + std::string("\" ");
                this->error_string += std::string("errno: ") + std::to_string(local_errno);
            }
    };

    class NoFreePipeFound : public SubprocessError
    {
        public:
            NoFreePipeFound()
            {
                this->error_string = "Tried " + std::to_string(MAX_NUMBER_OF_TRIES) + std::string("different pipe endpoints");
            }
    };

    class ErrorDeletePipe : public SubprocessError
    {
        public:
            explicit ErrorDeletePipe(const std::string &path)
            {
                this->error_string = std::string("Could not remove pipe \"") + path + std::string("\"");
            }
    };

    class ErrorOpenPipe : public SubprocessError
    {
        public:
            ErrorOpenPipe(const std::string &path, const int local_errno)
            {
                this->error_string = std::string("Could not open pipe \"") + path + std::string("\" ");
                this->error_string += std::string("errno: ") + std::to_string(local_errno);
            }
    };

    class ErrorPollPipe : public SubprocessError
    {
        public:
            explicit ErrorPollPipe(const int local_errno)
            {
                this->error_string = std::string("Error while polling; errno: ") + std::to_string(local_errno);
            }
    };

    class ErrorPollPipeWait : public SubprocessError
    {
        private:
            std::string error_string;
        
        public:
            explicit ErrorPollPipeWait(const std::string &err_msg)
            {
                this->error_string = std::string("Error while waiting for FIFO: ") + err_msg;
            }
    };

    class ExecuteSubprocess : public SubprocessError
    {
        public:
            ExecuteSubprocess( const std::string &cmd, const std::string &cause)
            {
                this->error_string = std::string("system() cmd caused error during exectung: \"") + cmd + std::string("\"; ");
                this->error_string += std::string("caused by: \"") + cause + std::string("\"");
            }
    };

        class ErrorReadPipe : public SubprocessError
    {
        public:
            ErrorReadPipe( const std::string &path, const int local_error)
            {
                this->error_string = std::string("Error during reading pipe: \"") + path + std::string("\"; ");
                this->error_string += std::string("ferror: ") + std::to_string(local_error);
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// Popen declaration
    ///////////////////////////////////////////////////////////////////////////
    class Popen
    {
        private:
            std::filesystem::path pipe;
            std::string cmd_ret;
            int ret_value;
        public:
            explicit Popen(const std::string &);
            ~Popen();
            Popen(const Popen &) = delete;
            Popen &operator=(const Popen &) = delete;
            Popen(Popen &&) = delete;
            Popen &operator=(Popen &&) = delete;

            std::string output() const;
            int returnvalue() const;

    };
};

#endif