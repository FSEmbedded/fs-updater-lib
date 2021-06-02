#include "subprocess.h"

subprocess::Popen::Popen(const std::string &prog)
{
    srand(time(NULL));
    unsigned int same_pipes = 0;
    while (true)
    {
        if (same_pipes >= MAX_NUMBER_OF_TRIES)
        {
            throw(NoFreePipeFound());
        }
        int rand_number = rand();
        
        std::stringstream stream;
        stream << std::hex << rand_number;
        std::filesystem::path pipe_endpoint(std::string("/tmp/") + std::string(PIPE_PATTERN) + stream.str());
        const std::regex subprocess_pattern(std::string("^") + std::string(PIPE_PATTERN) + std::string("[A-Fa-f0-9]*$"));

        bool random_number_already_used = false;

        for (const auto &entry: std::filesystem::directory_iterator(std::filesystem::path("/tmp/")))
        {
            if (std::regex_match(entry.path().string(), subprocess_pattern))
            {
                if (entry.path().string().compare(std::string(PIPE_PATTERN) + stream.str()))
                {
                    random_number_already_used = true;
                    break;
                }
            }
        }

        if (random_number_already_used == false)
        {
            const int state_mkf = mkfifo(pipe_endpoint.c_str(), 0600);
            if (state_mkf != 0)
            {
                throw(BadNamedPipe(pipe_endpoint.string(), errno));
            }
            this->pipe = pipe_endpoint;
            break;
        }
        else
        {
            same_pipes++;
        }
    }

    int fd_pipe = open(this->pipe.c_str(), O_NONBLOCK|O_RDONLY);
    if (fd_pipe == 0)
    {
        throw(ErrorOpenPipe(this->pipe, errno));
    }

    std::string cmd = prog;
    cmd += " &> " + this->pipe.string();

    const int system_status = system(cmd.c_str());

    if (system_status == 127)
    {
        close(fd_pipe);
        throw(ExecuteSubprocess(cmd, "Shell could not be executed"));
    }
    else if (system_status == -1)
    {
        close(fd_pipe);
        throw(ExecuteSubprocess(cmd, "Shell could not be created"));
    }

    this->ret_value = system_status;

    struct pollfd fd_pipe_poll[1];
    fd_pipe_poll[0].events = POLLRDNORM|POLLPRI|POLLRDBAND;
    fd_pipe_poll[0].fd = fd_pipe;

    const int poll_status = poll(fd_pipe_poll, 1, -1);
    if (poll_status == -1)
    {
        throw(ErrorPollPipe(errno));
    }

    if ( fd_pipe_poll[0].revents & POLLERR )
    {
        throw(ErrorPollPipeWait("An error has occurred on the device or stream"));
    }
    else if ( fd_pipe_poll[0].revents & POLLNVAL )
    {
        throw(ErrorPollPipeWait("File descriptor is invaild"));
    }

    try 
    {
        char BUFFER[BUFFER_SIZE_READING_OUTPUT + 1] = {0};
        int status_read = 0;
        do {
            status_read = read(fd_pipe, BUFFER, BUFFER_SIZE_READING_OUTPUT);
            if (status_read == -1)
            {
                close(fd_pipe);
                throw(ErrorReadPipe(this->pipe, errno));
            }
            this->cmd_ret += std::string(BUFFER);
        } while(status_read > 0);
        const std::regex remove_trailor("[ \t\n]+$");
        this->cmd_ret = std::regex_replace(this->cmd_ret, remove_trailor, "");
        close(fd_pipe);
    }
    catch(...)
    {
        close(fd_pipe);
        throw;
    }
    
}

subprocess::Popen::~Popen()
{
    if (this->pipe.string().length() > 0)
    {
        const int state = remove(this->pipe.c_str());
        if ( state != 0 )
        {
            ErrorDeletePipe(this->pipe.string());
        }
    }
}

std::string subprocess::Popen::output() const
{
    return this->cmd_ret;
}

int subprocess::Popen::returnvalue() const
{
    return this->ret_value;
}