BIN_PATH := bin
COMMANDS_PATH := commands

all:shell.cpp
	mkdir -p $(BIN_PATH)
	g++ shell.cpp -o npshell
all_with_commands:
	mkdir -p $(BIN_PATH)
	g++ shell.cpp -o npshell
	g++ $(COMMANDS_PATH)/noop.cpp -o $(BIN_PATH)/noop
	g++ $(COMMANDS_PATH)/number.cpp -o $(BIN_PATH)/number
	g++ $(COMMANDS_PATH)/removetag.cpp -o $(BIN_PATH)/removetag
	g++ $(COMMANDS_PATH)/removetag0.cpp -o $(BIN_PATH)/removetag0
clean:
	rm -f npshell