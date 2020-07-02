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

#define BUFFER_SIZE 1024

void 
telereturn() {
}

int
socket_setup(char *ip) {
	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_port = 6969;
	if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		printf("Successfully bound socket %d to ip %s \n", sock_fd, ip);
	} else {
		printf("Error: could not bind to socket and address \n");
		exit(1);
	}

	return sock_fd;
}

struct TeleInfo * 
telepad(char *ip) {
	// Set up socket and listen
	int sock_fd = socket_setup(ip);
	if (listen(sock_fd, 100) == 0) {
		printf("Listening...\n");
	} else {
		printf("Error: could not listen");
		exit(1);
	}

	// Accept the forking parent (lol)
	struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
	int client_socket_number = accept(sock_fd, (struct sockaddr *)&client_address, (socklen_t *)&client_address_length);

    // Read in the data from the parent
    struct thread_register_list *p_registers = malloc(sizeof(struct thread_register_list));

    int bytes_read = read(client_socket_number, &p_registers->thread_count, sizeof(unsigned int));
    if (bytes_read != sizeof(unsigned int)) {
    	printf("  - ERROR: Did not receive enough bytes for thread count \n");
    	printf("	  expected: %lu \n", sizeof(unsigned int));
    	printf("	  got:      %d \n", bytes_read);
    } else {
    	printf("  + %u threads to set up \n", p_registers->thread_count);
    }

 	p_registers->start_node = malloc(sizeof(struct thread_list_node));
	struct thread_list_node *curr_node = p_registers->start_node;
	for (int i = 0; i < p_registers->thread_count; i++) {
	    bytes_read = read(client_socket_number, &curr_node->state, sizeof(x86_thread_state64_t));
	    if (bytes_read != sizeof(x86_thread_state64_t)) {
	    	printf("  - ERROR: Did not receive enough bytes for x86_thread_state64_t (%d) \n", i);
	    	printf("	  expected: %lu \n", sizeof(x86_thread_state64_t));
	    	printf("	  got:      %d \n", bytes_read);
	    } else {
	    	printf("  + thread (%d) received \n", i);
	    }

		if (i < p_registers->thread_count - 1) {
		    curr_node->next = malloc(sizeof(struct thread_list_node));
			curr_node = curr_node->next;
		}
	}

    // Create TeleInfo struct
	struct TeleInfo *teleInfo = malloc(sizeof(struct TeleInfo));

    telereturn();
	return teleInfo;
}



int main(int argc, char **argv) {
	telepad("127.0.0.1");
	return 0;
}

