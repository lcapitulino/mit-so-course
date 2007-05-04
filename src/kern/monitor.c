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
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "alloc_page", "Allocate a physical page", mon_alloc_page },
	{ "backtrace", "Show backtrace", mon_backtrace },
	{ "halt", "Halt the processor", mon_halt },
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "pse", "Display PSE information", mon_pse },
	{ "showmap", "Display virtual to physical mapping", mon_showmap },
	{ "symtab", "Display symbol table", mon_symtab },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_alloc_page(int argc, char **argv, struct Trapframe *tf)
{
	int err;
	struct Page *pp;

	err = page_alloc(&pp);
	if (err) {
		cprintf("Page allocation failed\n");
		return 0;
	}

	cprintf("phys: 0x%08x virt: 0x%08x\n", page2pa(pp),
		(uintptr_t) page2kva(pp));
	return 0;
}

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

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	const char *p;
	uint32_t *ebp, eip;
	struct Eipdebuginfo info;

	cprintf("Stack backtrace:\n");

	eip = read_eip();
	ebp = (uint32_t *) read_ebp();
	for (; ebp; ebp = (uint32_t *) *ebp) {

		debuginfo_eip(eip, &info);

		cprintf("%s:%d: ", info.eip_file, info.eip_line);

		for (p = info.eip_fn_name; *p != ':'; p++)
			cputchar(*p);

		cprintf("+%x\n", eip - info.eip_fn_addr);
		cprintf(" eip %08x args 0x%08x 0x%08x 0x%08x\n", eip,
			*(ebp + 2), *(ebp + 3),	*(ebp + 4));

		eip = *(ebp + 1);
	}

	return 0;
}

static void
dump_pde_flags(pde_t *pde)
{
	cprintf(" pde: 0x%08x [", *pde);
	if (*pde & PTE_PS)
		cprintf(" 0x%08x PS", PTE_PS_ADDR(*pde));
	else
		cprintf(" 0x%08x", PTE_ADDR(*pde));
	if (*pde & PTE_A)
		cprintf(" A");
	if (*pde & PTE_PCD)
		cprintf(" PCD");
	if (*pde & PTE_PWT)
		cprintf(" PWT");
	if (*pde & PTE_U)
		cprintf(" U");
	else
		cprintf(" S");
	if (*pde & PTE_W)
		cprintf(" W");
	else
		cprintf(" R");
	if (*pde & PTE_P)
		cprintf(" P");
	cprintf(" ]\n");
}

static void
dump_pte_flags(pte_t *pte)
{
	cprintf(" pte: 0x%08x [ 0x%08x", *pte, PTE_ADDR(*pte));
	if (*pte & PTE_G)
		cprintf(" G");
	if (*pte & PTE_PAT)
		cprintf(" PTA");
	if (*pte & PTE_D)
		cprintf(" D");
	if (*pte & PTE_A)
		cprintf(" A");
	if (*pte & PTE_PCD)
		cprintf(" PCD");
	if (*pte & PTE_PWT)
		cprintf(" PWT");
	if (*pte & PTE_U)
		cprintf(" U");
	else
		cprintf(" S");
	if (*pte & PTE_W)
		cprintf(" W");
	else
		cprintf(" R");
	if (*pte & PTE_P)
		cprintf(" P");
	cprintf(" ]\n");
}

int
mon_pse(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("PSE support: ");
	if (pse_support)
		cprintf("enabled\n");
	else
		cprintf("not enabled\n");

	return 0;
}

int
mon_showmap(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 1; i < argc; i++) {
		pde_t *pde, *pte;
		uintptr_t va, ph;

		va = strtol(argv[i], NULL, 16);

		pde = boot_pgdir + PDX(va);
		if (!(*pde & PTE_P)) {
			cprintf("\n0x%08x not mapped\n", va);
			continue;
		}

		pte = 0;

		if (!(*pde & PTE_PS)) {
			pte = (pte_t *) KADDR(PTE_ADDR(*pde));
			pte += PTX(va);
			if (!(*pte & PTE_P)) {
				cprintf("\n0x%08x not mapped\n", va);
				continue;
			}
			ph = PTE_ADDR(*pte) + PGOFF(va);
		} else {
			ph = PTE_PS_ADDR(*pde) + PS_PGOFF(va);
		}

		cprintf("\n0x%08x -> 0x%08x\n", va, ph);

		// dump PDE and PTE flags
		dump_pde_flags(pde);
		if (pte)
			dump_pte_flags(pte);
	}

	cputchar('\n');
	return 0;
}

int
mon_symtab(int argc, char **argv, struct Trapframe *tf)
{
	struct Stab *stab;
	extern char __STABSTR_BEGIN__[];
	extern char __STAB_BEGIN__[], __STAB_END__[];

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
