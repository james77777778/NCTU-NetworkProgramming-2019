#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <array>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>


using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::algorithm;

struct header
{
  std::string name;
  std::string value;
};
struct request
{
  std::string raw_uri;
  std::string method;
  int http_version_major;
  int http_version_minor;
  std::string protocol;
  std::string host;
  std::vector<header> headers;
};
struct Host {
    string hostname;
    int port;
    string filename;
};
enum result_type { good, bad, indeterminate };
enum state
{
  method_start,
  method,
  uri,
  http_version_h,
  http_version_t_1,
  http_version_t_2,
  http_version_p,
  http_version_slash,
  http_version_major_start,
  http_version_major,
  http_version_minor_start,
  http_version_minor,
  expecting_newline_1,
  header_line_start,
  header_lws,
  header_name,
  space_before_header_value,
  header_value,
  expecting_newline_2,
  expecting_newline_3
} state_;
const std::string ok_string = "HTTP/1.0 200 OK\r\n";
const std::string internal_server_error_string = "HTTP/1.0 500 Internal Server Error\r\n";
const char name_value_separator[] = { ':', ' ' };
const char crlf[] = { '\r', '\n' };
struct reply
{
  /// The status of the reply.
  enum status_type
  {
    ok = 200,
    internal_server_error = 500,
  } status;
  std::vector<header> headers;
  std::string content;
  std::vector<boost::asio::const_buffer> to_buffers();
};
std::string reply_to_string(reply::status_type status)
{
  switch (status)
  {
  case reply::ok:
    return ok_string;
  default:
    return internal_server_error_string;
  }
}
/// Get a stock reply.
reply stock_reply(reply::status_type status) {
  reply rep;
  rep.status = status;
  rep.content = reply_to_string(status);
  rep.headers.resize(2);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = std::to_string(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = "text/html";
  return rep;
};
boost::asio::const_buffer to_buffer(reply::status_type status)
{
  switch (status)
  {
  case reply::ok:
    return boost::asio::buffer(ok_string);
  default:
    return boost::asio::buffer(internal_server_error_string);
  }
}
std::vector<boost::asio::const_buffer> reply::to_buffers()
{
  std::vector<boost::asio::const_buffer> buffers;
  buffers.push_back(to_buffer(status));
  for (std::size_t i = 0; i < headers.size(); ++i)
  {
    header& h = headers[i];
    buffers.push_back(boost::asio::buffer(h.name));
    buffers.push_back(boost::asio::buffer(name_value_separator));
    buffers.push_back(boost::asio::buffer(h.value));
    buffers.push_back(boost::asio::buffer(crlf));
  }
  buffers.push_back(boost::asio::buffer(crlf));
  buffers.push_back(boost::asio::buffer(content));
  return buffers;
}

enum { max_length = 4096 };
io_service global_io_service;
io_service console_io_service;
typedef vector< string > split_vector_type;
Host host[5];

////////////////////////////////////////////////////////////////////////////////
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
            else {
                std::cout << "Error: " << ec.message() << " value = " << ec.value() << "\n";
            }
            do_read();
        });
    }
    bool do_send_cmd(size_t bytes_transferred) {
        auto self(shared_from_this());
        // get one-line command
        string cmd;
        getline(fileout, cmd);
        string cmd_send = cmd + "\n";
        // cout << cmd_send << flush;
        boost::asio::async_write(socket_, boost::asio::buffer(cmd_send.c_str(), cmd_send.size()),
        [this, self](boost::system::error_code ec, std::size_t)
        {
        });
        if (exit_ == false)
            output_command(cmd_send);
        // prepare to close socket
        if (cmd == "exit")
            return true;
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
        auto self(shared_from_this());
        string tmp = s;
        encode(tmp);
        string out = "<script>document.getElementById('s" + std::to_string(index) + "')";
        out += ".innerHTML += '" + tmp + "';</script>";
        socket_.async_send(
            buffer(out.c_str(), out.length()),
            [this, self](boost::system::error_code ec, std::size_t /* length */) {}
        );
    }
    void output_command(const string s) {
        auto self(shared_from_this());
        string tmp = s;
        encode(tmp);
        string out = "<script>document.getElementById('s" + std::to_string(index) + "')";
        out += ".innerHTML += '<b>" + tmp + "</b>';</script>";
        socket_.async_send(
            buffer(out.c_str(), out.length()),
            [this, self](boost::system::error_code ec, std::size_t /* length */) {}
        );
    }
};

