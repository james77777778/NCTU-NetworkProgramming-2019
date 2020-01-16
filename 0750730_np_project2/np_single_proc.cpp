#include "np_single_proc.h"


using namespace std;


int main(int argc, char* argv[])
{
    // server variable
    struct sockaddr_in fsin; /* the from address of a client*/
    vector<ClientInfo>::iterator cur_cli_iter;
    if (argc != 2)
    {
        cerr << "./np_multi_proc [port]" << endl;
        exit(-1);
    }
    int port = atoi(argv[1]);
    // create tcp
    msock = PassiveTCP(port);
    cout << "Server sockfd: " << msock << endl;
    for (size_t i = 0; i < MAX_CLIENT_SIZE; i++)
        id_table[i] = 0;  // init id_table
    int nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock, &afds);

    Shell shell; // init shell

    int max_fd = msock;
    while (1) 
    {
        vector<ClientInfo>::iterator iter = client_table.begin();
        while (iter != client_table.end())
        {
            if ((*iter).fd > max_fd)
                max_fd = (*iter).fd;
            iter ++;
        }
        
        memcpy(&rfds, &afds, sizeof(rfds));
        // server listen to fd_sets
        int select_stat = -1;
        int select_error = 0;
        do
        {
            select_stat = select(max_fd+1, &rfds, NULL, NULL, NULL);
            if (select_stat<0)
                select_error = errno;
            
        } while ((select_stat < 0) && (select_error == EINTR));
        if (select_stat < 0)
            perror("other error");
        
        // server add new client
        if (FD_ISSET(msock, &rfds))
        {
            int ssock;
            socklen_t alen = sizeof(fsin);  /* from-address length */
            ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
            // string new_cli_msg = "new client: " + ssock;
            // write(msock, new_cli_msg.c_str(), new_cli_msg.length());
            if (ssock < 0)
                perror("accept error");
            else
            {
                struct ClientInfo new_cli;
                new_cli.fd = ssock;
                new_cli.id = GetAvaliableID(id_table);
                if (new_cli.id < 0)
                {
                    cout << "Reach max client number" << endl;
                    continue;
                }
                new_cli.env_vector.clear();
                new_cli.pipe_vector_.clear();
                new_cli.userpipe_vector_.clear();
                new_cli.user_name = "(no name)";
                strncpy(new_cli.user_ip, inet_ntoa(fsin.sin_addr), INET_ADDRSTRLEN);
                new_cli.port = ntohs(fsin.sin_port);;
                new_cli.msg = "";
                struct EnvInfo new_env;
                new_env.env_var = "PATH";
                new_env.env_val = "bin:.";
                new_cli.env_vector.push_back(new_env);
                
                client_table.push_back(new_cli);
                FD_SET(ssock, &afds);

                PrintWelcome(new_cli.fd);
                BroadCast("login", "", new_cli, -1);
                send(new_cli.fd, "% ", 2, 0);
            }
        }
        for (int fd=0; fd<max_fd+1; ++fd)
        {
            if (FD_ISSET(fd, &rfds) && fd != msock)
            {
                cur_cli_iter = GetClientByFd(fd);
                int nbytes;
                char buff[BUFF_SIZE];
                // client logout
                if ((nbytes = recv(fd, buff, sizeof(buff), 0)) <= 0)
                {
                    if (nbytes == 0)
                        cout << "socket: " << fd << "closed" << endl;
                    else
                        perror("receive error");
                    BroadCast("logout", "", (*cur_cli_iter), -1);
                    // erase order is very important!!!
                    id_table[((*cur_cli_iter).id-1)] = 0;
                    EraseClientVector((*cur_cli_iter).fd);
                    close(fd);
                    FD_CLR(fd, &afds);
                }
                // client has msg
                else
                {
                    int	stdfd[3];
                    buff[nbytes-1] = '\0';
                    string cli_msg(buff);
                    int status = shell.ClientExec(cur_cli_iter, cli_msg);
                    // client leave
                    if (status == -1)
                    {
                        BroadCast("logout", "", (*cur_cli_iter), -1);
                        // erase order is very important!!!
                        id_table[((*cur_cli_iter).id-1)] = 0;
                        EraseClientVector((*cur_cli_iter).id);
                        close(fd);
                        close(1);
                        close(2);
                        dup2(0, 1);
                        dup2(0, 2);
                        FD_CLR(fd, &afds);
                    }
                }
            }
        } 
    }
    return 0;
}
