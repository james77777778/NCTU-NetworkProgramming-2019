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
using namespace std;

#define BUFF_SIZE 2048
#define MAX_CLIENT_SIZE 30

// Pipe
struct PipeFd
{
    int in_fd;
    int out_fd;
    int count;
};
// User Pipe
struct UserPipeFd
{
    int in_fd;
    int out_fd;
    int source_id;
    int target_id;
    bool is_used;
};
// Environment Info
struct EnvInfo
{
    string env_var;
    string env_val;
};
// Client Info
struct ClientInfo{
    int fd;
    int id;
    string user_name;
    char user_ip[INET_ADDRSTRLEN];
    int port;
    string msg;
    vector<EnvInfo> env_vector;
    vector<PipeFd> pipe_vector_;  // pipe
    vector<UserPipeFd> userpipe_vector_;  // userpipe
};

// global server variable
int msock;
// create fd_set
fd_set rfds;
fd_set afds;
// create user variable
int id_table[MAX_CLIENT_SIZE];
vector<ClientInfo> client_table;

// declare functions needed by shell
vector<ClientInfo>::iterator GetClientByID(int id);
void BroadCast(string action, string msg, ClientInfo &cur_cli, int target_fd);

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
        // client
        struct ClientInfo *cli;
        int sockfd;
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
        int ClientExec(vector<ClientInfo>::iterator, string);
        void SetAllEnv(void);
        void EraseEnv(string);
        void Who(void);
        void Yell(string);
        void Tell(int, string);
        void Name(string);
        // client user pipe
        void CreateUserPipe(vector<UserPipeFd>&, int, int&);
        int GetUserPipeFd(vector<UserPipeFd>&, int, int&);
        void EraseUserPipeVector(vector<UserPipeFd>&);
};


int Shell::ClientExec(vector<ClientInfo>::iterator cur_cli_iter, string cli_msg)
{
    one_line_input_ = cli_msg;
    cli = &(*cur_cli_iter);
    // init sockfd
    sockfd = (*cli).fd;
    dup2(sockfd, 1);
    dup2(sockfd, 2);
    // initialize environment variables
    clearenv();
    SetAllEnv();
    pid_vector_.clear();
    // begin npshell program
    // check enter input
    if(one_line_input_.empty())
        return 0;
    ParseArgs();
    // PrintCmds(cmds_);
    int status = ExecCmds();
    cout << "% ";
    fflush(stdout);
    return status;
}

