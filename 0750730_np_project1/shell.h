#include <iostream>
#include <sstream>
#include <queue>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
using namespace std;


struct PipeFd
{
    int in_fd;
    int out_fd;
    int count;
};

class Shell
{
    private:
        // path
        vector<string> path_vector_;
        // commands
        string one_line_input_;
        queue<string> cmds_;
        // pipe
        vector<PipeFd> pipe_vector_;
        // pid
        vector<pid_t> pid_vector_;
    public:
        void Exec();
        // built-in operations
        vector<string> Split (string, string);
        void SetEnv(string, string);
        void PrintEnv(string);
        // command operations
        void ParseArgs();
        void PrintCmds(queue<string>);
        void ExecCmds();
        bool IsQueueEmpty(queue<string>);
        void ExecCmd(vector<string> &, bool, int, int, int, bool, bool);
        bool IsExecutable(string, vector<string>&);
        // pipe operations
        void CreatePipe(vector<PipeFd>&, int, int&);
        void CountdownPipeVector(vector<PipeFd>&);
        void GetPipeFd(vector<PipeFd>&, int&);
        void BindPipeFd(int, int&);
        void ConnectPipeFd(int, int, int);
        void ErasePipeVector(vector<PipeFd>&);
        // pid operations
        static void ChildHandler(int);
};

void Shell::Exec()
{
    // initialize environment variables
    SetEnv("PATH", "bin:.");
    pid_vector_.clear();
    // begin npshell program
    while(true)
    {
        cout << "% ";
        getline(cin, one_line_input_);
        // check EOF
        if(!cin)
            if(cin.eof())
            {
                cout << endl;
                return;
            }
        // check enter input
        if(one_line_input_.empty())
            continue;
        ParseArgs();
        // PrintCmds(cmds_);
        ExecCmds();
    }
}

/////////////////////////
// built-in operations //
/////////////////////////
vector<string> Shell::Split (string s, string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    string token;
    vector<string> res;

    while ((pos_end = s.find (delimiter, pos_start)) != string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }
    res.push_back (s.substr (pos_start));
    return res;
}

void Shell::SetEnv(string var, string val)
{
    setenv(var.c_str(), val.c_str(), 1);
    if (var=="PATH")
    {
        path_vector_.clear();
        vector<string> res = Split(val, ":");
        for(vector<string>::iterator it = res.begin(); it != res.end(); ++it) 
            path_vector_.push_back(*it);
    }
}

void Shell::PrintEnv(string var)
{
    char* val = getenv(var.c_str());
    if (val!=NULL)
        cout << val << endl;
}

////////////////////////
// command operations //
////////////////////////
void Shell::ParseArgs()
{
    istringstream in(one_line_input_);
    string t;
    while (in >> t)
        cmds_.push(t);
}

void Shell::PrintCmds(queue<string> cmds)
{
    queue<string> copy = cmds;
    while (!copy.empty())
    {
        cout << copy.front() << endl;
        copy.pop();
    }
    cout << endl;
}

