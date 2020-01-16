#include <sys/types.h>
#include <sys/wait.h>
#include <array>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <fstream>

using namespace std;
using namespace boost::asio;
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


io_service global_io_service;
typedef vector< string > split_vector_type;

class HttpSession : public enable_shared_from_this<HttpSession> {
    private:
    enum { max_length = 1024 };
    ip::tcp::socket _socket;
    array<char, max_length> _data;
    request _request;
    result_type _result;
    reply _reply;

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
                    handle_request(_request, _reply);
                else if (_result == bad)
                    ;
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
			[self](boost::system::error_code ec, std::size_t /* length */) {}
		);
    }
    void setenv_client (const request& req) {
        setenv("REQUEST_METHOD", req.method.c_str(), 1);
        split_vector_type SplitVec;
        split(SplitVec, req.raw_uri, is_any_of("?"), token_compress_on);
        setenv("REQUEST_URI", SplitVec[0].c_str(), 1);
        if (SplitVec.size()==1)
            setenv("QUERY_STRING", "", 1);
        else
            setenv("QUERY_STRING", SplitVec[1].c_str(), 1);
        setenv("SERVER_PROTOCOL", req.protocol.c_str(), 1);
        split(SplitVec, req.host, is_any_of(":"), token_compress_on);
        setenv("HTTP_HOST", SplitVec[0].c_str(), 1);
        if (SplitVec.size()==1)
            setenv("SERVER_PORT", "80", 1);
        else
            setenv("SERVER_PORT", SplitVec[1].c_str(), 1);
        boost::asio::ip::address local_addr = _socket.local_endpoint().address();
        boost::asio::ip::address remote_addr = _socket.remote_endpoint().address();
        setenv("SERVER_ADDR", local_addr.to_string().c_str(), 1);
        setenv("REMOTE_ADDR", remote_addr.to_string().c_str(), 1);
        string port = std::to_string(_socket.remote_endpoint().port());
        setenv("REMOTE_PORT", port.c_str(), 1);
    }
    void fork_service(const request& req) {
        split_vector_type SplitVec;
        split(SplitVec, req.raw_uri, is_any_of("?"), token_compress_on);
        std::string cmd = SplitVec[0];
        cmd.erase(0, 1);
        pid_t child_pid;
        child_pid = fork();
        while (child_pid < 0) {
            usleep(1000);
            child_pid = fork();
        }
        // panel process
        if (child_pid == 0) {
            int socket_fd = _socket.native_handle();
            // cout << "fork: " << socket_fd << endl;
            dup2(socket_fd, STDOUT_FILENO);
            dup2(socket_fd, STDERR_FILENO);
            char *args[2] = {(char*)cmd.c_str(), NULL};
            OK(req.protocol);
            execvp(("./"+cmd).c_str(), args);
        }
        else {
            global_io_service.notify_fork(io_service::fork_parent);
            return;
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