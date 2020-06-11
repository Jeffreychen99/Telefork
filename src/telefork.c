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
	mach_msg_type_number_t thread_count;
};

struct thread_list_node {
	thread_state_t thread_state;
	struct thread_list_node *next;
};

struct thread_register_list *
readRegisters() {
	task_t port;
	task_for_pid(mach_task_self(), getpid(), &port);

	thread_act_port_array_t thread_list;

	struct thread_register_list *threadRegList = malloc(sizeof(struct thread_register_list));
	task_threads(port, &thread_list, &threadRegList->thread_count);

	for (int i = 0; i < threadRegList->thread_count; i++) {
		struct thread_list_node *thread_node = malloc(sizeof(struct thread_list_node));

		thread_get_state(thread_list[i], i386_THREAD_STATE, thread_node->thread_state, 0);
	}
	return threadRegList;
}

void 
getMemoryMappings() {
	vm_map();
}

void *
readMemoryMappings() {
    vm_offset_t readMem;

    /*
    kern_return_t kr = vm_read(t,        // vm_map_t target_task,
                 addr,     // mach_vm_address_t address,
                 *size,     // mach_vm_size_t size
                 &readMem,     //vm_offset_t *data,
                 size);     // mach_msg_type_number_t *dataCnt
    */
}

struct TeleInfo {
	int child_pid;
	int fd;
};

struct TeleInfo *
telereceive(int parent_fd) {
	struct TeleInfo *teleInfo = malloc(sizeof(struct TeleInfo)); 
	return teleInfo;
}

struct TeleInfo *
telesend(int child_fd) {

	void *process_state = sbrk(0);

	struct TeleInfo *parentInfo = malloc(sizeof(struct TeleInfo)); 

	struct TeleInfo *teleInfo;

	return teleInfo;
}

struct TeleInfo *telefork() {
	return NULL;
}

int main(int argc, char **argv) {
	return 0;
}


