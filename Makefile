
all: server
	
user_space_ioctl: server.o
	gcc -o server server.o

clean:
	rm -f *.o server
