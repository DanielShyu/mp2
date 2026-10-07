CXX ?= c++
CXXFLAGS = -Wall -Wextra -std=c++17

TARGETS = client server

all: $(TARGETS)

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server

clean:
	rm -f $(TARGETS)