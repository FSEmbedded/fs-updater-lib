#pragma once

#include <string>
#include <filesystem>
#include <exception>

extern "C"{
    #include <sys/types.h>
}

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

    class ChildProcess : public SubprocessError
    {
        public:
            /**
             * Execution error in child process.
             * @param msg Error string that occured.
             */
            ChildProcess(const pid_t process_id, const std::string & msg)
            {
                this->error_string = std::string("Error in child process: ") + std::to_string(process_id);
                this->error_string += std::string("; error: ") + msg;
            }
    };

    class WaitForWait : public SubprocessError
    {
        public:
        /**
         * Can not wait for fork process end.
         * @param local_errno Errno variable of failed wait request.
         */
        WaitForWait(const pid_t process_id, const int local_errno)
        {
            this->error_string = std::string("Could not wait for forked process: ") + std::to_string(process_id);
            this->error_string = std::string("; errno: ") + std::to_string(local_errno);
        }

    };

    class OpenPipeParent : public SubprocessError
    {
        public:
            /**
             * Can not open pipe in parent process.
             * @param local_errno Errno variable of failen open-function.
             */
            explicit OpenPipeParent(const int local_errno)
            {
                this->error_string = std::string("Could not open pipe in parent process; ");
                this->error_string += std::string("errno: ") + std::to_string(local_errno);
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

    class ReadPipe : public SubprocessError
    {
        public:
            /**
             * Could not read from pipe.
             * @param local_error Errno variable during error occurs.
             */
            explicit ReadPipe( const int local_error)
            {
                this->error_string = std::string("Error during reading pipe; errno: ") + std::to_string(local_error);
            }
    };

    class CreatePipe : public SubprocessError
    {
        public:
            /**
             * Could not read from pipe.
             * @param local_error Errno variable during error occurs.
             */
            explicit CreatePipe( const int local_error)
            {
                this->error_string = std::string("Error during creating pipe; errno: ") + std::to_string(local_error);
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// Popen declaration
    ///////////////////////////////////////////////////////////////////////////
    class Popen
    {
        private:
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
             * @throw ReadPipe
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
}