void Shell::ExecCmds()
{
    bool is_first_argv = true;
    bool is_final_argv = false;
    string prog;
    vector<string> arguments;
    // pid
    bool is_using_pipe = false;
    bool line_ends = false;
    while (!cmds_.empty())
    {
        // init fd
        int in_fd=STDIN_FILENO, out_fd=STDOUT_FILENO, err_fd=STDERR_FILENO;

        if (is_first_argv)
        {
            prog = cmds_.front();
            cmds_.pop();
            is_first_argv = false;
            arguments.clear();
            arguments.push_back(prog);
            is_final_argv = IsQueueEmpty(cmds_);
            is_using_pipe = false;
            if (cmds_.empty())
                line_ends = true;
        }
        else
        {
            // encounter normal pipe
            if (cmds_.front().find('|') != string::npos)
            {
                // pipe: pipe num
                char pipe_char[5];
                int pipe_num;
                // simple pipe
                if (cmds_.front().length()==1)
                    pipe_num = 1;
                // numbered-pipe
                else
                {
                    for (int i = 1; i < cmds_.front().length(); i++)
                    {
                        pipe_char[i-1] = cmds_.front()[i];
                        pipe_char[i] = '\0';
                    }
                    pipe_num = atoi(pipe_char);
                }
                // pipe: create pipe
                CreatePipe(pipe_vector_, pipe_num, out_fd);
                
                is_first_argv = true;
                is_final_argv = true;
                
                is_using_pipe = true;
                cmds_.pop();
                if (cmds_.empty())
                    line_ends = true;
            }
            // encounter error pipe
            else if (cmds_.front().find('!') != string::npos)
            {
                // pipe: pipe num
                char pipe_char[5];
                int pipe_num;
                // simple pipe
                if (cmds_.front().length()==1)
                    pipe_num = 1;
                // numbered-pipe
                else
                {
                    for (int i = 1; i < cmds_.front().length(); i++)
                    {
                        pipe_char[i-1] = cmds_.front()[i];
                        pipe_char[i] = '\0';
                    }
                    pipe_num = atoi(pipe_char);
                }
                // pipe: create pipe
                CreatePipe(pipe_vector_, pipe_num, out_fd);
                err_fd = out_fd;
                
                is_first_argv = true;
                is_final_argv = true;

                is_using_pipe = true;
                cmds_.pop();
                if (cmds_.empty())
                    line_ends = true;
            }
            // encounter redirection >
            else if (cmds_.front() == ">")
            {
                cmds_.pop();
                string filename = cmds_.front();
                int file_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                if (file_fd < 0)
                    perror("open file error");
                out_fd = file_fd;
                is_first_argv = true;
                is_final_argv = true;

                is_using_pipe = false;
                cmds_.pop();
                if (cmds_.empty())
                    line_ends = true;
            }
            else if (cmds_.front() == ">>")
            {
                cmds_.pop();
                string filename = cmds_.front();
                int file_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
                if (file_fd < 0)
                    perror("open file error");
                out_fd = file_fd;
                is_first_argv = true;
                is_final_argv = true;

                is_using_pipe = false;
                cmds_.pop();
                if (cmds_.empty())
                    line_ends = true;
            }
            else if (cmds_.front() == "&>")
            {
                cmds_.pop();
                string filename = cmds_.front();
                int file_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                if (file_fd < 0)
                    perror("open file error");
                out_fd = file_fd;
                err_fd = file_fd;
                is_first_argv = true;
                is_final_argv = true;

                is_using_pipe = false;
                cmds_.pop();
                if (cmds_.empty())
                    line_ends = true;
            }
            else
            {
                arguments.push_back(cmds_.front());
                cmds_.pop();
                is_final_argv = IsQueueEmpty(cmds_);

                is_using_pipe = false;
                if (cmds_.empty())
                    line_ends = true;
            }
        }
        // execute
        if (is_final_argv)
        {
            // pipe: get pipe (count == 0)
            GetPipeFd(pipe_vector_, in_fd);
            // execute
            bool is_executable = IsExecutable(prog, path_vector_);
            ExecCmd(arguments, is_executable, in_fd, out_fd, err_fd, line_ends, is_using_pipe);
            is_final_argv = false;
            // pipe
            ErasePipeVector(pipe_vector_);
            CountdownPipeVector(pipe_vector_);
        }
    }
}

bool Shell::IsQueueEmpty(queue<string> cmds)
{
    if (cmds.empty()) return true;
    return false;
}

void Shell::ExecCmd(vector<string> &arguments, bool is_executable, int in_fd, int out_fd, int err_fd, bool line_ends, bool is_using_pipe)
{
    char *args[arguments.size()+1];
    for (size_t i = 0; i < arguments.size(); i++)
    {
        args[i] = new char[arguments[i].size() + 1];
        strcpy(args[i], arguments[i].c_str());
    }
    string prog(args[0]);
    
    // built-in functions
    if (prog == "printenv")
    {
        PrintEnv(args[1]);
        return;
    }
    else if (prog == "setenv")
    {
        SetEnv(args[1], args[2]);  // does overwrite
        return;
    }
    else if (prog == "exit")
    {
        ::exit(0);
        return;
    }
    // not built-in functions
    signal(SIGCHLD, ChildHandler);
    pid_t child_pid;
    child_pid = fork();
    while (child_pid < 0)
    {
        usleep(1000);
        child_pid = fork();
    }
    // child process
    if(child_pid == 0)
    {
        // pipe operations
        ConnectPipeFd(in_fd, out_fd, err_fd);
        if (!is_executable)
        {
            cerr << "Unknown command: [" << args[0] << "]." << endl;
            exit(0);
        }
        else
        {
            args[arguments.size()] = NULL;
            if(execvp(args[0], args) < 0)
            {
                perror("execl error");
                return;
            }
        }
    }
    // parent process
    else
    {
        if (line_ends)
        {
            if (!is_using_pipe)
            {
                int status;
                waitpid(child_pid, &status, 0);
            }
        }
    }
}

