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

struct TeleInfo *
telereceive(int parent_fd) {
	struct TeleInfo *teleInfo = malloc(sizeof(struct TeleInfo)); 
	return teleInfo;
}

void 
telepad(int *socket_number, int server_port) {

    struct sockaddr_in server_address, client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket_number;

    *socket_number = socket(PF_INET, SOCK_STREAM, 0);
    if (*socket_number == -1) {
        perror("Failed to create a new socket");
    	exit(errno);
    }

    int socket_option = 1;
    if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1) {
    	perror("Failed to set socket options");
    	exit(errno);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(server_port);

    if (bind(*socket_number, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
    	perror("Failed to bind on socket");
    	exit(errno);
    }

    if (listen(*socket_number, 1024) == -1) {
    	perror("Failed to listen on socket");
    	exit(errno);
    }

    printf("Listening on port %d...\n", server_port);

    while (1) {
	    client_socket_number = accept(*socket_number, (struct sockaddr *) &client_address, (socklen_t *) &client_address_length);
		if (client_socket_number < 0) {
	  		perror("Error accepting socket");
	  		continue;
		}

		printf("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);
	    
	    telereceive(0);
	    close(client_socket_number);
	}
}

int main(int argc, char **argv) {
	return 0;
}

