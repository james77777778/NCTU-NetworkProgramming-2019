#include <sys/wait.h>
#include <array>
#include <cstdlib>
#include <stdlib.h>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <time.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>


using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

io_service global_io_service;

enum status_type
{
    request_granted = 0x5a,
    request_failed = 0x5b,
    request_failed_no_identd = 0x5c,
    request_failed_bad_user_id = 0x5d
};
enum command_type
{
    connect = 0x01,
    bind = 0x02
};
enum { max_length = 8192, bind_max_length = 100000 };

class ConnectMode : public enable_shared_from_this<ConnectMode>{
    public:
    ConnectMode(io_service &web_io_service, unsigned char *message, const shared_ptr<ip::tcp::socket> &socket) : in_socket(socket), out_socket(web_io_service), out_resolver(web_io_service)
    {
        char host_name[20];
        char port_number[10];
        std::fill(in_data.begin(),in_data.end(),0);
        std::fill(out_data.begin(),out_data.end(),0);
        memset(host_name, '\0', 20);
        memset(port_number, '\0', 10);
        for (int i=0; i<8; i++)
            browser_message[i] = message[i];
        sprintf(host_name, "%u.%u.%u.%u", browser_message[4], browser_message[5], browser_message[6], browser_message[7]);
        sprintf(port_number, "%d", (int)browser_message[2]*256+(int)browser_message[3]);
        out_addr = string(host_name);
        out_port = string(port_number);
    }
    void start()
    {
        do_resolve();
    }

    private:
    shared_ptr<ip::tcp::socket> in_socket;
    ip::tcp::socket out_socket;
    ip::tcp::resolver out_resolver;
    string out_addr;
    string out_port;
    string host_ = "httpbin.org";
    array<char, max_length> in_data;
    array<char, max_length> out_data;
    unsigned char reply_to_browser[8];
    unsigned char browser_message[8];
    void do_resolve(){
        auto self(shared_from_this());
        tcp::resolver::query query(host_, out_port);
        out_resolver.async_resolve(query,
            boost::bind(&ConnectMode::do_connect, self,
            boost::asio::placeholders::error,
            boost::asio::placeholders::iterator));
    }
    void do_connect(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator){
        if (!err) {
            auto self(shared_from_this());
            ip::tcp::endpoint dst_endpoint(boost::asio::ip::address::from_string(out_addr), atoi(out_port.c_str()));
            out_socket.async_connect(dst_endpoint, boost::bind(&ConnectMode::connect_handler, self, boost::asio::placeholders::error));
        }
        else
            cout << "Error: " << err.message() << endl;
    }
    void connect_handler(const boost::system::error_code& err) {
        auto self(shared_from_this());
        if (!err){
            reply_to_browser[0] = 0;
            reply_to_browser[1] = status_type::request_granted;
            reply_to_browser[2] = browser_message[2];
            reply_to_browser[3] = browser_message[3];
            reply_to_browser[4] = browser_message[4];
            reply_to_browser[5] = browser_message[5];
            reply_to_browser[6] = browser_message[6];
            reply_to_browser[7] = browser_message[7];
            in_socket->async_send(buffer(reply_to_browser, 8), [this, self](boost::system::error_code err, std::size_t length_){
                if(!err) {
                    do_read(3);
                }
                else
                    cout << "async_send error!" << endl;
            });
        }
        else
            cout << "connect error!" << err.message() << endl;
    }
    void do_read(int direction) {
        auto self(shared_from_this());
        if (direction & 0x1) {
            in_socket->async_read_some(buffer(in_data, max_length), [this, self](boost::system::error_code err, std::size_t len) {
                if (!err)
                {
                    do_write(1, len);
                }
                else if (err == boost::asio::error::eof) {
                    out_socket.async_send(
                    buffer(in_data, len),
                    [self](boost::system::error_code err, std::size_t /* length */) {
                        if (!err){}
                        else {
                            cout << "send error!" << err.message() << endl;
                        } 
                    });
                    // cout << "in_socket close" << endl;
                }
                else {
                    cout << "read error!" << err.message() << endl;
                }
                    
            });
        }
        if (direction & 0x2) {
            out_socket.async_read_some(buffer(out_data, max_length), [this, self](boost::system::error_code err, std::size_t len) {
                if (!err){
                    
                    do_write(2, len);
                }
                else if (err == boost::asio::error::eof) {
                    in_socket->async_send(
                    buffer(out_data, len),
                    [self](boost::system::error_code err, std::size_t len) {
                        if (!err) {}
                        else {
                            cout << "send error!" << err.message() << endl;
                        } 
                    });
                    // cout << "out_socket close" << endl;
                }
                else {
                    cout << "read error!" << err.message() << endl;
                } 
            });
        }
    }
    void do_write(int direction, std::size_t len) {
        auto self(shared_from_this());
        switch(direction) {
        case 1:
            out_socket.async_send(
            buffer(in_data, len),
            [this, self, direction](boost::system::error_code err, std::size_t /* length */) {
                if (!err){
                    do_read(direction);
                }
                else {
                    cout << "send error!" << err.message() << endl;
                } 
            });
            break;
        case 2:
            in_socket->async_send(
            buffer(out_data, len),
            [this, self, direction](boost::system::error_code err, std::size_t len) {
                if (!err) {
                    do_read(direction);
                }
                else {
                    cout << "send error!" << err.message() << endl;
                } 
            });
            break;
        }
    }
};