void Shell::SetAllEnv()
{
    vector<EnvInfo>::iterator iter = cli->env_vector.begin();
    while(iter != cli->env_vector.end())
    {
        SetEnv(iter->env_var, iter->env_val);
        iter++;
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

void Shell::EraseEnv(string var)
{
    vector<EnvInfo>::iterator iter = cli->env_vector.begin();
    while(iter != cli->env_vector.end())
    {
        if(iter->env_var == var)
        {
            cli->env_vector.erase(iter);
            return;
        }
        iter ++;
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

void Shell::PrintEnv(string var)
{
    char* val = getenv(var.c_str());
    if (val!=NULL)
        cout << val << endl;
}

void Shell::Who(void)
{
    int cur_id = 0;
    cout << "<ID>\t" << "<nickname>\t" << "<IP:port>\t"<<"<indicate me>"<< endl;
    for (size_t id = 0; id < MAX_CLIENT_SIZE; id++)
    {
        if (id_table[id] != 0)
        {
            cur_id = id + 1;
            vector<ClientInfo>::iterator iter = GetClientByID(cur_id);
            ClientInfo temp = (*iter);
            cout << temp.id << "\t" << temp.user_name << "\t" << temp.user_ip;
            cout << ":" << temp.port << "\t";
            if (cur_id == cli->id)
                cout << "<-me" << endl;
            else
                cout << endl;
        }
    }
}

void Shell::Yell(string msg)
{
    BroadCast("yell", msg, *cli, -1);
}

void Shell::Tell(int target_id, string msg)
{
    if (id_table[target_id-1] != 0)
    {
        vector<ClientInfo>::iterator iter = GetClientByID(target_id);
        msg = "*** " + cli->user_name + " told you ***: " + msg + "\n";
        write((*iter).fd, msg.c_str(), msg.length());
    }
    else
        cout << "*** Error: user #" << to_string(target_id) << " does not exist yet. ***" << endl;
}

void Shell::Name(string name)
{
    vector<ClientInfo>::iterator iter = client_table.begin();
    while (iter != client_table.end())
    {
        if ((*iter).user_name == name)
        {
            cout << "*** User '" + name + "' already exists. ***" << endl;
            return;
        }
        iter ++;
    }
    cli->user_name = name;
    BroadCast("name", "", *cli, -1);
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

int Shell::ExecCmds()
{
    bool is_first_argv = true;
    bool is_final_argv = false;
    string prog;
    vector<string> arguments;
    // pid
    bool is_using_pipe = false;
    bool line_ends = false;
    int redirect_in = -1;
    bool is_in_redirect = false;
    int in_fd=sockfd, out_fd=sockfd, err_fd=sockfd;
    // client status
    int status = 0;
    bool is_in_userpipe = false;
    bool need_close_userpipe = false;
    vector<int> need_erase_cli_ids;
    // client broadcast msg
    int recv_str_id = -1;
    int send_str_id = -1;
    while (!cmds_.empty())
    {
        // init fd
        if(!is_in_redirect && !is_in_userpipe)
            in_fd=sockfd, out_fd=sockfd, err_fd=sockfd;

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
                CreatePipe(cli->pipe_vector_, pipe_num, out_fd);
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
                CreatePipe(cli->pipe_vector_, pipe_num, out_fd);
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
                int target_id;
                // user-pipe
                for (int i = 1; i < cmds_.front().length(); i++)
                {
                    user_char[i-1] = cmds_.front()[i];
                    user_char[i] = '\0';
                }
                target_id = atoi(user_char);
                // target id does not exist
                if (id_table[target_id-1] == 0)
                {
                    cmds_.pop();
                    cout << "*** Error: user #" << target_id << " does not exist yet. ***" << endl;
                    // clear cmds_ queue
                    queue<string> empty;
                    swap(cmds_, empty);
                    return 0;
                }
                // target id exists
                else
                {
                    // check if user pipe already exists
                    vector<UserPipeFd>::iterator iter = cli->userpipe_vector_.begin();
                    while(iter != cli->userpipe_vector_.end())
                    {
                        if ((*iter).target_id == target_id)
                        {
                            cout << "*** Error: the pipe #" << cli->id << "->#" << target_id;
                            cout << " already exists. ***" << endl;
                            // clear cmds_ queue
                            queue<string> empty;
                            swap(cmds_, empty);
                            return 0;
                        }
                        iter += 1;
                    }
                    // no user pipe
                    CreateUserPipe(cli->userpipe_vector_, target_id, out_fd);
                    is_using_pipe = true;
                    is_in_userpipe = true;
                    cmds_.pop();
                    if (cmds_.empty())
                    {
                        line_ends = true;
                        is_first_argv = true;
                        is_final_argv = true;
                    }
                    // setup send_str_id
                    send_str_id = target_id;
                }
            }
            // encounter named pipe (in)
            else if ((cmds_.front().find('<') != string::npos) && (cmds_.front() != "<"))
            {
                // pipe: pipe num
                char user_char[3];
                int source_id;
                // user-pipe
                for (int i = 1; i < cmds_.front().length(); i++)
                {
                    user_char[i-1] = cmds_.front()[i];
                    user_char[i] = '\0';
                }
                source_id = atoi(user_char);
                // target id does not exist
                if (id_table[source_id-1] == 0)
                {
                    cmds_.pop();
                    cout << "*** Error: user #" << source_id << " does not exist yet. ***" << endl;
                    // clear cmds_ queue
                    queue<string> empty;
                    swap(cmds_, empty);
                    return 0;
                }
                // target id exists
                else
                {
                    vector<ClientInfo>::iterator iter = GetClientByID(source_id);
                    ClientInfo *source_cli = &(*iter);
                    need_erase_cli_ids;
                    int candidate_id = GetUserPipeFd(source_cli->userpipe_vector_, source_cli->id, in_fd);
                    cmds_.pop();
                    if (candidate_id!=-1)
                    {
                        // record source cli id to erase
                        need_erase_cli_ids.push_back(candidate_id);
                        need_close_userpipe = true;
                        is_in_userpipe = true;
                        // setup recv_str_id
                        recv_str_id = candidate_id;
                    }
                    else
                    {
                        // cannot find any userpipe's target id is current client id
                        cout << "*** Error: the pipe #" << source_id << "->#" << cli->id;
                        cout << " does not exist yet. ***" << endl;
                        // clear cmds_ queue
                        queue<string> empty;
                        swap(cmds_, empty);
                        return 0;
                    }
                    if (cmds_.empty())
                    {
                        line_ends = true;
                        is_first_argv = true;
                        is_final_argv = true;
                    }
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
                BroadCast("recv", line_input, *cli, recv_str_id);
                recv_str_id = -1;
            }
            if (send_str_id != -1)
            {
                string line_input = one_line_input_.substr(0, one_line_input_.length());
                if (line_input.back()=='\r')
                    line_input.pop_back();
                BroadCast("send", line_input, *cli, send_str_id);
                send_str_id = -1;
            }
            
            // pipe: get pipe (count == 0)
            bool need_close_pipe = GetPipeFd(cli->pipe_vector_, in_fd);
            // execute
            bool is_executable = IsExecutable(prog, path_vector_);
            status = ExecCmd(arguments, is_executable, in_fd, out_fd, err_fd, line_ends, is_using_pipe, redirect_in);
            is_final_argv = false;
            redirect_in = -1;
            is_in_redirect = false;
            is_in_userpipe = false;
            // pipe
            ErasePipeVector(cli->pipe_vector_);
            CountdownPipeVector(cli->pipe_vector_);
            if (need_close_pipe || need_close_userpipe)
            {
                close(in_fd);
                need_close_userpipe = false;
            }
            // erase userpipe in need_erase_cli_ids
            need_erase_cli_ids.push_back(cli->id);
            vector<int>::iterator iter = need_erase_cli_ids.begin();
            while (iter!=need_erase_cli_ids.end())
            {
                // cout << *iter << endl;
                vector<ClientInfo>::iterator cli_iter = GetClientByID(*iter);
                ClientInfo *candidate_cli = &(*cli_iter);
                EraseUserPipeVector(candidate_cli->userpipe_vector_);
                iter++;
            }
            need_erase_cli_ids.clear();
        }
    }
    return status;
}

bool Shell::IsQueueEmpty(queue<string> cmds)
{
    if (cmds.empty()) return true;
    return false;
}

int Shell::ExecCmd(vector<string> &arguments, bool is_executable, int in_fd, int out_fd, int err_fd, bool line_ends, bool is_using_pipe, int redirect_in)
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
        EraseEnv(args[1]);
        struct EnvInfo new_env;
        new_env.env_var = args[1];
        new_env.env_val = args[2];
        cli->env_vector.push_back(new_env);
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
    if (in_fd != sockfd)
        dup2(in_fd, STDIN_FILENO);
    if (out_fd != sockfd)
        dup2(out_fd, STDOUT_FILENO);
    if (err_fd != sockfd)
        dup2(err_fd, STDERR_FILENO);
    if (in_fd != sockfd)
        close(in_fd);
    if (out_fd != sockfd)
        close(out_fd);
    if (err_fd != sockfd)
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
void Shell::CreateUserPipe(vector<UserPipeFd> &userpipe_vector_, int target_id, int &in_fd)
{
    // no same user pipe
    int pipe_fd[2];
    if(pipe(pipe_fd) < 0)
    {
        perror("pipe error");
        ::exit(0);
    }
    UserPipeFd new_userpipe;
    new_userpipe.in_fd = pipe_fd[1];  // write fd
    new_userpipe.out_fd = pipe_fd[0];  // read fd
    new_userpipe.source_id = cli->id;
    new_userpipe.target_id = target_id;
    new_userpipe.is_used = false;
    userpipe_vector_.push_back(new_userpipe);
    in_fd = pipe_fd[1];
}

int Shell::GetUserPipeFd(vector<UserPipeFd>& userpipe_vector_, int source_id, int& in_fd)
{
    vector<UserPipeFd>::iterator iter = userpipe_vector_.begin();
    while(iter != userpipe_vector_.end())
    {
        // there is a userpipe's target id is current client id
        if (((*iter).source_id == source_id) && ((*iter).target_id == cli->id))
        {
            close((*iter).in_fd);
            in_fd = (*iter).out_fd;
            (*iter).is_used = true;  // flag this userpipe is used
            return source_id;
        }
        iter += 1;
    }
    return -1;
}

void Shell::EraseUserPipeVector(vector<UserPipeFd>& userpipe_vector_)
{
    vector<UserPipeFd>::iterator iter = userpipe_vector_.begin();
    while(iter != userpipe_vector_.end())
    {
        // there is a userpipe used
        if ((*iter).is_used == true)
        {
            // cout << "source: " << (*iter).source_id << "target: " << (*iter).target_id << endl;
            // close((*iter).in_fd);
            close((*iter).out_fd);
            userpipe_vector_.erase(iter);
        }
        else
            iter++;
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
int GetAvaliableID(int id_table[])
{
    for (size_t i = 0; i < MAX_CLIENT_SIZE; i++)
    {
        if (id_table[i] == 0)
        {
            id_table[i] = 1;
            return (i+1);
        }
    }
    return -1;  // no more available id
}

vector<ClientInfo>::iterator GetClientByFd(int fd)
{
    // struct ClientInfo *res;
    vector<ClientInfo>::iterator iter = client_table.begin();
    while (iter != client_table.end())
    {
        if ((*iter).fd == fd)
            return iter;
        iter ++;
    }
    cout << "No client fd: " << fd << endl;
    return iter;
}

vector<ClientInfo>::iterator GetClientByID(int id)
{
    // struct ClientInfo *res;
    vector<ClientInfo>::iterator iter = client_table.begin();
    while (iter != client_table.end())
    {
        if ((*iter).id == id)
            return iter;
        iter ++;
    }
    // cout << "No client id: " << id << endl;
    return iter;
}

void EraseClientVector(int id)
{
    vector<ClientInfo>::iterator iter = client_table.begin();
    while (iter != client_table.end())
    {
        vector<UserPipeFd>::iterator up_iter = (*iter).userpipe_vector_.begin();
        while (up_iter != (*iter).userpipe_vector_.end())
        {
            if ((*up_iter).target_id == id)
            {
                close((*up_iter).in_fd);
                close((*up_iter).out_fd);
                (*iter).userpipe_vector_.erase(up_iter);
            }   
            else
                up_iter ++;
        }
        
        
        if ((*iter).id == id)
        {
            (*iter).env_vector.clear();
            (*iter).userpipe_vector_.clear();
            client_table.erase(iter);
        }
        else
            iter ++;
    }
}

string GetEnvValue(string env_var, ClientInfo &cur_cli)
{
    vector<EnvInfo>::iterator iter = cur_cli.env_vector.begin();
    while (iter != cur_cli.env_vector.end())
    {
        if ((*iter).env_var == env_var)
            return (*iter).env_val;
        iter ++;
    }
    return "";
}

void PrintWelcome(int sockfd)
{
    string msg = "";
    msg += "****************************************\n";
    msg += "** Welcome to the information server. **\n";
    msg += "****************************************\n";
    if (write(sockfd, msg.c_str(), msg.length()) == -1)
        perror("send error");
}

void BroadCast(string action, string msg, ClientInfo &cur_cli, int target_id)
{
    int nfds = getdtablesize();
    string send_msg;
    vector<ClientInfo>::iterator tar_cli ;
    for(int fd = 0 ; fd < nfds ; fd ++)
    {
        if (fd == msock)
            continue;
        send_msg = "";
        if (FD_ISSET(fd, &afds))
        {
            if (action == "login")
                send_msg = "*** User '(no name)' entered from "+string(cur_cli.user_ip)+":"+to_string(cur_cli.port)+". ***\n";
            else if (action == "logout")
                send_msg = "*** User '"+cur_cli.user_name+"' left. ***\n";
            else if (action == "name")
                send_msg = "*** User from " +string(cur_cli.user_ip)+":"+to_string(cur_cli.port)+" is named '"+cur_cli.user_name+"'. ***\n";
            else if (action == "yell")
                send_msg = "*** "+cur_cli.user_name+" yelled ***: " + msg + "\n";
            else if (action == "send")
            {
                vector<ClientInfo>::iterator target_iter = GetClientByID(target_id);
                send_msg = "*** "+cur_cli.user_name+" (#"+to_string(cur_cli.id)+") just piped '";
                send_msg += msg+"' to "+ (*target_iter).user_name +" (#"+to_string((*target_iter).id)+") ***\n";
            }
            else if (action == "recv")
            {
                vector<ClientInfo>::iterator target_iter = GetClientByID(target_id);
                send_msg = "*** "+cur_cli.user_name+" (#"+to_string(cur_cli.id);
                send_msg += ") just received from "+(*target_iter).user_name+" (#";
                send_msg += to_string((*target_iter).id)+") by '"+msg+"' ***\n";
            }
            if (write(fd, send_msg.c_str(), send_msg.length()) == -1)
                perror("send error");
        }
    }
}