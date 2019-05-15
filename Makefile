pa6: main.cpp ini_parser.cpp ini_parser.h message.cpp message.h connection.cpp connection.h
	g++ -g -Wall -std=c++11 -o pa6 main.cpp ini_parser.cpp ini_parser.h message.cpp message.h connection.cpp connection.h -lpthread -lssl -lcrypto
clean:
	rm -f pa6