class BindMode: public enable_shared_from_this<BindMode>{
    public:
    BindMode(io_service &web_io_service, unsigned char *message, const shared_ptr<ip::tcp::socket> &socket):out_socket(web_io_service),in_socket(socket),bind_acceptor(web_io_service){
            std::fill(in_data.begin(),in_data.end(),0);
            std::fill(out_data.begin(),out_data.end(),0);
        }
        void start(){
            handle_bind_mode();
        }
    private:
    ip::tcp::socket out_socket;
    shared_ptr<ip::tcp::socket> in_socket;
    ip::tcp::acceptor bind_acceptor;
    unsigned short bind_port;
    array<char, bind_max_length> in_data;
    array<char, bind_max_length> out_data;
    unsigned char reply_to_client[8];
    void handle_bind_mode(){
        unsigned short os_free_port(0);
        ip::tcp::endpoint endPoint(ip::tcp::endpoint(ip::tcp::v4(), os_free_port));
        bind_acceptor.open(endPoint.protocol());
        bind_acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
        bind_acceptor.bind(endPoint);
        bind_acceptor.listen();
        bind_port = bind_acceptor.local_endpoint().port();
        reply_to_client[0] = 0;
        reply_to_client[1] = status_type::request_granted;
        reply_to_client[2] = (unsigned char)(bind_port/256);
        reply_to_client[3] = (unsigned char)(bind_port%256);
        reply_to_client[4] = 0;
        reply_to_client[5] = 0;
        reply_to_client[6] = 0;
        reply_to_client[7] = 0;
        auto self(shared_from_this());
        in_socket->async_send(buffer(reply_to_client, 8),
            [this,self](boost::system::error_code err, std::size_t length_){
                if(!err) {
                    auto self(shared_from_this());
                    bind_acceptor.async_accept(out_socket, [this,self](boost::system::error_code err2) {
                        if (!err2)
                            in_socket->async_send(buffer(reply_to_client, 8),
                                [this,self](boost::system::error_code err, std::size_t len){
                                    if(!err) {
                                        do_read(3);
                                    }
                                    else
                                        cout << "accept error" << err.message() << endl;
                                });
                        else
                            cout << "accept error" << err2.message() << endl;
                    });
                }
                else{
                    cout << "send error" << err.message() << endl;
                }
            });
    }
    void do_read(int direction) {
        auto self(shared_from_this());
        if (direction & 0x1) {
            in_socket->async_read_some(buffer(in_data, bind_max_length), [this, self](boost::system::error_code err, std::size_t len_from_in) {
                if (!err)
                {
                    do_write(1, len_from_in);
                }
                else if (err == boost::asio::error::eof) {
                    out_socket.async_send(
                    buffer(in_data, len_from_in),
                    [self](boost::system::error_code err, std::size_t /* length */) {
                        if (!err){}
                        else {
                            cout << "send error!" << err.message() << endl;
                        } 
                    });
                    out_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, err);
                }
                else {
                    cout << "read error!" << err.message() << endl;
                }
                    
            });
        }
        if (direction & 0x2) {
            out_socket.async_read_some(buffer(out_data, bind_max_length), [this, self](boost::system::error_code err, std::size_t len_from_out) {
                if (!err){
                    
                    do_write(2, len_from_out);
                }
                else if (err == boost::asio::error::eof) {
                    in_socket->async_send(
                    buffer(out_data, len_from_out),
                    [self](boost::system::error_code err, std::size_t len) {
                        if (!err) {}
                        else {
                            cout << "send error!" << err.message() << endl;
                        } 
                    });
                    in_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, err);
                }
                else {
                    cout << "read error!" << err.message() << endl;
                } 
            });
        }
    }
    void do_write(int direction, std::size_t len) {
        auto self(shared_from_this());
        switch(direction) {
        case 1:
            out_socket.async_send(
            buffer(in_data, len),
            [this, self, direction, len](boost::system::error_code err, std::size_t len_to_out) {
                if (!err){
                    if (len == len_to_out)
                        do_read(direction);
                    else
                        do_write(direction, len_to_out-len);
                }
                else {
                    cout << "send error!" << err.message() << endl;
                } 
            });
            break;
        case 2:
            in_socket->async_send(
            buffer(out_data, len),
            [this, self, direction, len](boost::system::error_code err, std::size_t len_to_in) {
                if (!err) {
                    if (len == len_to_in)
                        do_read(direction);
                    else
                        do_write(direction, len_to_in-len);
                }
                else {
                    cout << "send error!" << err.message() << endl;
                } 
            });
            break;
        }
    }
};

