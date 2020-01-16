#include <sys/types.h>
#include <sstream>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <queue>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>

using namespace std;

#define BUFF_SIZE 2048
#define MAX_CLIENT_SIZE 30
#define MAX_MESSAGE_SIZE 1024
#define PERMS 0666
#define SHM_KEY 9000
#define SHM_MSG_KEY 9001
#define SHM_FIFO_KEY 9002
#define FIFO_PATH "user_pipe/"
#define MAX_PATH_SIZE 30

// Pipe
struct PipeFd
{
    int in_fd;
    int out_fd;
    int count;
};
// Environment Info
struct EnvInfo
{
    string env_var;
    string env_val;
};
// Client Info
struct ClientInfo{
    int valid;
    int pid;
    int id;
    char user_name[20];
    char user_ip[INET_ADDRSTRLEN];
    int port;
};
struct FIFO{
    char name[MAX_PATH_SIZE];
    bool is_used;
    int in_fd;
    int out_fd;
};
struct FIFOInfo{
    FIFO fifo[MAX_CLIENT_SIZE][MAX_CLIENT_SIZE];
};
// global server variable
int g_shmid_cli;
int g_shmid_msg;
int g_shmid_fifo;
// client
int cur_id;
// create user variable
vector<ClientInfo> client_table;

// declare functions needed by shell
ClientInfo* GetCliSHM(int g_shmid_cli);
ClientInfo* GetClientByID(int id, ClientInfo*);
char* GetMsgSHM(int g_shmid_msg);
FIFOInfo* GetFIFOSHM(int g_shmid_fifo);
void Broadcast(string action, string msg, int cur_id, int target_id);
void BroadcastOne(string action, string msg, int cur_id, int target_id);
void PrintWelcome();
void SigHandler (int sig);

class Shell
{
    private:
        // path
        vector<string> path_vector_;
        // commands
        string one_line_input_;
        queue<string> cmds_;
        // pid
        vector<pid_t> pid_vector_;
        // pipe
        vector<PipeFd> pipe_vector_;  // numbered pipe
        // vector<UserPipeFd> userpipe_vector_;  // userpipe
    public:
        void Exec(int);
        // built-in operations
        vector<string> Split (string, string);
        void SetEnv(string, string);
        void PrintEnv(string);
        // command operations
        void ParseArgs();
        void PrintCmds(queue<string>);
        int ExecCmds();
        bool IsQueueEmpty(queue<string>);
        int ExecCmd(vector<string> &, bool, int, int, int, bool, bool, int);
        bool IsExecutable(string, vector<string>&);
        // pipe operations
        void CreatePipe(vector<PipeFd>&, int, int&);
        void CountdownPipeVector(vector<PipeFd>&);
        bool GetPipeFd(vector<PipeFd>&, int&);
        void BindPipeFd(int, int&);
        void ConnectPipeFd(int, int, int);
        void ErasePipeVector(vector<PipeFd>&);
        // pid operations
        static void ChildHandler(int);
        // client operations
        int ClientExec(int);
        void SetAllEnv(void);
        void EraseEnv(string);
        void Who(void);
        void Yell(string);
        void Tell(int, string);
        void Name(string);
        // client user pipe
        void CreateUserPipe(int, int, int&);
        void GetUserPipeFd(int, int, int&);
        void SetUserPipeOut(int send_id, int& out_fd);
        void EraseUserPipe(int);
};

