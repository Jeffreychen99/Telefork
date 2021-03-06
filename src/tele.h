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

struct thread_register_list {
	struct thread_list_node *start_node;
	unsigned int thread_count;
};

struct thread_list_node {
	x86_thread_state64_t state;
	struct thread_list_node *next;
};



struct TeleInfo {
	int pid;
	struct sockaddr addr;
};

