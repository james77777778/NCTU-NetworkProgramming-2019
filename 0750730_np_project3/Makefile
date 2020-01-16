CXX=clang++
CXXFLAGS=-std=c++11 -Wall -pedantic -pthread -lboost_system
CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))
CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))


all: http_server console

http_server: http_server.cpp
	$(CXX) $< -o $@ $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

console: console.cpp
	clang++ console.cpp -o console.cgi -std=c++11 -Wall -pedantic -pthread -lboost_system

main: main.cpp
	g++ main.cpp -o cgi_server -lwsock32 -lws2_32 -std=c++14

clean:
	rm -f *.cgi
