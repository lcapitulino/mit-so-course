// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/stab.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "backtrace", "Show backtrace", mon_backtrace },
	{ "halt", "Halt the processor", mon_halt },
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "symtab", "Display symbol table", mon_symtab },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

int
mon_halt(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Halting the processor\n");
	asm("hlt");
	return 0;
}

static char *
get_func_name(uintptr_t addr, uintptr_t *ret)
{
	struct Stab *stab, *last;
	extern char __STABSTR_BEGIN__[];
	extern char __STAB_BEGIN__[], __STAB_END__[];

	last = stab = (struct Stab *) &__STAB_BEGIN__;
	for (; stab < (struct Stab *) &__STAB_END__; stab++) {
		if (stab->n_type != N_FUN)
			continue;

		if (addr < stab->n_value) {
			*ret = last->n_value;
			return (last->n_strx + (char *) &__STABSTR_BEGIN__);
		}

		last = stab;
	}

	return NULL;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	int ptrace = 1;
	char *p, *fname;
	uintptr_t faddr = 0;
	uint32_t *ebp, prev_eip, eip;

	cprintf("Kernel backtrace dump:\n");
	prev_eip = read_eip();
	cprintf("EIP is at [0x%08x] ", prev_eip);

	ebp = (uint32_t *) read_ebp();
	for (; ebp; ebp = (uint32_t *) *ebp) {
		eip = *(ebp + 1);
		fname = get_func_name(prev_eip, &faddr);
		if (!fname)
			fname = "unknown";

		if (!ptrace)
			cprintf(" [0x%08x] ", faddr);

		for (p = fname; *p != ':'; p++)
			cputchar(*p);

		cprintf(" args 0x%08x 0x%08x 0x%08x\n", *(ebp + 2), *(ebp + 3),
			*(ebp + 4));

		if (ptrace) {
			cprintf("Call trace:\n");
			ptrace = 0;
		}

		prev_eip = eip;
	}

	return 0;
}

int
mon_symtab(int argc, char **argv, struct Trapframe *tf)
{
	struct Stab *stab;
	extern char __STABSTR_BEGIN__[];
	extern char __STAB_BEGIN__[], __STAB_END__[];

	/* 
	 * XXX: is it possible to share code with
	 * get_func_name() ?
	 */

	stab = (struct Stab *) &__STAB_BEGIN__;
	for (; stab < (struct Stab *) &__STAB_END__; stab++)
		if (stab->n_type == N_FUN) {
			char *p;

			cprintf("%#x ", stab->n_value);


			/* Stab format for functions is:
			 * 
			 *     func_name:F(x,y)
			 * 
			 * But we're only interested in func_name
			 */
			for (p = stab->n_strx + (char *) &__STABSTR_BEGIN__;
			     *p != ':'; p++)
				cputchar(*p);
			cputchar('\n');
		}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
