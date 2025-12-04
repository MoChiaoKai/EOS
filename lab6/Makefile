all: 
	gcc server.c -o server
	gcc client.c -o client

clean:
	rm -f server *.o $(LOG_FILE)
	rm -f client *.o $(LOG_FILE)
	
.PHONY: all clean