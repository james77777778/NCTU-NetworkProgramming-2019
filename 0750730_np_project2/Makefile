BIN_PATH := bin
COMMANDS_PATH := commands

all: np_simple np_single_proc np_multi_proc

np_simple: np_simple.cpp
	g++ np_simple.cpp -o np_simple

np_single_proc: np_single_proc.cpp
	g++ np_single_proc.cpp -o np_single_proc

np_multi_proc: np_multi_proc.cpp
	g++ np_multi_proc.cpp -o np_multi_proc

all_with_commands:
	mkdir -p $(BIN_PATH)
	g++ np_simple.cpp -o np_simple
	g++ $(COMMANDS_PATH)/noop.cpp -o $(BIN_PATH)/noop
	g++ $(COMMANDS_PATH)/number.cpp -o $(BIN_PATH)/number
	g++ $(COMMANDS_PATH)/removetag.cpp -o $(BIN_PATH)/removetag
	g++ $(COMMANDS_PATH)/removetag0.cpp -o $(BIN_PATH)/removetag0
clean:
	rm -f np_simple
	rm -f npshell
	rm -f np_single_proc
	rm -f np_multi_proc