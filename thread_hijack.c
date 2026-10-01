#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <objc/runtime.h>


// From Brandon Azad's threadexec library
static thread_t
pick_hijack_thread(task_t task) {
	thread_t hijack = MACH_PORT_NULL;

	// Get all the threads in the task.
	thread_act_array_t threads;
	mach_msg_type_number_t thread_count;
	kern_return_t kr = task_threads(task, &threads, &thread_count);
	if (kr != KERN_SUCCESS) {
        fprintf(stderr, "task_threads failed: %s\n", mach_error_string(kr));
		goto fail_0;
	}
	if (thread_count == 0) {
        fprintf(stderr, "no threads in task 0x%x\n", task);
		goto fail_1;
	}
	// Find a candidate thread.
	thread_t thread = MACH_PORT_NULL;
	for (long i = thread_count - 1; thread == MACH_PORT_NULL && i >= 0; i--) {
        thread_basic_info_data_t basic_info;
		mach_msg_type_number_t bi_count = THREAD_BASIC_INFO_COUNT;
		kr = thread_info(threads[i], THREAD_BASIC_INFO, (thread_info_t)&basic_info, &bi_count);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "error getting thread info: %s\n", mach_error_string(kr));
            break;
        }

		if (basic_info.suspend_count == 0) {
			thread = threads[i];
			break;
		}
	}
	if (thread == MACH_PORT_NULL) {
        fprintf(stderr, "no available candidate threds to hijack\n");
		goto fail_1;
	}
	// Success!
	hijack = thread;
	// Deallocate the thread ports and array.
fail_1:
	for (size_t i = 0; i < thread_count; i++) {
		if (threads[i] != hijack) {
			mach_port_deallocate(mach_task_self(), threads[i]);
		}
	}
	mach_vm_deallocate(mach_task_self(), (mach_vm_address_t) threads,
			thread_count * sizeof(*threads));
fail_0:
	return hijack;
}

static uint64_t
find_jmp_rbx() {
	static uint64_t jmp_rbx = 1;
	if (jmp_rbx == 1) {
		uint8_t jmp_rbx_ins[2] = { 0xff, 0xe3 };
		void *start = (void *)&malloc;
		if ((void *) &abort < start) {
			start = (void *)&abort;
		}
		size_t size = 0x4000 * 128;
		void *found = memmem(start, size, &jmp_rbx_ins, sizeof(jmp_rbx_ins));
		jmp_rbx = (uint64_t) found;
	}
	return jmp_rbx;
}

static mach_vm_address_t
write_remote_string(task_t task, const char *s)
{
    mach_vm_address_t addr = (mach_vm_address_t)NULL;
    kern_return_t kr;

    kr = mach_vm_allocate(task, &addr, strlen(s), VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "unable to allocate memory for dylib string: %s\n", mach_error_string(kr));
        return (mach_vm_address_t)NULL;
    }

    kr = mach_vm_write(task, addr, (vm_offset_t)s, strlen(s));
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "unable to write dylib string: %s\n", mach_error_string(kr));
        return (mach_vm_address_t)NULL;
    }

    return addr;
}

void
print_thread_state(const char *message, x86_thread_state64_t state)
{
#ifdef DEBUG
    printf("%s:\n", message);
    printf("       rax = 0x%016llx\n", state.__rax);
	printf("       rbx = 0x%016llx\n", state.__rbx);
	printf("       rcx = 0x%016llx\n", state.__rcx);
	printf("       rdx = 0x%016llx\n", state.__rdx);
	printf("       rdi = 0x%016llx\n", state.__rdi);
	printf("       rsi = 0x%016llx\n", state.__rsi);
	printf("       rbp = 0x%016llx\n", state.__rbp);
	printf("       rsp = 0x%016llx\n", state.__rsp);
	printf("        r8 = 0x%016llx\n", state.__r8);
	printf("        r9 = 0x%016llx\n", state.__r9);
	printf("       r10 = 0x%016llx\n", state.__r10);
	printf("       r11 = 0x%016llx\n", state.__r11);
	printf("       r12 = 0x%016llx\n", state.__r12);
	printf("       r13 = 0x%016llx\n", state.__r13);
	printf("       r14 = 0x%016llx\n", state.__r14);
	printf("       r15 = 0x%016llx\n", state.__r15);
	printf("       rip = 0x%016llx\n", state.__rip);
	printf("    rflags = 0x%016llx\n", state.__rflags);
	printf("        cs = 0x%016llx\n", state.__cs);
	printf("        fs = 0x%016llx\n", state.__fs);
	printf("        gs = 0x%016llx\n", state.__gs);
    printf("\n");
#endif
}