int Shell::ClientExec(int id)
{
    // init
    cur_id = id;  // record current id
    clearenv();
    SetEnv("PATH", "bin:.");
    // signal handler
    signal(SIGUSR1, SigHandler);	/* receive messages from others */
	signal(SIGUSR2, SigHandler);	/* open fifos to read from */
	signal(SIGINT, SigHandler);
	signal(SIGQUIT, SigHandler);
	signal(SIGTERM, SigHandler);
    // welcome
    PrintWelcome();
    Broadcast("login", "", cur_id, -1);
    // initialize environment variables
    pid_vector_.clear();
    while(true)
    {
        cout << "% ";
        getline(cin, one_line_input_);
        // check EOF
        if(!cin)
            if(cin.eof())
            {
                cout << endl;
                return 1;
            }
        // check enter input
        if(one_line_input_.empty())
            continue;
        ParseArgs();
        int status = ExecCmds();
        // status -1 means exit
        if (status == -1)
            return status;
    }
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
// /////////////////////////
// // built-in operations //
// /////////////////////////
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
void Shell::PrintEnv(string var)
{
    char* val = getenv(var.c_str());
    if (val!=NULL)
        cout << val << endl;
}
void Shell::Who(void)
{
    int temp_id = 0;
    ClientInfo* shm_cli;
    ClientInfo* cur_cli = GetClientByID(cur_id, shm_cli);
    cout << "<ID>\t" << "<nickname>\t" << "<IP:port>\t"<<"<indicate me>"<< endl;
    for (size_t id = 0; id < MAX_CLIENT_SIZE; id++)
    {
        if (GetClientByID(id+1, shm_cli) != NULL)
        {
            temp_id = id + 1;
            ClientInfo* temp = GetClientByID(temp_id, shm_cli);
            cout << temp->id << "\t" << temp->user_name << "\t" << temp->user_ip;
            cout << ":" << temp->port;
            if (temp_id == cur_cli->id)
                cout << "\t" << "<-me" << endl;
            else
                cout << "\t" << endl;
        }
    }
    shmdt(shm_cli);
}
void Shell::Yell(string msg)
{
    Broadcast("yell", msg, cur_id, -1);
}
void Shell::Tell(int target_id, string msg)
{
    ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
    if(shm_cli[target_id-1].valid != 0)
        BroadcastOne("tell", msg, cur_id, target_id);
    else
        cout << "*** Error: user #" << to_string(target_id) << " does not exist yet. ***" << endl;
    shmdt(shm_cli);
}
void Shell::Name(string name)
{
    ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
    for (size_t i = 0; i < MAX_CLIENT_SIZE; i++)
    {
        if (shm_cli[i].user_name == name)
        {
            cout << "*** User '" + name + "' already exists. ***" << endl;
            return;
        }
    }
    strcpy(shm_cli[cur_id-1].user_name, name.c_str());
    shmdt(shm_cli);
    Broadcast("name", "", cur_id, -1);
}
// ////////////////////////
// // command operations //
// ////////////////////////
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
int Shell::ExecCmds()
{
    // store arguments
    bool is_first_argv = true;
    bool is_final_argv = false;
    string prog;
    vector<string> arguments;
    // pid
    bool is_using_pipe = false;
    bool line_ends = false;
    bool is_in_redirect = false;
    int in_fd=STDIN_FILENO, out_fd=STDOUT_FILENO, err_fd=STDERR_FILENO;
    // client status
    int status = 0;
    bool is_in_userpipe = false;
    // client broadcast msg and setup userpipe
    int source_id = -1;
    int target_id = -1;
    // client broadcast msg
    int recv_str_id = -1;
    int send_str_id = -1;
    while (!cmds_.empty())
    {
        // init fd
        if(!is_in_redirect && !is_in_userpipe)
            in_fd=STDIN_FILENO, out_fd=STDOUT_FILENO, err_fd=STDERR_FILENO;
        if (is_first_argv)
        {
            prog = cmds_.front();
            cmds_.pop();
            arguments.clear();
            arguments.push_back(prog);
            // deal with tell and yell
            if (prog == "tell" || prog == "yell")
            {
                while (!cmds_.empty())
                {
                    arguments.push_back(cmds_.front());
                    cmds_.pop();
                }
            }
            is_first_argv = false;
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
                // is_first_argv = true;
                // is_final_argv = true;
                is_in_redirect = true;

                is_using_pipe = false;
                cmds_.pop();
                if (cmds_.empty())
                {
                    line_ends = true;
                    is_first_argv = true;
                    is_final_argv = true;
                }
            }
            else if (cmds_.front() == "<")
            {
                cmds_.pop();
                string filename = cmds_.front();
                int file_fd = open(filename.c_str(), O_RDONLY, 0644);
                if (file_fd < 0)
                    perror("open file error");
                is_using_pipe = false;
                is_in_redirect = true;
                in_fd = file_fd;
                cmds_.pop();
                if (cmds_.empty())
                {
                    line_ends = true;
                    is_first_argv = true;
                    is_final_argv = true;
                }
            }
            // encounter named pipe (out)
            else if ((cmds_.front().find('>') != string::npos) && (cmds_.front() != ">"))
            {
                // pipe: pipe num
                char user_char[3];
                // user-pipe
                for (int i = 1; i < cmds_.front().length(); i++)
                {
                    user_char[i-1] = cmds_.front()[i];
                    user_char[i] = '\0';
                }
                // record target_id
                target_id = atoi(user_char);
                // target id does not exist
                ClientInfo* shm_cli;
                if (GetClientByID(target_id, shm_cli) == NULL)
                {
                    cmds_.pop();
                    cout << "*** Error: user #" << target_id << " does not exist yet. ***" << endl;
                    target_id = -1;
                    // clear cmds_ queue
                    queue<string> empty;
                    swap(cmds_, empty);
                    return 0;
                }
                // target id exists
                else
                {
                    // check if user pipe already exists
                    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
                    if (shm_fifo->fifo[cur_id-1][target_id-1].in_fd != -1)
                    {
                        cout << "*** Error: the pipe #" << cur_id << "->#" << target_id;
                        cout << " already exists. ***" << endl;
                        // clear cmds_ queue
                        queue<string> empty;
                        swap(cmds_, empty);
                        return 0;
                    }
                    shmdt(shm_fifo);
                    // mkfifo and record fifoname
                    CreateUserPipe(cur_id, target_id, out_fd);
                    is_using_pipe = true;
                    is_in_userpipe = true;
                    cmds_.pop();
                    if (cmds_.empty())
                    {
                        line_ends = true;
                        is_first_argv = true;
                        is_final_argv = true;
                    }
                    send_str_id = target_id;
                }
            }
            // encounter named pipe (in)
            else if ((cmds_.front().find('<') != string::npos) && (cmds_.front() != "<"))
            {
                // pipe: pipe num
                char user_char[3];
                // user-pipe
                for (int i = 1; i < cmds_.front().length(); i++)
                {
                    user_char[i-1] = cmds_.front()[i];
                    user_char[i] = '\0';
                }
                // record source_id
                source_id = atoi(user_char);
                // target id does not exist
                ClientInfo* shm_cli;
                if (GetClientByID(source_id, shm_cli) == NULL)
                {
                    cmds_.pop();
                    cout << "*** Error: user #" << source_id << " does not exist yet. ***" << endl;
                    source_id = -1;
                    // clear cmds_ queue
                    queue<string> empty;
                    swap(cmds_, empty);
                    return 0;
                }
                // target id exists
                else
                {
                    // check if user pipe already exists
                    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
                    if (shm_fifo->fifo[source_id-1][cur_id-1].out_fd == -1)
                    {
                        // cannot find any userpipe's target id is current client id
                        cout << "*** Error: the pipe #" << source_id << "->#" << cur_id;
                        cout << " does not exist yet. ***" << endl;
                        // clear cmds_ queue
                        queue<string> empty;
                        swap(cmds_, empty);
                        return 0;
                    }
                    shmdt(shm_fifo);
                    GetUserPipeFd(source_id, cur_id, in_fd);
                    cmds_.pop();
                    is_in_userpipe = true;
                    if (cmds_.empty())
                    {
                        line_ends = true;
                        is_first_argv = true;
                        is_final_argv = true;
                    }
                    recv_str_id = source_id;
                }
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
            // broadcast send and recv
            if (recv_str_id != -1)
            {
                string line_input = one_line_input_.substr(0, one_line_input_.length());
                if (line_input.back()=='\r')
                    line_input.pop_back();
                recv_str_id = -1;
                Broadcast("recv", line_input, cur_id, source_id);
                usleep(50);
            }
            if (send_str_id != -1)
            {
                string line_input = one_line_input_.substr(0, one_line_input_.length());
                if (line_input.back()=='\r')
                    line_input.pop_back();
                send_str_id = -1;
                Broadcast("send", line_input, cur_id, target_id);
                usleep(50);
            }
            
            // pipe: get pipe (count == 0)
            bool need_close_pipe = GetPipeFd(pipe_vector_, in_fd);
            // execute
            bool is_executable = IsExecutable(prog, path_vector_);
            status = ExecCmd(arguments, is_executable, in_fd, out_fd, err_fd, line_ends, is_using_pipe, target_id);
            is_final_argv = false;
            is_in_redirect = false;
            is_in_userpipe = false;
            // pipe
            ErasePipeVector(pipe_vector_);
            CountdownPipeVector(pipe_vector_);
            if (need_close_pipe)
                close(in_fd);
            if (target_id > 0)
                target_id = -1;
            // has userpipe in -> need to erase
            if (source_id > 0)
            {
                EraseUserPipe(source_id);
                source_id = -1;
            }
        }
    }
    return status;
}
bool Shell::IsQueueEmpty(queue<string> cmds)
{
    if (cmds.empty()) return true;
    return false;
}
int Shell::ExecCmd(vector<string> &arguments, bool is_executable, int in_fd, int out_fd, int err_fd, bool line_ends, bool is_using_pipe, int target_id)
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
        return 0;
    }
    else if (prog == "setenv")
    {
        SetEnv(args[1], args[2]);
        return 0;
    }
    else if (prog == "who")
    {
        Who();
        return 0;
    }
    else if (prog == "yell")
    {
        // concate all following char
        string msg = "";
        for (size_t i = 1; i < arguments.size(); i++)
            msg += string(args[i]) + " ";
        msg.pop_back();
        Yell(msg);
        return 0;
    }
    else if (prog == "tell")
    {
        int target_id = stoi(args[1]);
        string msg = "";
        for (size_t i = 2; i < arguments.size(); i++)
            msg += string(args[i]) + " ";
        msg.pop_back();
        Tell(target_id, msg);
        return 0;
    }
    else if (prog == "name")
    {
        string name = string(args[1]);
        Name(name);
        return 0;
    }
    else if (prog == "exit")
        return -1;
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
        // now current client open fifo and record write fd
        if (target_id > 0)
            SetUserPipeOut(target_id, out_fd);
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
                exit(0);
            }
            perror("execvp");
            exit(0);
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
    return 0;
}
bool Shell::IsExecutable(string prog, vector<string> &path_vector_)
{
    if (prog == "printenv" || prog == "setenv" || prog == "exit")
        return true;
    // client built-in cmd
    if (prog == "who" || prog == "tell" || prog == "yell" || prog == "name")
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
bool Shell::GetPipeFd(vector<PipeFd> &pipe_vector_, int& in_fd)
{
    vector<PipeFd>::iterator iter = pipe_vector_.begin();
    while(iter != pipe_vector_.end())
    {
        if ((*iter).count == 0)
        {
            close((*iter).in_fd);
            in_fd = (*iter).out_fd;
            return true;
        }
        iter += 1;
    }
    return false;
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
/////////////////////////
// userpipe operatoins //
/////////////////////////
void Shell::CreateUserPipe(int cur_id, int target_id, int &out_fd)
{
    char fifopath[MAX_PATH_SIZE];
    sprintf(fifopath, "%suser_pipe_%d_%d", FIFO_PATH, cur_id, target_id);
    // create fifo
    if (mkfifo(fifopath, S_IFIFO | PERMS)<0)
    {
        perror("mkfifo error");
        ::exit(0);
    }
    ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
    ClientInfo* target_cli = GetClientByID(target_id, shm_cli);
    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
    strncpy(shm_fifo->fifo[cur_id-1][target_id-1].name, fifopath, MAX_PATH_SIZE);
    // signal target client to open fifo and read
    kill(target_cli->pid, SIGUSR2);
    shmdt(shm_cli);
    shmdt(shm_fifo);
}
void Shell::SetUserPipeOut(int target_id, int& out_fd)
{
    ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
    ClientInfo* target_cli = GetClientByID(target_id, shm_cli);
    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
    char fifopath[MAX_PATH_SIZE];
    sprintf(fifopath, "%suser_pipe_%d_%d", FIFO_PATH, cur_id, target_id);
    // now current client open fifo and record write fd
    out_fd = open(fifopath, O_WRONLY);
    shm_fifo->fifo[cur_id-1][target_id-1].in_fd = out_fd;
    shmdt(shm_cli);
    shmdt(shm_fifo);
}
void Shell::GetUserPipeFd(int source_id, int cur_id, int& in_fd)
{
    char fifopath[MAX_PATH_SIZE];
    sprintf(fifopath, "%suser_pipe_%d_%d", FIFO_PATH, source_id, cur_id);
    ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
    ClientInfo* source_cli = GetClientByID(source_id, shm_cli);
    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
    shm_fifo->fifo[source_id-1][cur_id-1].in_fd = -1;
    in_fd = shm_fifo->fifo[source_id-1][cur_id-1].out_fd;
    shm_fifo->fifo[source_id-1][cur_id-1].is_used = true;
    shmdt(shm_cli);
    shmdt(shm_fifo);
}
void Shell::EraseUserPipe(int id)
{
    FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
    if (shm_fifo->fifo[id-1][cur_id-1].is_used == true)
    {
        // close(shm_fifo->fifo[id-1][cur_id-1].out_fd);
        shm_fifo->fifo[id-1][cur_id-1].in_fd = -1;
        shm_fifo->fifo[id-1][cur_id-1].out_fd = -1;
        shm_fifo->fifo[id-1][cur_id-1].is_used = false;
        unlink(shm_fifo->fifo[id-1][cur_id-1].name);
        memset(&shm_fifo->fifo[id-1][cur_id-1].name, 0, sizeof(shm_fifo->fifo[id-1][cur_id-1].name));
    }
}
////////////////////////////////////////////////////////////////////////////////
///////////////////////
// socket operations //
///////////////////////
int PassiveTCP(int port)
{
    int LISTENQ = 0;
    int sockfd = 0;
    struct sockaddr_in serv_addr;
    // open TCP socket
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0))<0)
    {
        cerr << "server: can't open stream socket" << endl;
        return 0;
    }
        
    bzero((char *) &serv_addr, sizeof(serv_addr));
    // set TCP
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    // set socket
	int optval = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) 
    {
		cerr << "Error: set socket failed" << endl;
		return 0;
	}
    // bind socket
	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
		cerr << "Error: bind socket failed" << endl;
        return 0;
	}
	listen(sockfd, LISTENQ);
	return sockfd;
}
///////////////////////
// server operations //
///////////////////////
int GetClientIDByPID() {
	ClientInfo* shm;
	if ((shm = (ClientInfo*)shmat(g_shmid_cli, NULL, 0)) == (ClientInfo*)-1) {
		fprintf(stderr, "Error: shmat() failed\n");
		::exit(1);
	}
	int pid = getpid();
	for (int i = 0; i < MAX_CLIENT_SIZE; i++) {
		if (shm[i].valid == 1 && shm[i].pid == pid) {
			shmdt(shm);
			return (i+1);
		}
	}
	shmdt(shm);
	return -1;
}
void PrintWelcome()
{
    string msg = "";
    msg += "****************************************\n";
    msg += "** Welcome to the information server. **\n";
    msg += "****************************************\n";
    cout << msg;
}
void Broadcast(string action, string msg, int cur_id, int target_id)
{
    // init
    string send_msg = "";
    ClientInfo* shm_cli;
    ClientInfo* cur_cli = GetClientByID(cur_id, shm_cli);
    ClientInfo* target_cli;
    if (target_id != -1)  // no need target_id
        target_cli = GetClientByID(target_id, shm_cli);
    else
        target_cli = NULL;
    if (cur_cli == NULL)
    {
        cout << "cannot find client id: " << cur_id << endl;
        return;
    }
    // broadcast to all
    if (action == "login")
        send_msg = "*** User '(no name)' entered from "+string(cur_cli->user_ip)+":"+to_string(cur_cli->port)+". ***\n";
    else if (action == "logout")
        send_msg = "*** User '"+string(cur_cli->user_name)+"' left. ***\n";
    else if (action == "name")
        send_msg = "*** User from " +string(cur_cli->user_ip)+":"+to_string(cur_cli->port)+" is named '"+cur_cli->user_name+"'. ***\n";
    else if (action == "yell")
        send_msg = "*** "+string(cur_cli->user_name)+" yelled ***: " + msg + "\n";   
    else if (action == "send")
    {
        send_msg = "*** "+string(cur_cli->user_name)+" (#"+to_string(cur_cli->id)+") just piped '";
        send_msg += msg+"' to "+ target_cli->user_name +" (#"+to_string(target_cli->id)+") ***\n";
    }
    else if (action == "recv")
    {
        send_msg = "*** "+string(cur_cli->user_name)+" (#"+to_string(cur_cli->id);
        send_msg += ") just received from "+string(target_cli->user_name)+" (#";
        send_msg += to_string(target_cli->id)+") by '"+msg+"' ***\n";
    }
    char* shm_msg = GetMsgSHM(g_shmid_msg);
    sprintf(shm_msg, "%s", send_msg.c_str());
    shmdt(shm_msg);
    usleep(50);
    shm_cli = GetCliSHM(g_shmid_cli);
    for (int i = 0; i < MAX_CLIENT_SIZE; i++)
    {
        // cout << "id: " << (i+1) << "valid: " << shm_cli[i].valid << endl;
		if (shm_cli[i].valid == 1)
            kill(shm_cli[i].pid, SIGUSR1);
	}
	shmdt(shm_cli);
    shmdt(shm_msg);
    return;
}

