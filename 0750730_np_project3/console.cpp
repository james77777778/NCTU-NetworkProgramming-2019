#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::algorithm;

enum { max_length = 4096 };
io_service global_io_service;

struct Host {
    string hostname;
    int port;
    string filename;
};
Host host[5];

class ShellSession : public enable_shared_from_this<ShellSession>{
    private:
    tcp::socket socket_;
    boost::asio::streambuf request_;
    boost::asio::streambuf response_;
    array<char, max_length> data_;
    string reply_;
    string filename_;
    const string filepath_ = "./test_case/";
    ifstream fileout;
    int index;
    bool exit_;
    public:
    ShellSession(ip::tcp::socket socket, string filename, const int i) : socket_(move(socket)) {
        exit_ = false;
        index = i;
        filename_ = filename;
        fileout.open(filepath_+filename_);
        if(!fileout.is_open()) {
            cout << "no file: " << filepath_+filename_ << endl;
            exit(1);
        }
    }
    void start() {
        reply_ = "";
        do_read(); 
    }
    private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length),
        [this, self](boost::system::error_code ec, size_t bytes_transferred){
            if (!ec) {
                string str = std::string(data_.data(), data_.data() + bytes_transferred);
                reply_ += str;
                output_shell(reply_);
                reply_ = "";
                if (boost::algorithm::contains(data_, "% ")) {
                    exit_ = do_send_cmd(bytes_transferred);
                    std::fill(data_.begin(),data_.end(),0);
                }
            }
            else if (ec == boost::asio::error::eof) {
                string str = std::string(data_.data(), data_.data() + bytes_transferred);
                reply_ += str;
                output_shell(reply_);
                reply_ = "";
                return;
            }
            else
                std::cout << "Error: " << ec.message() << " value = " << ec.value() << "\n";
            do_read();
        });
    }
    bool do_send_cmd(size_t bytes_transferred) {
        auto self(shared_from_this());
        // get one-line command
        string cmd;
        getline(fileout, cmd);
        string cmd_send = cmd + "\n";
        boost::asio::async_write(socket_, boost::asio::buffer(cmd_send.c_str(), cmd_send.size()),
        [self](boost::system::error_code ec, std::size_t)
        {
        });
        if (exit_ == false)
            output_command(cmd_send);
        // prepare to close socket
        if (cmd == "exit") {
            return true;
        }
        else
            return false;
    }
    void encode(std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for(size_t pos = 0; pos != data.size(); ++pos) {
            switch(data[pos]) {
                case '&':  buffer.append("&amp;");       break;
                case '\"': buffer.append("&quot;");      break;
                case '\'': buffer.append("&apos;");      break;
                case '<':  buffer.append("&lt;");        break;
                case '>':  buffer.append("&gt;");        break;
                case '\n': buffer.append("&NewLine;");   break;
                case '\r': buffer.append("");   break;
                default:   buffer.append(&data[pos], 1); break;
            }
        }
        data.swap(buffer);
    }
    void output_shell(const string s) {
        string tmp = s;
        encode(tmp);
        string out = "<script>document.getElementById('s" + std::to_string(index) + "')";
        out += ".innerHTML += '" + tmp + "';</script>";
        cout << out << flush;
        usleep (10000);
    }
    void output_command(const string s) {
        string tmp = s;
        encode(tmp);
        string out = "<script>document.getElementById('s" + std::to_string(index) + "')";
        out += ".innerHTML += '<b>" + tmp + "</b>';</script>";
        cout << out << flush;
        usleep (10000);
    }
};

class ShellServer {
    private:
    tcp::resolver resolver_;
    ip::tcp::socket *socket_[5];

    public:
    ShellServer()
        : resolver_(global_io_service) {
        do_resolve();
    }

