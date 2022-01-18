CXXFLAGS = -std=gnu++0x -Wall
LDLIBS = -lstdc++
.PHONY: all clean test
all: light_websocket_example
clean:
	-rm  light_websocket_example *.o

light_websocket_example: 	light_websocket_example.o 		light_websocket_client.o
light_websocket_example.o: 	light_websocket_example.cpp 	light_websocket_client.hpp
light_websocket_client.o: 	light_websocket_client.cpp 		light_websocket_client.hpp
