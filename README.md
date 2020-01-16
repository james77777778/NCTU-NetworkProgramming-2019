# NetworkProgramming-2019-Fall
### HW1
A shell that supports numbered-pipe.
- Setup npshell
    ```bash
    ./npshell
    ```
- Connect npshell
    ```bash
    telnet [npshell_ip] [npshell_port] 
    ```

### HW2
Three different npshell.
1. Simple npshell that supports client connect.
2. One process npshell that supports communication between clients. (by FD tabel)
3. Multiprocess npshell that supports communication between clients. (by shared memory)
- Setup npshell
    ```bash
    ./np_simple [port]
    ./np_single_proc [port]
    ./np_multi_proc [port]
    ```
- Connect npshell
    ```bash
    telnet [npshell_ip] [npshell_port] 
    ```

### HW3
A http server with cgi program that can connect to npshell and execute commands from files.
- Setup http server
    ```bash
    ./http_server [port]
    ```
- Run
    Open browser to the page of panel.cgi

### HW4
A socks4 server for proxying.