    private:
    void pre_output() {
        cout << "Content-type: text/html\r\n\r\n" << flush;
        string content = "";
        content += 
"<!DOCTYPE html>"
"<html lang=\"en\">"
"  <head>"
"    <meta charset=\"UTF-8\" />"
"    <title>NP Project 3 Console</title>"
"    <link"
"      rel=\"stylesheet\""
"      href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\""
"      integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\""
"      crossorigin=\"anonymous\""
"    />"
"    <link"
"      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
"      rel=\"stylesheet\""
"    />"
"    <link"
"      rel=\"icon\""
"      type=\"image/png\""
"      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\""
"    />"
"    <style>"
"      * {"
"        font-family: 'Source Code Pro', monospace;"
"        font-size: 1rem !important;"
"      }"
"      body {"
"        background-color: #212529;"
"      }"
"      pre {"
"        color: #cccccc;"
"      }"
"      b {"
"        color: #ffffff;"
"      }"
"    </style>"
"  </head>"
"  <body>"
"    <table class=\"table table-dark table-bordered\">"
"      <thead>"
"        <tr>";
        for (size_t i = 0; i < 5; i++)
        {
            if (host[i].hostname != "")
                content += "          <th scope=\"col\">" + host[i].hostname + ":" + std::to_string(host[i].port) +"</th>";
        }
        content +=
"        </tr>"
"      </thead>"
"      <tbody>"
"        <tr>";
        for (size_t i = 0; i < 5; i++)
        {
            if (host[i].hostname != "")
                content += "          <td><pre id=\"s" + std::to_string(i) + "\" class=\"mb-0\"></pre></td>";
        }
        content +=
"        </tr>"
"      </tbody>"
"    </table>"
"  </body>"
"</html>";
        cout << content << flush;
    }
    void do_resolve() {
        char* qstring = getenv("QUERY_STRING");
        typedef vector< string > split_vector_type;
        split_vector_type split_query;
        split_vector_type split_item;
        split(split_query, qstring, is_any_of("&"), token_compress_on);
        for (size_t i = 0; i < split_query.size(); i+=3)
        {
            int now_idx = i/3;
            split(split_item, split_query[i], is_any_of("="), token_compress_on);
            if (split_item.size()>1)
                host[now_idx].hostname = split_item[1];
            else
                host[now_idx].hostname = "";
            split(split_item, split_query[i+1], is_any_of("="), token_compress_on);
            if (split_item.size()>1)
                host[now_idx].port = atoi(split_item[1].c_str());
            else
                host[now_idx].port = 0;
            split(split_item, split_query[i+2], is_any_of("="), token_compress_on);
            if (split_item.size()>1)
                host[now_idx].filename = split_item[1];
            else
                host[now_idx].filename = "";
        }
        pre_output();
        for (size_t i = 0; i < 5; i++)
        {
            // start asynchronous resolve
            if (host[i].hostname != "") {
                tcp::resolver::query query(host[i].hostname, std::to_string(host[i].port));
                resolver_.async_resolve(query,
                    boost::bind(&ShellServer::do_connect, this,
                    i,
                    host[i].filename,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
            }
        }
    }
    void do_connect(const int i, string filename, const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) {
        if (!err)
        {
            socket_[i] = new ip::tcp::socket(global_io_service);
            tcp::endpoint endpoint = *endpoint_iterator;
            (*socket_[i]).async_connect(endpoint, boost::bind(&ShellServer::handle_connect, this, i, filename, boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }
    void handle_connect(const int i, string filename, const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            make_shared<ShellSession>(move((*socket_[i])), filename, i)->start();
            return;
        }
        else if (endpoint_iterator != tcp::resolver::iterator())
        {
            (*socket_[i]).close();
            tcp::endpoint endpoint = *endpoint_iterator;
            (*socket_[i]).async_connect(endpoint, boost::bind(&ShellServer::handle_connect, this, i, filename, boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
            std::cout << "Error: " << err.message() << "\n";
    }
};

int main(int argc, char* argv[])
{
    try {
        ShellServer shell_server;
        global_io_service.run();
    } catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}