class SocksSession : public enable_shared_from_this<SocksSession>
{
    private:
    ip::tcp::socket _socket;
    unsigned char message[1024] = {0};
    void read_info()
    {
        auto self(shared_from_this());
        _socket.async_read_some(buffer(message, 1024), [this, self](boost::system::error_code e, std::size_t len) {
            if (!e)
            {
                unsigned char VN = message[0];
                unsigned char CD = message[1];
                int is_granted=0;    
                FILE *fp;
                if((fp = fopen("socks.conf","r"))==NULL)
                    cerr<<"Can not open file sock_conf\n";
                char *check=NULL;
                char rule[10];
                char mode[10];
                char addr[20];
                unsigned char addr_pass[4]={'0'};

                while(!feof(fp)) {
                    fscanf(fp, "%s %s %s", rule, mode, addr);
                    if(feof(fp))
                        break;
                    check = strtok(addr, ".");
                    addr_pass[0] = (unsigned char)atoi(check);
                    check = strtok(NULL, ".");
                    addr_pass[1] = (unsigned char)atoi(check);
                    check = strtok(NULL, ".");
                    addr_pass[2] = (unsigned char)atoi(check);
                    check = strtok(NULL, ".");
                    addr_pass[3] = (unsigned char)atoi(check);
                    if((CD==1 && !strcmp(mode,"c")) || (CD==2 && !strcmp(mode,"b"))) {
                        if((addr_pass[0]==message[4]||addr_pass[0]==0)&&(addr_pass[1]==message[5]||addr_pass[1]==0)&&(addr_pass[2]==message[6]||addr_pass[2]==0)&&(addr_pass[3]==message[7]||addr_pass[3]==0)){
                             is_granted=1;
                             break;
                        }
                    }
                }
                fclose(fp);
                
                if (VN != 4){
                    cout << "not socks4 request\n";
                    is_granted =0;
                }

                cout << "<S_IP>: " << _socket.remote_endpoint().address() << endl;
                cout << "<S_PORT>: " << _socket.remote_endpoint().port() << endl;
                cout << "<D_IP>: " << (int)message[4] << "." << (int)message[5] << "." << (int)message[6] << "." << (int)message[7] << endl;
                cout << "<D_PORT>: " << (int)message[2]*256+(int)message[3] << endl;
                
                if(CD == command_type::connect)
                    cout << "<Command> CONNECT" << endl;
                else if(CD == command_type::bind)
                    cout << "<Command> BIND" << endl;
                
                if(is_granted == 0 ){
                    cout << "<Reply> Reject" << endl;
                    send_reject(message);
                    read_info();
                }
                else
                {
                    cout << "<Reply> Accept" << endl;
                    auto socket_ptr = make_shared<ip::tcp::socket>(move(_socket));
                    if(CD == command_type::connect) {
                        make_shared<ConnectMode>(global_io_service, message, socket_ptr)->start();
                    }else if (CD == command_type::bind) {
                        make_shared<BindMode>(global_io_service, message, socket_ptr)->start();
                    }
                }
            }
        });
    }
    void send_reject(unsigned char *reply_m){
        auto self(shared_from_this());
        unsigned char reply_to_browser[8];
        reply_to_browser[0] = 0;
        reply_to_browser[1] = status_type::request_failed;  // 91: request rejected or failed
        for(int i=2; i<8; i++)
            reply_to_browser[i]=reply_m[i];
        _socket.async_send(buffer(reply_to_browser, 8),[self](boost::system::error_code err, std::size_t len){
            if(!err){}
            else
                cout<<"error!"<<endl;
        });
    }
    
  public:
    SocksSession(ip::tcp::socket socket) : _socket(move(socket))
    {}
    void start()
    {
        read_info();
    }
};

class SocksServer
{
  private:
    ip::tcp::acceptor _acceptor;
    ip::tcp::socket _socket;
    void do_accept()
    {
        _acceptor.async_accept(_socket, [this](boost::system::error_code err) {
            if (!err){
                global_io_service.notify_fork(boost::asio::io_service::fork_prepare);
                pid_t child_pid;
                child_pid = fork();
                // make_shared<SocksSession>(move(_socket))->start();
                // SocksSession
                if (child_pid == 0) {
                    global_io_service.notify_fork(boost::asio::io_service::fork_child);
                    _acceptor.close();
                    make_shared<SocksSession>(move(_socket))->start();
                }
                else {
                    global_io_service.notify_fork(boost::asio::io_service::fork_parent);
                    _socket.close();
                }
            }
            do_accept();
        });
    }

  public:
    SocksServer(short port) : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)), _socket(global_io_service)
    {
        do_accept();
    }
};

int main(int argc, char *const argv[], char *envp[])
{
    if (argc != 2)
    {
        cerr << "Usage:" << argv[0] << " [port]" << endl;
        return 1;
    }
    try
    {
        short port = atoi(argv[1]);
        SocksServer server(port);
        global_io_service.run();
    }
    catch (exception &e)
    {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}