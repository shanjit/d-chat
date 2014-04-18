all: dchat.cpp
	g++ -std=c++11 -o dchat dchat.cpp -pthread