class ShellServer : public enable_shared_from_this<ShellServer>{
    private:
    tcp::resolver resolver_;
    ip::tcp::socket *socket_[5];
    ip::tcp::socket server_socket_;
    string QUERY_STRING;

    public:
    ShellServer(string q_string, ip::tcp::socket server_socket)
        : resolver_(global_io_service),
          server_socket_(move(server_socket)) {
        QUERY_STRING = q_string;
        do_resolve();
    }

    private:
    void pre_output() {
        auto self(shared_from_this());
        string head = "Content-type: text/html\r\n\r\n";
        server_socket_.async_send(
			buffer(head.c_str(), head.length()),
			[this, self](boost::system::error_code ec, std::size_t /* length */) {}
		);
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
        server_socket_.async_send(
            buffer(content.c_str(), content.length()),
            [this, self](boost::system::error_code ec, std::size_t /* length */) {}
        );
    }
    void do_resolve() {
        auto self(shared_from_this());
        string qstring = QUERY_STRING;
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
                    boost::bind(&ShellServer::do_connect, self,
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
            socket_[i] = new ip::tcp::socket(console_io_service);
            tcp::endpoint endpoint = *endpoint_iterator;
            (*socket_[i]).async_connect(endpoint, boost::bind(&ShellServer::handle_connect, this, i, filename, boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
            std::cout << "Error: " << err.message() << "\n";
    }
    void handle_connect(const int i, string filename, const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator)
    {
        auto self(shared_from_this());
        if (!err)
        {
            // The connection was successful. Send the request.
            // cout << "connect success" << endl << flush;
            make_shared<ShellSession>(move((*socket_[i])), filename, i)->start();
            return;
        }
        else if (endpoint_iterator != tcp::resolver::iterator())
        {
            // The connection failed. Try the next endpoint in the list.
            (*socket_[i]).close();
            tcp::endpoint endpoint = *endpoint_iterator;
            (*socket_[i]).async_connect(endpoint, boost::bind(&ShellServer::handle_connect, self, i, filename, boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
            std::cout << "Error: " << err.message() << "\n";
    }
};
////////////////////////////////////////////////////////////////////////////////
class HttpSession : public enable_shared_from_this<HttpSession> {
    private:
    ip::tcp::socket _socket;
    array<char, max_length> _data;
    request _request;
    result_type _result;
    reply _reply;
    string REQUEST_METHOD, REQUEST_URI, QUERY_STRING;
	string SERVER_PROTOCOL, HTTP_HOST;
	string SERVER_ADDR, SERVER_PORT, REMOTE_ADDR, REMOTE_PORT;
	string cgi_name;

    public:
    HttpSession(ip::tcp::socket socket) : _socket(move(socket)) {}
    void start() {
        // cli addr
        state_ = method_start;
        do_read();
        return;
    }
    // write
    void do_write()
    {
        auto self(shared_from_this());
        boost::asio::async_write(_socket, _reply.to_buffers(),
        [this, self](boost::system::error_code ec, std::size_t)
        {
            if (!ec)
            {
                // Initiate graceful connection closure.
                boost::system::error_code ignored_ec;
                _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                ignored_ec);
            }
        });
    }
    // read
    void do_read()
    {
        auto self(shared_from_this());
        _socket.async_read_some(
        buffer(_data, max_length),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (!ec) {
                std::tie(_result, std::ignore) = parse(_request, _data.data(), _data.data() + bytes_transferred);
                if (_result == good)
                {
                    // cout << "parse good" << endl;
                    handle_request(_request, _reply);
                }
                else if (_result == bad)
                    ;
                    // cout << "parse bad" << endl;
                else
                    do_read();
            }
        });
    }
    // parse
    template <typename InputIterator>
    std::tuple<result_type, InputIterator> parse(request& req,
            InputIterator begin, InputIterator end)
    {
        while (begin != end)
        {
        result_type result = consume(req, *begin++);
        if (result == good || result == bad)
            return std::make_tuple(result, begin);
        }
        return std::make_tuple(indeterminate, begin);
    }
    // check methods for consume
    bool is_char(int c)
    {
        return c >= 0 && c <= 127;
    }
    bool is_ctl(int c)
    {
        return (c >= 0 && c <= 31) || (c == 127);
    }
    bool is_tspecial(int c)
    {
        switch (c)
        {
        case '(': case ')': case '<': case '>': case '@':
        case ',': case ';': case ':': case '\\': case '"':
        case '/': case '[': case ']': case '?': case '=':
        case '{': case '}': case ' ': case '\t':
        return true;
        default:
        return false;
        }
    }
    bool is_digit(int c)
    {
        return c >= '0' && c <= '9';
    }
    // consume
    result_type consume(request& req, char input)
    {
        switch (state_)
        {
        case method_start:
        if (!is_char(input) || is_ctl(input) || is_tspecial(input))
            return bad;
        else {
            state_ = method;
            req.method.push_back(input);
            return indeterminate;}
        case method:
        if (input == ' ') {
            state_ = uri;
            return indeterminate;}
        else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
            return bad;
        else {
            req.method.push_back(input);
            return indeterminate;}
        case uri:
        if (input == ' ') {
            state_ = http_version_h;
            return indeterminate;}
        else if (is_ctl(input))
            return bad;
        else{
            req.raw_uri.push_back(input);
            return indeterminate;}
        case http_version_h:
        if (input == 'H') {
            state_ = http_version_t_1;
            return indeterminate;}
        else
            return bad;
        case http_version_t_1:
        if (input == 'T') {
            state_ = http_version_t_2;
            return indeterminate;}
        else
            return bad;
        case http_version_t_2:
        if (input == 'T') {
            state_ = http_version_p;
            return indeterminate;}
        else
            return bad;
        case http_version_p:
        if (input == 'P') {
            state_ = http_version_slash;
            return indeterminate;}
        else
            return bad;
        case http_version_slash:
        if (input == '/') {
            req.http_version_major = 0;
            req.http_version_minor = 0;
            state_ = http_version_major_start;
            return indeterminate;}
        else
            return bad;
        case http_version_major_start:
        if (is_digit(input)) {
            req.http_version_major = req.http_version_major * 10 + input - '0';
            state_ = http_version_major;
            return indeterminate;}
        else
            return bad;
        case http_version_major:
        if (input == '.') {
            state_ = http_version_minor_start;
            return indeterminate;}
        else if (is_digit(input)){
            req.http_version_major = req.http_version_major * 10 + input - '0';
            return indeterminate;}
        else
            return bad;
        case http_version_minor_start:
        if (is_digit(input)) {
            req.http_version_minor = req.http_version_minor * 10 + input - '0';
            state_ = http_version_minor;
            return indeterminate;}
        else
            return bad;
        case http_version_minor:
        if (input == '\r') {
            req.protocol = "HTTP/"+std::to_string(req.http_version_major)+"."+std::to_string(req.http_version_minor);
            state_ = expecting_newline_1;
            return indeterminate;}
        else if (is_digit(input)) {
            req.http_version_minor = req.http_version_minor * 10 + input - '0';
            return indeterminate;}
        else
            return bad;
        case expecting_newline_1:
        if (input == '\n') {
            state_ = header_line_start;
            return indeterminate;}
        else
            return bad;
        case header_line_start:
        if (input == '\r') {
            state_ = expecting_newline_3;
            return indeterminate;}
        else if (!req.headers.empty() && (input == ' ' || input == '\t')) {
            state_ = header_lws;
            return indeterminate;}
        else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
            return bad;
        else {
            req.headers.push_back(header());
            req.headers.back().name.push_back(input);
            state_ = header_name;
            return indeterminate;}
        case header_lws:
        if (input == '\r') {
            state_ = expecting_newline_2;
            return indeterminate;}
        else if (input == ' ' || input == '\t')
            return indeterminate;
        else if (is_ctl(input))
            return bad;
        else {
            state_ = header_value;
            req.headers.back().value.push_back(input);
            return indeterminate;}
        case header_name:
        if (input == ':') {
            state_ = space_before_header_value;
            return indeterminate;}
        else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
            return bad;
        else {
            req.headers.back().name.push_back(input);
            return indeterminate;}
        case space_before_header_value:
        if (input == ' ') {
            state_ = header_value;
            return indeterminate;}
        else
            return bad;
        case header_value:
        if (input == '\r') {
            if (req.headers.back().name == "Host")
                req.host = req.headers.back().value;
            state_ = expecting_newline_2;
            return indeterminate;}
        else if (is_ctl(input))
            return bad;
        else {
            req.headers.back().value.push_back(input);
            return indeterminate;}
        case expecting_newline_2:
        if (input == '\n') {
            state_ = header_line_start;
            return indeterminate;}
        else
            return bad;
        case expecting_newline_3:
        return (input == '\n') ? good : bad;
        default:
        return bad;
        }
    }
    void OK (std::string proto)
    {
        auto self(shared_from_this());
        string buff = proto + " 200 OK\nServer: sake\n";
        _socket.async_send(
			buffer(buff.c_str(), buff.length()),
			[this, self](boost::system::error_code ec, std::size_t /* length */) {}
		);
    }
    void setenv_client (const request& req) {
        REQUEST_METHOD = req.method;
        split_vector_type SplitVec;
        split(SplitVec, req.raw_uri, is_any_of("?"), token_compress_on);
        REQUEST_URI = SplitVec[0];
        if (SplitVec.size()==1)
            QUERY_STRING = "";
        else
            QUERY_STRING = SplitVec[1];
        SERVER_PROTOCOL = req.protocol;
        split(SplitVec, req.host, is_any_of(":"), token_compress_on);
        HTTP_HOST = SplitVec[0];
        if (SplitVec.size()==1)
            SERVER_PORT = "80";
        else
            SERVER_PORT = SplitVec[1];
        boost::asio::ip::address local_addr = _socket.local_endpoint().address();
        boost::asio::ip::address remote_addr = _socket.remote_endpoint().address();
        SERVER_ADDR = local_addr.to_string();
        REMOTE_ADDR = remote_addr.to_string();
        string port = std::to_string(_socket.remote_endpoint().port());
        REMOTE_PORT = port;
    }
    void fork_service(const request& req) {
        split_vector_type SplitVec;
        split(SplitVec, req.raw_uri, is_any_of("?"), token_compress_on);
        std::string cmd = SplitVec[0];
        cmd.erase(0, 1);
        // send OK
        OK(req.protocol);
        if (cmd == "panel.cgi")
			output_panel_cgi();
        if (cmd == "console.cgi")
		{
			// show the initial webpage of console
			console_io_service.reset();
            make_shared<ShellServer>(QUERY_STRING, move(_socket));
			// ShellServer shell_server(QUERY_STRING, _socket);
			console_io_service.run();
		}
    }
    void handle_request(const request& req, reply& rep) {
        // print HTTP Request content
        std::string content = "";
        content += req.method + "<br>";
        content += req.raw_uri + "<br>";
        content += req.protocol + "<br>";
        for (size_t i=0; i<req.headers.size(); i++) {
            content += req.headers[i].name + " " + req.headers[i].value + "<br>";
        }
        rep.content = content;
        rep.headers.resize(2);
        rep.headers[0].name = "Content-Length";
        rep.headers[0].value = std::to_string(rep.content.size());
        rep.headers[1].name = "Content-Type";
        rep.headers[1].value = "text/html";
        setenv_client(req);
        split_vector_type SplitVec;
        split(SplitVec, req.raw_uri, is_any_of("?"), token_compress_on);
        std::string cmd = SplitVec[0];
        if (boost::algorithm::contains(cmd, "cgi"))
            fork_service(req);
        else
            do_write();
    }
    void output_panel_cgi() {
        auto self(shared_from_this());
        string head = "Content-Type:text/html\r\n\r\n";
        _socket.async_send(
			buffer(head.c_str(), head.length()),
			[this, self](boost::system::error_code ec, std::size_t /* length */) {}
		);
        string content = "";
        content +=
"<!DOCTYPE html>"
"<html lang=\"en\">"
"  <head>"
"    <title>NP Project 3 Panel</title>"
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
"      href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\""
"    />"
"    <style>"
"      * {"
"        font-family: 'Source Code Pro', monospace;"
"      }"
"    </style>"
"  </head>"
"  <body class=\"bg-secondary pt-5\">"
"    <form action=\"console.cgi\" method=\"GET\">"
"      <table class=\"table mx-auto bg-light\" style=\"width: inherit\">"
"        <thead class=\"thead-dark\">"
"          <tr>"
"            <th scope=\"col\">#</th>"
"            <th scope=\"col\">Host</th>"
"            <th scope=\"col\">Port</th>"
"            <th scope=\"col\">Input File</th>"
"          </tr>"
"        </thead>"
"        <tbody>"
"          <tr>"
"            <th scope=\"row\" class=\"align-middle\">Session 1</th>"
"            <td>"
"              <div class=\"input-group\">"
"                <select name=\"h0\" class=\"custom-select\">"
"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
"                </select>"
"                <div class=\"input-group-append\">"
"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
"                </div>"
"              </div>"
"            </td>"
"            <td>"
"              <input name=\"p0\" type=\"text\" class=\"form-control\" size=\"5\" />"
"            </td>"
"            <td>"
"              <select name=\"f0\" class=\"custom-select\">"
"                <option></option>"
"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>"
"              </select>"
"            </td>"
"          </tr>"
"          <tr>"
"            <th scope=\"row\" class=\"align-middle\">Session 2</th>"
"            <td>"
"              <div class=\"input-group\">"
"                <select name=\"h1\" class=\"custom-select\">"
"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
"                </select>"
"                <div class=\"input-group-append\">"
"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
"                </div>"
"              </div>"
"            </td>"
"            <td>"
"              <input name=\"p1\" type=\"text\" class=\"form-control\" size=\"5\" />"
"            </td>"
"            <td>"
"              <select name=\"f1\" class=\"custom-select\">"
"                <option></option>"
"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>"
"              </select>"
"            </td>"
"          </tr>"
"          <tr>"
"            <th scope=\"row\" class=\"align-middle\">Session 3</th>"
"            <td>"
"              <div class=\"input-group\">"
"                <select name=\"h2\" class=\"custom-select\">"
"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
"                </select>"
"                <div class=\"input-group-append\">"
"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
"                </div>"
"              </div>"
"            </td>"
"            <td>"
"              <input name=\"p2\" type=\"text\" class=\"form-control\" size=\"5\" />"
"            </td>"
"            <td>"
"              <select name=\"f2\" class=\"custom-select\">"
"                <option></option>"
"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>"
"              </select>"
"            </td>"
"          </tr>"
"          <tr>"
"            <th scope=\"row\" class=\"align-middle\">Session 4</th>"
"            <td>"
"              <div class=\"input-group\">"
"                <select name=\"h3\" class=\"custom-select\">"
"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
"                </select>"
"                <div class=\"input-group-append\">"
"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
"                </div>"
"              </div>"
"            </td>"
"            <td>"
"              <input name=\"p3\" type=\"text\" class=\"form-control\" size=\"5\" />"
"            </td>"
"            <td>"
"              <select name=\"f3\" class=\"custom-select\">"
"                <option></option>"
"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>"
"              </select>"
"            </td>"
"          </tr>"
"          <tr>"
"            <th scope=\"row\" class=\"align-middle\">Session 5</th>"
"            <td>"
"              <div class=\"input-group\">"
"                <select name=\"h4\" class=\"custom-select\">"
"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
"                </select>"
"                <div class=\"input-group-append\">"
"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
"                </div>"
"              </div>"
"            </td>"
"            <td>"
"              <input name=\"p4\" type=\"text\" class=\"form-control\" size=\"5\" />"
"            </td>"
"            <td>"
"              <select name=\"f4\" class=\"custom-select\">"
"                <option></option>"
"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>"
"              </select>"
"            </td>"
"          </tr>"
"          <tr>"
"            <td colspan=\"3\"></td>"
"            <td>"
"              <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>"
"            </td>"
"          </tr>"
"        </tbody>"
"      </table>"
"    </form>"
"  </body>"
"</html>";

        _socket.async_send(
            buffer(content.c_str(), content.length()),
            [this, self](boost::system::error_code ec, std::size_t /* length */) {}
        );
    }
};
class HttpServer {
    private:
    ip::tcp::acceptor _acceptor;
    ip::tcp::socket _socket;
    public:
    HttpServer(short port)
        : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)),
            _socket(global_io_service) {
        do_accept();
    }
    private:
    void do_accept() {
        _acceptor.async_accept(_socket, [this](boost::system::error_code ec) {
        if (!ec) make_shared<HttpSession>(move(_socket))->start();
        do_accept();
        });
    }
};
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* const argv[]) {
    if (argc != 2) {
        cerr << "Usage:" << argv[0] << " [port]" << endl;
        return 1;
    }
    try {
        unsigned short port = atoi(argv[1]);
        HttpServer server(port);
        global_io_service.run();
    } catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}