void BroadcastOne(string action, string msg, int cur_id, int target_id)
{
    // init
    string send_msg = "";
    ClientInfo* shm = GetCliSHM(g_shmid_cli);
    ClientInfo* cur_cli = GetClientByID(cur_id, shm);
    ClientInfo* target_cli = GetClientByID(target_id, shm);
    // broadcast to one
    if (action == "tell")
    {
        send_msg = "*** " + string(cur_cli->user_name) + " told you ***: " + msg + "\n";
    }
    char* shm_msg = GetMsgSHM(g_shmid_msg);
    sprintf(shm_msg, "%s", send_msg.c_str());
    shmdt(shm_msg);
    usleep(50);
    shm = GetCliSHM(g_shmid_cli);
    kill(shm[target_id-1].pid, SIGUSR1);
    shmdt(shm);
    return;
}

///////////////////////
// shrmem operations //
///////////////////////
void ResetCliSHM(int g_shmid_cli) {
	ClientInfo *shm;
	if ((shm = (ClientInfo*)shmat(g_shmid_cli, NULL, 0)) == (ClientInfo*)-1)
    {
		fprintf(stderr, "Error: shmat() failed\n");
		::exit(1);
	}
	for(int i = 0; i < MAX_CLIENT_SIZE; i++)
    {
		shm[i].valid = 0;
	}
	shmdt(shm);
}
void ResetFIFOSHM(int g_shmid_fifo) {
	FIFOInfo *shm_fifo = GetFIFOSHM(g_shmid_fifo);
	for(size_t i = 0; i < MAX_CLIENT_SIZE; i++)
    {
        for (size_t j = 0; j < MAX_CLIENT_SIZE; j++)
        {
            shm_fifo->fifo[i][j].in_fd = -1;
            shm_fifo->fifo[i][j].out_fd = -1;
            shm_fifo->fifo[i][j].is_used = 0;
            char name[MAX_PATH_SIZE];
            memset(&shm_fifo->fifo[i][j].name, 0, sizeof(name));
        }
	}
	shmdt(shm_fifo);
}
void InitSHM()
{
	// temp for extern
	int shmid_cli;
	int shmid_msg;
    int shmid_fifo;
	// var
	key_t key = SHM_KEY;
	key_t msg_key = SHM_MSG_KEY;
    key_t fifo_key = SHM_FIFO_KEY;
	int shm_size = sizeof(ClientInfo) * MAX_CLIENT_SIZE;
	if ((shmid_cli = shmget(key, shm_size, IPC_CREAT | PERMS)) < 0)
    {
		// failed
		fprintf(stderr, "Error: init_shm() failed\n");
		::exit(1);
	}
    // cout << "shmid_cli" << endl;
	int msg_size = sizeof(char) * MAX_MESSAGE_SIZE;
	if ((shmid_msg = shmget(msg_key, msg_size, IPC_CREAT | PERMS)) < 0)
    {
		// failed
		fprintf(stderr, "Error: init_shm() failed\n");
		::exit(1);
	}
    // cout << "shmid_msg" << endl;
    int fifo_size = sizeof(FIFOInfo);
	if ((shmid_fifo = shmget(fifo_key, fifo_size, IPC_CREAT | PERMS)) < 0)
    {
		// failed
		fprintf(stderr, "Error: init_shm() failed\n");
		::exit(1);
	}
    // cout << "shmid_fifo" << endl;
	ResetCliSHM(shmid_cli);
    ResetFIFOSHM(shmid_fifo);
	// update global var
	g_shmid_cli = shmid_cli;
	g_shmid_msg = shmid_msg;
    g_shmid_fifo = shmid_fifo;
}
ClientInfo* GetCliSHM(int g_shmid_cli)
{
    ClientInfo* shm;
    // share memory attach
	if ((shm = (ClientInfo*)shmat(g_shmid_cli, NULL, 0)) == (ClientInfo*)-1)
    {
		// failed
		fprintf(stderr, "Error: get_new_id() failed\n");
		::exit(1);
	}
    return shm;
}
char* GetMsgSHM(int g_shmid_msg)
{
    char* shm;
    // share memory attach
	if ((shm = (char*)shmat(g_shmid_msg, NULL, 0)) == (char*)-1)
    {
		// failed
		fprintf(stderr, "Error: get_new_id() failed\n");
		::exit(1);
	}
    return shm;
}
FIFOInfo* GetFIFOSHM(int g_shmid_fifo)
{
    FIFOInfo* shm;
    // share memory attach
	if ((shm = (FIFOInfo*)shmat(g_shmid_fifo, NULL, 0)) == (FIFOInfo*)-1)
    {
		// failed
		fprintf(stderr, "Error: get_new_id() failed\n");
		::exit(1);
	}
    return shm;
}

