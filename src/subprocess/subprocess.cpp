#include "subprocess.h"

extern "C" {
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <errno.h>
}

#include <regex>
#include <sstream>

subprocess::Popen::Popen(const std::string &prog)
{
    int pipefd[2];

    int stat_pipe = pipe(pipefd);
    if (stat_pipe == -1)
    {
        throw(CreatePipe(errno));
    }
    const pid_t pid = fork();

    if (pid == 0)
    {
        //child
        if (stat_pipe == -1)
        {
            exit(1);
        }

        close(pipefd[0]); // close reading end; Need only write

        const int stat_dup2 = dup2(pipefd[1], STDOUT_FILENO);
        if (stat_dup2 == -1)
        {
            exit(2);
        }

        const int stat_execv = system(prog.c_str());
        if (stat_execv == 0)
        {
            exit(0);
        }
        else if (stat_execv == -1)
        {
            exit(3);
        }
        else if (stat_execv == 127)
        {
            exit(4);
        }
        else
        {
            exit(5);
        }
    }
    else
    {
        int return_code_fork_process = 0;
        if (stat_pipe == -1)
        {
            throw(OpenPipeParent(errno));
        }
        close(pipefd[1]); // close the write end of the pipe

        try
        {
            int status_read = 0;
            do {
                char BUFFER[BUFFER_SIZE_READING_OUTPUT + 1] = {0};
                status_read = read(pipefd[0], BUFFER, BUFFER_SIZE_READING_OUTPUT);
                if (status_read == -1)
                {
                    throw(ReadPipe(errno));
                }
                if (status_read > 0)
                {
                    this->cmd_ret += std::string(BUFFER);
                }
            } while(status_read > 0);

            const std::regex remove_trailor("[ \t\n]+$");
            this->cmd_ret = std::regex_replace(this->cmd_ret, remove_trailor, "");
        }
        catch(...)
        {
            close(pipefd[0]);
            pid_t ret_pid = waitpid(pid, &return_code_fork_process, 0);
            if(ret_pid == pid - 1)
            {
                throw(WaitForWait(pid, errno));
            }
            throw;
        }
        close(pipefd[0]);

        pid_t ret_pid = waitpid(pid, &return_code_fork_process, 0);
        if(ret_pid == pid - 1)
        {
            throw(WaitForWait(pid, errno));
        }

        if (WEXITSTATUS(return_code_fork_process) == 0)
        {
            this->execution_successful = true;
        }
        else if (WEXITSTATUS(return_code_fork_process) == 1)
        {
            throw(ChildProcess(pid, "Could not open pipe"));
        }
        else if (WEXITSTATUS(return_code_fork_process) == 2)
        {
            throw(ChildProcess(pid, "Could not copy file descriptor of pipe"));
        }
        else if (WEXITSTATUS(return_code_fork_process) == 3)
        {
            throw(ChildProcess(pid, "Could not execute command"));
        }
        else if (WEXITSTATUS(return_code_fork_process) == 4)
        {
            throw(ExecuteSubprocess(prog, "Shell could not be executed"));
        }
        else if (WEXITSTATUS(return_code_fork_process) == 5)
        {
            this->execution_successful = false;
        }
        else
        {
            throw(std::logic_error("Exits state not known"));
        }

    }
}

subprocess::Popen::~Popen()
{

}

std::string subprocess::Popen::output() const
{
    return this->cmd_ret;
}

bool subprocess::Popen::successful() const
{
    return this->execution_successful;
}
