CXX=clang++
CXXFLAGS=-std=c++11 -Wall -pedantic
CPP_CGI_SRC=$(wildcard *.cpp)
CPP_CGI=$(patsubst %.cpp,%.cgi,$(CPP_CGI_SRC))

.PHONY: all install clean

all: $(CPP_CGI)

%.cgi: %.cpp
	$(CXX) $< -o $@ $(CXXFLAGS)

install: all | ~/public_html
	cp *.cgi ~/public_html

~/public_html:
	mkdir -p ~/public_html

clean:
	rm -f $(CPP_CGI)