int GetAvaliableIDFromSHM()
{
	ClientInfo *shm;
	shm = GetCliSHM(g_shmid_cli);
	for (int i = 0; i < MAX_CLIENT_SIZE; i++)
    {
		if (!shm[i].valid)
        {
            shm[i].valid = 1;
			shmdt(shm);
			return (i+1);
		}
	}
    // share memory detach
	shmdt(shm);
    return -1;  // no more available id
}
ClientInfo* GetClientByID(int id, ClientInfo* shm_cli)
{
    shm_cli = GetCliSHM(g_shmid_cli);
	for (int i = 0; i < MAX_CLIENT_SIZE; i++)
    {
		// available
		if ((shm_cli[i].id == id) && (shm_cli[i].valid == 1))
        {
            ClientInfo* res = &shm_cli[i];
			return res;
		}
	}
    // cout << "No client id: " << id << endl;
    return NULL;  // no more available id
}
void SetNameFromSHM(int id, string name)
{
	ClientInfo *shm;
    int shm_idx = id-1;
    shm = GetCliSHM(g_shmid_cli);
	strcpy(shm[shm_idx].user_name, name.c_str());
	shmdt(shm);
}
/////////////////////
// fifo operations //
/////////////////////
void SigHandler (int sig)
{
	if (sig == SIGUSR1)  /* receive messages from others */
    {
		char* msg;
        if ((msg = (char*)shmat(g_shmid_msg, NULL, 0)) == (char* )-1) {
            fprintf(stderr, "Error: shmat() failed\n");
            ::exit(1);
        }
        if (sig == SIGUSR1) {
            int n = write(STDOUT_FILENO, msg, strlen(msg));
            if (n < 0) {
                fprintf(stderr, "Error: broadcast_catch() failed\n");
            }
        }
        shmdt(msg);
	}
    else if (sig == SIGUSR2)  /* open fifos to read from */
    {
        FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
		int	i;
		for (i = 0; i < MAX_CLIENT_SIZE; ++i)
        {
			if (shm_fifo->fifo[i][cur_id-1].out_fd == -1 && shm_fifo->fifo[i][cur_id-1].name[0] != 0)
            {
                shm_fifo->fifo[i][cur_id-1].out_fd = open(shm_fifo->fifo[i][cur_id-1].name, O_RDONLY);
            }
				
		}
        shmdt(shm_fifo);
	}
    else if (sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
    {
        // clean client
        Broadcast("logout", "", cur_id, -1);
        FIFOInfo* shm_fifo = GetFIFOSHM(g_shmid_fifo);
        for (size_t i = 0; i < MAX_CLIENT_SIZE; i++)
        {
            if (shm_fifo->fifo[i][cur_id-1].out_fd!=-1)
            {
                // read out message in the unused fifo
                char buf[1024];
                while(read(shm_fifo->fifo[i][cur_id-1].out_fd, &buf, sizeof(buf)) > 0){}
                shm_fifo->fifo[i][cur_id-1].out_fd = -1;
                shm_fifo->fifo[i][cur_id-1].in_fd = -1;
                shm_fifo->fifo[i][cur_id-1].is_used = false;
                unlink(shm_fifo->fifo[i][cur_id-1].name);
                memset(shm_fifo->fifo[i][cur_id-1].name, 0, sizeof(shm_fifo->fifo[cur_id-1][i].name));
            }
        }
	}
	signal(sig, SigHandler);
}