bool Shell::IsExecutable(string prog, vector<string> &path_vector_)
{
    if (prog == "printenv" || prog == "setenv" || prog == "exit")
        return true;
    
    bool is_executable;
    string path;
    vector<string>::iterator iter = path_vector_.begin();
    while(iter != path_vector_.end())
    {
        path = *iter;
        path = path + "/" + prog;
        is_executable = (access(path.c_str(), 0) == 0);
        if (is_executable)
            return true;
        iter += 1;
    }
    return false;
}

/////////////////////
// pipe operations //
/////////////////////
void Shell::CreatePipe(vector<PipeFd> &pipe_vector_, int pipe_num, int &in_fd)
{
    // check if pipe to same pipe
    // has same pipe => reuse old pipe (multiple write, one read)
    // no same pipe => create new pipe (one write, one read)
    bool has_same_pipe = false;
    vector<PipeFd>::iterator iter = pipe_vector_.begin();
    while(iter != pipe_vector_.end())
    {
        if ((*iter).count == pipe_num)
        {
            has_same_pipe = true;
            in_fd = (*iter).in_fd;
        }
        iter += 1;
    }
    if (has_same_pipe)
        return;
    // no same pipe
    int pipe_fd[2];
    if(pipe(pipe_fd) < 0)
    {
        perror("pipe error");
        ::exit(0);
    }
    PipeFd new_pipe_fd;
    new_pipe_fd.in_fd = pipe_fd[1];  // write fd
    new_pipe_fd.out_fd = pipe_fd[0];  // read fd
    new_pipe_fd.count = pipe_num;
    pipe_vector_.push_back(new_pipe_fd);
    in_fd = pipe_fd[1];
}

void Shell::CountdownPipeVector(vector<PipeFd> &pipe_vector_)
{
    vector<PipeFd>::iterator iter = pipe_vector_.begin();
    while(iter != pipe_vector_.end())
    {
        (*iter).count -= 1;
        iter += 1;
    }
}

void Shell::ErasePipeVector(vector<PipeFd> &pipe_vector_)
{
    vector<PipeFd>::iterator iter = pipe_vector_.begin();
    while(iter != pipe_vector_.end())
    {
        if ((*iter).count == 0)
        {
            close((*iter).in_fd);
            close((*iter).out_fd);
            pipe_vector_.erase(iter);
        }
        else
            iter += 1;
    }
}

void Shell::GetPipeFd(vector<PipeFd> &pipe_vector_, int& in_fd)
{
    vector<PipeFd>::iterator iter = pipe_vector_.begin();
    while(iter != pipe_vector_.end())
    {
        if ((*iter).count == 0)
        {
            close((*iter).in_fd);
            in_fd = (*iter).out_fd;
        }
        iter += 1;
    }
}

void Shell::ConnectPipeFd(int in_fd, int out_fd, int err_fd)
{
    if (in_fd != STDIN_FILENO)
        dup2(in_fd, STDIN_FILENO);
    if (out_fd != STDOUT_FILENO)
        dup2(out_fd, STDOUT_FILENO);
    if (err_fd != STDERR_FILENO)
        dup2(err_fd, STDERR_FILENO);
    if (in_fd != STDIN_FILENO)
        close(in_fd);
    if (out_fd != STDOUT_FILENO)
        close(out_fd);
    if (err_fd != STDERR_FILENO)
        close(err_fd);
};

////////////////////
// pid operations //
////////////////////
void Shell::ChildHandler(int signo)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0){}
}