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

struct thread_register_list *
read_registers(int pid) {
	task_t port;
	task_for_pid(mach_task_self(), pid, &port);

	thread_act_array_t thread_list;

	struct thread_register_list *threadRegList = malloc(sizeof(struct thread_register_list));
	kern_return_t kr = task_threads(port, &thread_list, &threadRegList->thread_count);
	if (kr != 0) {
		printf("Unable to get threads: kr = %x \n", kr);
	}

	for (int i = 0; i < threadRegList->thread_count; i++) {
		struct thread_list_node *thread_node = malloc(sizeof(struct thread_list_node));

		mach_msg_type_number_t thread_state_count = x86_THREAD_STATE64_COUNT;
		kr = thread_get_state(thread_list[i], x86_THREAD_STATE64, (thread_state_t)&thread_node->state, &thread_state_count);
		if (kr != 0) {
			printf("Unable to get thread %d: kr = %x \n", i, kr);
		}
	}
	return threadRegList;
}

void *
read_memory() {
	return NULL;
}




int
socket_setup(char *child_ip) {
	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in child_addr;
	child_addr.sin_family = AF_INET;
	child_addr.sin_addr.s_addr = inet_addr(child_ip);
	child_addr.sin_port = 6969;
	int success = connect(sock_fd, (struct sockaddr *)&child_addr, sizeof(child_addr));
	printf("CONNECT: %d \n", success);

	return sock_fd;
}

struct TeleInfo *
telefork(char *child_ip) {

	struct thread_register_list *p_registers;

	// Fork to a frozen child to observe
	int pid = fork();
	if (pid == 0) {
		//wait();
		exit(0);
	} else {
		// Read child process's thread registers (these should actually be the entire TCB)
		pid = getpid();
		p_registers = read_registers(pid);

		// Set up virtual memory iterator (vm_map)
	}

	// Create and bind socket to child machine's IP address
	int sock_fd = socket_setup(child_ip);

	// Send the registers over to the child via the socket
	if (write(sock_fd, &p_registers->thread_count, sizeof(p_registers->thread_count)) == -1) {
		printf("Error in sending threads \n");
	}
	printf("THREAD_COUNT: %d \n", p_registers->thread_count);
	for (int i = 0; i < p_registers->thread_count; i++) {
		printf("i: %d \n", i);
	}

	// Send the virtual memory mappings to the child via the socket

	// Accept the teleInfo struct from the child via the socket


	struct TeleInfo *parentInfo = malloc(sizeof(struct TeleInfo)); 
	return parentInfo;
}

int
main() {
	telefork("127.0.0.1");
	return 0;
}