// thread is assumed to be in a suspended state
void *
remote_dlopen(task_t task, thread_t thread, mach_vm_address_t path)
{
    void *result = NULL;
    kern_return_t kr;

    uint64_t jmp_rbx = find_jmp_rbx();
    if (jmp_rbx == 0) {
		fprintf(stderr, "could not locate 'jmp rbx' gadget\n");
		return result;
	}
    printf("found jmp rbx at 0x%llx\n", jmp_rbx);

    // save current thread state
    x86_thread_state64_t saved_state;
    x86_thread_state64_t state;
    mach_msg_type_number_t thread_state_count = x86_THREAD_STATE64_COUNT;
	kr = thread_get_state(thread, x86_THREAD_STATE64, (thread_state_t)&state, &thread_state_count);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error getting thread state: %s\n", mach_error_string(kr));
    }
    saved_state = state;
    print_thread_state("original registers", state);

    // modify stack
    uint64_t remote_stack = state.__rsp;
    remote_stack -= sizeof(uint64_t);
    state.__rsp = remote_stack;
    printf("remote stack = 0x%llx\n", state.__rsp);

    kr = mach_vm_write(task, remote_stack, (vm_offset_t)&jmp_rbx, sizeof(uint64_t));
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error writing new return address on stack\n");
    }

    // set arguments and rip
    // Calling dlopen this way seems to crash things
    state.__rbx = jmp_rbx;
    state.__rip = (uint64_t)dlopen;
    state.__rdi = path;
    state.__rsi = RTLD_LAZY;

    // Sample read primitive eax will have the value read from rdi register
    // state.__rip = (uint64_t) property_getName;
    // state.__rdi = path;

    kr = thread_set_state(thread, x86_THREAD_STATE64, (thread_state_t)&state, x86_THREAD_STATE64_COUNT);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error setting thread state: %s\n", mach_error_string(kr));
    }

    kr = thread_resume(thread);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error resuming hijacked thread: %s\n", mach_error_string(kr));
    }

    // monitor for finish
    for (;;) {
        kr = thread_get_state(thread, x86_THREAD_STATE64, (thread_state_t)&state, &thread_state_count);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "error getting thread state for monitoring: %s\n", mach_error_string(kr));
            break;
        }

		if (state.__rip == jmp_rbx && state.__rbx == jmp_rbx) {
            printf("hijacked thread is finished!\n");
			break;    
		}
	}

    // suspend thread
    kr = thread_suspend(thread);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error suspending hijacked thread: %s\n", mach_error_string(kr));
    }

    // Capture the result
    print_thread_state("result registers", state);
    result = (void *)state.__rax;

    // restore thread state
    kr = thread_set_state(thread, x86_THREAD_STATE64, (thread_state_t)&saved_state, x86_THREAD_STATE64_COUNT);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error restoring thread back to original values: %s\n", mach_error_string(kr));
    }

    return result;
}

static int
hijack(pid_t pid, const char *lib)
{
    kern_return_t kr;
    task_t remote_task;
    thread_t remote_thread;

    kr = task_for_pid(mach_task_self(), pid, &remote_task);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "task_for_pid(%d) failed: %s\n", pid, mach_error_string(kr));
        return 1;
    }

    remote_thread = pick_hijack_thread(remote_task);
    if (remote_thread == MACH_PORT_NULL) {
        fprintf(stderr, "failed to find thread to hijack\n");
        return 1;
    }
    printf("hijacking thread 0x%x\n", remote_thread);

    kr = thread_suspend(remote_thread);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error suspending thread 0x%x\n", remote_thread);
        return 1;
    }

    mach_vm_address_t remote_lib = write_remote_string(remote_task, lib);
    if (remote_lib == (mach_vm_address_t)NULL) {
        fprintf(stderr, "could not write dylib path into remote task\n");
        return 1;
    }
    printf("wrote %s to 0x%llx\n", lib, remote_lib);

    void *handle = remote_dlopen(remote_task, remote_thread, remote_lib);
    if (!handle) {
        fprintf(stderr, "remote dlopen failed\n");
        return 1;
    }

    kr = mach_vm_deallocate(remote_task, remote_lib, strlen(lib));
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error removing remote string\n");
        return 1;
    }

    kr = thread_resume(remote_thread);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "error resuming thread 0x%x\n", remote_thread);
        return 1;
    }

    mach_port_deallocate(mach_task_self(), remote_task);
    return 0;
}

int
main(int argc, const char *argv[])
{
    pid_t pid;
    const char *lib;
    struct stat buf;
    int rc;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s _pid_ _action_\n", argv[0]);
        fprintf(stderr, "   _action_: path to a dylib on disk\n");
        return 1;
    }

    pid = atoi(argv[1]);
    lib = argv[2];

    rc = stat(lib, &buf);
    if (rc != 0) {
        fprintf(stderr, "Dylib not found\n");
        return 1;
    }

    return hijack(pid, lib);
}