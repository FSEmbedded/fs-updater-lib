#pragma once

#include <string>
#include <filesystem>
#include <exception>

#define PIPE_PATTERN "subprocess_"
#define MAX_NUMBER_OF_TRIES 20
#define BUFFER_SIZE_READING_OUTPUT 512


/**
 * Helper module to start external programs.
 * Interface inspired by the python subprocess library.
 * Can read the return code and the output of the stdout of started program.
 */
namespace subprocess 
{
    ///////////////////////////////////////////////////////////////////////////
    /// Popen' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class of exception class from helper module.
     * All exceptions of subprocess are derived from this base class.
     */
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
            /**
             * Describes error that occures when a named pipe can not be created.
             * @param path Path to named pipe destination.
             * @param local_errno Place a copy of the errno that are returned through the c-function.
             */
            BadNamedPipe(const std::string &path, const int local_errno)
            {
                this->error_string = std::string("Error while creating pipe: \"") + path + std::string("\" ");
                this->error_string += std::string("errno: ") + std::to_string(local_errno);
            }
    };

    class NoFreePipeFound : public SubprocessError
    {
        public:
            /**
             * The number of pipes are limited. If the number of max tries to create a pipe exceeds,
             * there must be a lot of pipes ceated. That is possible wrong. If not increase
             * the define MAX_NUMBER_OF_TRIES.
             * @param path Path to named pipe destination.
             * @param local_errno Place a copy of the errno that are returned through the C-function.
             */
            NoFreePipeFound()
            {
                this->error_string = "Tried " + std::to_string(MAX_NUMBER_OF_TRIES) + std::string("different pipe endpoints");
            }
    };

    class ErrorDeletePipe : public SubprocessError
    {
        public:
        /**
         * This error occures if the pipe can not be deleted.
         * @param path Path to named pipe.
         */
            explicit ErrorDeletePipe(const std::string &path)
            {
                this->error_string = std::string("Could not remove pipe \"") + path + std::string("\"");
            }
    };

    class ErrorOpenPipe : public SubprocessError
    {
        public:
        /**
         * Can not open given pipe.
         * @param path Path to named pipe.
         * @param local_errno Errno variable of failen open-function.
         */
            ErrorOpenPipe(const std::string &path, const int local_errno)
            {
                this->error_string = std::string("Could not open pipe \"") + path + std::string("\" ");
                this->error_string += std::string("errno: ") + std::to_string(local_errno);
            }
    };

    class ErrorPollPipe : public SubprocessError
    {
        public:
        /**
         * Can not poll given pipe.
         * @param local_errno Errno of failed poll-function.
         */
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
        /**
         * Can not wait for pipe. Error during waiting.
         * @param err_msg Error message while waiting.
         */
            explicit ErrorPollPipeWait(const std::string &err_msg)
            {
                this->error_string = std::string("Error while waiting for FIFO: ") + err_msg;
            }
    };

    class ExecuteSubprocess : public SubprocessError
    {
        public:
        /**
         * Error during executing of program.
         * @param cmd Command the is failed.
         * @param cause Reason for failed execution.
         */
            ExecuteSubprocess( const std::string &cmd, const std::string &cause)
            {
                this->error_string = std::string("system() cmd caused error during exectung: \"") + cmd + std::string("\"; ");
                this->error_string += std::string("caused by: \"") + cause + std::string("\"");
            }
    };

        class ErrorReadPipe : public SubprocessError
    {
        public:
        /**
         * Could not read from pipe.
         * @param path Path to pipe endpoint.
         * @param local_error Errno variable during error occurs.
         */
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
            /**
             * Executes given string. Blocks until the command is executed.
             * @param prog String that should be executed
             * @throw NoFreePipeFound
             * @throw BadNamedPipe
             * @throw ErrorOpenPipe
             * @throw ExecuteSubprocess
             * @throw ErrorPollPipe
             * @throw ErrorPollPipeWait
             * @throw ErrorReadPipe
             * @throw ErrorDeletePipe
             */
            explicit Popen(const std::string &);
            /**
             * Clean up memory. Close pipe and delete it.
             */
            ~Popen();

            Popen(const Popen &) = delete;
            Popen &operator=(const Popen &) = delete;
            Popen(Popen &&) = delete;
            Popen &operator=(Popen &&) = delete;

            /**
             * Return the ouput of command.
             * @return The stdout of given command.
             */
            std::string output() const;

            /**
             * Return the exit-code of the given command.
             * @return The exit-code of command.
             */
            int returnvalue() const;
    };
};