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
#include <unistd.h>

struct TeleInfo {
	int child_pid;
	int fd;
};

pid_t forkFrozen() {
	pid_t pid = fork();
	if (pid == 0) {
		raise(SIGSTOP);
	}
	struct user_regs_struct *regs = ptrace(PT_GETREGS, pid, 0, 0);
	return pid;
}



struct TeleInfo *telefork(int child_fd) {

	void *process_state = sbrk(0);

	pid_t child_pid = forkFrozen();

	struct TeleInfo *teleInfo = malloc(sizeof(struct TeleInfo)); 
	return teleInfo;
}

int main(int argc, char **argv) {
	printf("\nhello\n");
	//forkFrozen();
	return 0;
}



