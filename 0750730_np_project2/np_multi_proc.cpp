#include "np_multi_proc.h"


using namespace std;

int AddClient(int id, int sockfd, struct sockaddr_in address);
void ServerSigHandler (int sig);

int main(int argc, char* argv[])
{
    // server variable
    int sockfd, newsockfd, childpid;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    if (argc != 2)
    {
        cerr << "./np_multi_proc [port]" << endl;
        exit(-1);
    }
    int port = atoi(argv[1]);
    // create tcp
    sockfd = PassiveTCP(port);
    cout << "Server sockfd: " << sockfd << " port: " << port << endl;
    signal (SIGCHLD, ServerSigHandler);
	signal (SIGINT, ServerSigHandler);
	signal (SIGQUIT, ServerSigHandler);
	signal (SIGTERM, ServerSigHandler);
    // init PATH
    setenv("PATH", "bin:.", 1);
    // init shm, msg, fifo
    InitSHM();
    while (true) 
    {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0)
        {
            cerr << "Error: accept failed" << endl;
            continue;
        }
        cout << "New sockfd: " << newsockfd << endl;
        childpid = fork();
        while (childpid < 0)
            childpid = fork();
        if (childpid == 0)
        {
            dup2(newsockfd, STDIN_FILENO);
            dup2(newsockfd, STDOUT_FILENO);
            dup2(newsockfd, STDERR_FILENO);
            close(newsockfd);
            close(sockfd);
            int id = GetAvaliableIDFromSHM();
            int add_status = AddClient(id, newsockfd, cli_addr);
            Shell shell;
            int status = shell.ClientExec(id);
            if (status == -1)
            {
                Broadcast("logout", "", cur_id, -1);
                // client
                ClientInfo* shm_cli = GetCliSHM(g_shmid_cli);
                shm_cli[cur_id-1].valid = 0;
                shmdt(shm_cli);
                // fifo
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
                shmdt(shm_fifo);
                close(STDIN_FILENO);
                close(STDOUT_FILENO);
                close(STDERR_FILENO);
                exit(0);
            }
        }
        else
            close(newsockfd);
    }
    close(sockfd);
    return 0;
}

int AddClient(int id, int sockfd, struct sockaddr_in address)
{
	ClientInfo* shm;
    int shm_idx = id-1;
	if (id < 0) {
		fprintf(stderr, "Error: get_new_id() failed\n");
		return 0;
	}
	// share memory attach
	if ((shm = (ClientInfo*)shmat(g_shmid_cli, NULL, 0)) == (ClientInfo*)-1)
    {
		fprintf(stderr, "Error: init_new_client() failed\n");
		return 0;
	}
	shm[shm_idx].valid = 1;
    shm[shm_idx].id = id;
	shm[shm_idx].pid = getpid();
	shm[shm_idx].port = ntohs(address.sin_port);
    strncpy(shm[shm_idx].user_ip, inet_ntoa(address.sin_addr), INET_ADDRSTRLEN);
	// using client.h api
	SetNameFromSHM(id, "(no name)");
	shmdt(shm);
	// success
	return 1;
}
void DelSHM() {
	// delete all
	shmctl(g_shmid_cli, IPC_RMID, NULL);
	shmctl(g_shmid_msg, IPC_RMID, NULL);
    shmctl(g_shmid_fifo, IPC_RMID, NULL);
}
void ServerSigHandler(int sig)
{
	if (sig == SIGCHLD)
    {
		while(waitpid (-1, NULL, WNOHANG) > 0);
	}
    else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
    {
		DelSHM();
		exit (0);
	}
	signal (sig, ServerSigHandler);
}