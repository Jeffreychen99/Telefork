#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/thread_status.h>

#include "tele.h"

void 
telereturn() {
	printf("GOT HERE \n");
}

int
socket_setup(char *ip) {
	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_port = 6969;
	int success = bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
	printf("BIND: %d \n", success);

	return sock_fd;
}

struct TeleInfo * 
telepad(char *ip) {
	// Set up socket and listen
	int sock_fd = socket_setup(ip);
	int success = listen(sock_fd, 100);
	printf("LISTEN: %d \n", success);

	// Accept any parents forking (lol)
	struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
	int client_socket_number = accept(sock_fd, (struct sockaddr *)&client_address, (socklen_t *)&client_address_length);
	printf("CLIENT: %d \n\n", client_socket_number);
    telereturn();
    close(client_socket_number);

    // Read in the data from the parent

    // Create TeleInfo struct
	struct TeleInfo *teleInfo = malloc(sizeof(struct TeleInfo));

	return teleInfo;
}



int main(int argc, char **argv) {
	telepad("127.0.0.1");
	return 0;
}

