#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/kdebug.h>

static struct Taskstate ts;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Falt",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}


void
idt_init(void)
{
	extern struct Segdesc gdt[];
	
	// LAB 3: Your code here.
	SETGATE(idt[T_DIVIDE], 1, GD_KT, trap_ex_divide, 0)
	SETGATE(idt[T_DEBUG], 1, GD_KT, trap_ex_debug, 0)
	SETGATE(idt[T_NMI], 1, GD_KT, trap_ex_nmi, 0)
	SETGATE(idt[T_BRKPT], 1, GD_KT, trap_ex_break_point, 3)
	SETGATE(idt[T_OFLOW], 1, GD_KT, trap_ex_overflow, 0)
	SETGATE(idt[T_BOUND], 1, GD_KT, trap_ex_bound, 0)
	SETGATE(idt[T_ILLOP], 1, GD_KT, trap_ex_iop, 0)
	SETGATE(idt[T_DEVICE], 1, GD_KT, trap_ex_device, 0)
	SETGATE(idt[T_DBLFLT], 1, GD_KT, trap_ex_db_fault, 0)
	SETGATE(idt[T_TSS], 1, GD_KT, trap_ex_tss, 0)
	SETGATE(idt[T_SEGNP], 1, GD_KT, trap_ex_segnp, 0)
	SETGATE(idt[T_STACK], 1, GD_KT, trap_ex_stack, 0)
	SETGATE(idt[T_GPFLT], 1, GD_KT, trap_ex_gp_fault, 0)
	SETGATE(idt[T_PGFLT], 1, GD_KT, trap_ex_pg_fault, 0)
	SETGATE(idt[T_FPERR], 1, GD_KT, trap_ex_fp_error, 0)
	SETGATE(idt[T_ALIGN], 1, GD_KT, trap_ex_align, 0)
	SETGATE(idt[T_MCHK], 1, GD_KT, trap_ex_mcheck, 0)
	SETGATE(idt[T_SIMDERR], 1, GD_KT, trap_ex_simderr, 0)
	SETGATE(idt[T_SYSCALL], 1, GD_KT, trap_ex_syscall, 3)

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = KSTACKTOP;
	ts.ts_ss0 = GD_KD;

	// Initialize the TSS field of the gdt.
	gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
					sizeof(struct Taskstate), 0);
	gdt[GD_TSS >> 3].sd_s = 0;

	// Load the TSS
	ltr(GD_TSS);

	// Load the IDT
	asm volatile("lidt idt_pd");
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	cprintf("  err  0x%08x\n", tf->tf_err);
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.
	// LAB 3: Your code here.
 	switch (tf->tf_trapno) {
 	case T_PGFLT:
 		page_fault_handler(tf);
 		return;
 	case T_SYSCALL:
 		tf->tf_regs.reg_eax = syscall(tf->tf_regs.reg_eax,
 					  tf->tf_regs.reg_edx,
 					  tf->tf_regs.reg_ecx,
 					  tf->tf_regs.reg_ebx,
 					  tf->tf_regs.reg_edi,
 					  tf->tf_regs.reg_esi);
 		return;
 	case T_DEBUG:
 		monitor_ss(tf);
 		return;
 	case T_BRKPT:
 		monitor(tf);
 		return;
 	}
	
	// Handle clock and serial interrupts.
	// LAB 4: Your code here.

	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}

static int
single_step_enabled(void)
{
	int ret = 0;
	uint32_t dr6;
	const uint32_t sstep = 0x4000;

	dr6 = rdr6();
	if (dr6 & sstep) {
		dr6 &= ~sstep;
		ldr6(dr6);
		ret = 1;
	}

	return ret;
}

void
trap(struct Trapframe *tf)
{
	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		assert(curenv);
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}
	
	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

 	if (single_step_enabled())
 		return;

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNABLE)
		env_run(curenv);
	else
		sched_yield();
}

static void
show_backtrace(uint32_t eip, uint32_t *ebp)
{
	struct Eipdebuginfo info;

	cprintf("Call trace:\n");

	for (; ebp; ebp = (uint32_t *) *ebp) {

		debuginfo_eip(eip, &info);

		cprintf("  [<%08x>] ", info.eip_fn_addr);

		show_eip_func_name(&info);

		cprintf("+0x%x\n", eip - info.eip_fn_addr);

		eip = *(ebp + 1);
	}
}

static void
show_registers(const struct Trapframe *tf)
{
	cprintf("eax: %08x ebx: %08x ecx: %08x edx: %08x\n",
		tf->tf_regs.reg_eax, tf->tf_regs.reg_ebx,
		tf->tf_regs.reg_ecx, tf->tf_regs.reg_edx);
	cprintf("esi: %08x edi: %08x ebp: %08x esp: %08x\n",
		tf->tf_regs.reg_esi, tf->tf_regs.reg_edi,
		tf->tf_regs.reg_ebp, tf->tf_regs.reg_oesp);
	cprintf("ds: %08x es: %08x ss: %08x\n",
		tf->tf_ds, tf->tf_es, tf->tf_ds);
}

// Print a Linux-like debug info on kernel bug
// XXX: you're welcome to make this better
static void
kernel_oops(const struct Trapframe *tf, uint32_t fault_va)
{
	struct Eipdebuginfo info;

	// Greetings
	cprintf("\nBUG: Unable to handle kernel paging request "
		"at virtual address 0x%08x", fault_va);
	if (!fault_va)
		cprintf(" (NULL pointer)");
	cputchar('\n');

	// Basic debug info
	cprintf("EIP: %08x:[<%08x>]\n", tf->tf_cs, tf->tf_eip);
	cprintf("EFLAGS: %08x\n", tf->tf_eflags);
	debuginfo_eip(tf->tf_eip, &info);
	cprintf("EIP is at: ");
	show_eip_func_name(&info);
	cputchar('\n');
	show_registers(tf);

	if (curenv)
		cprintf("Environment id: %d parent id: %d\n", curenv->env_id,
			curenv->env_parent_id);

	show_backtrace(tf->tf_eip, (uint32_t *) tf->tf_regs.reg_ebp);
	cprintf("\n\nCalling the monitor...\n");
	monitor(NULL);
}

void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	
	// LAB 3: Your code here.
	if (tf->tf_cs == GD_KT)
		kernel_oops(tf, fault_va);

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack, or the exception stack overflows,
	// then destroy the environment that caused the fault.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').
	
	if (curenv->env_pgfault_upcall) {
		uintptr_t addr;
		struct UTrapframe utf;

		// fill utf
		memset(&utf, 0, sizeof(utf));
		utf.utf_fault_va = fault_va;
		utf.utf_err = tf->tf_err;
		utf.utf_regs = tf->tf_regs;
		utf.utf_eip = tf->tf_eip;
		utf.utf_eflags = tf->tf_eflags;
		utf.utf_esp = tf->tf_esp;

		// too slow?
		lcr3(curenv->env_cr3);

		if ((tf->tf_esp >= UXSTACKTOP - PGSIZE) &&
		    (tf->tf_esp < UXSTACKTOP)) {
			// pgfault handler faulted
			addr = tf->tf_esp - 4 - sizeof(utf);
		} else {
			// expected case
			addr = UXSTACKTOP - sizeof(utf);
		}

		user_mem_assert(curenv, (const void *) addr,
				sizeof(utf), PTE_W);

		memcpy((void *) addr, &utf, sizeof(utf));

		lcr3(boot_cr3);

		// go!
		curenv->env_tf.tf_eip =(uintptr_t) curenv->env_pgfault_upcall;
		curenv->env_tf.tf_esp = addr;
		env_run(curenv);
	}

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}
