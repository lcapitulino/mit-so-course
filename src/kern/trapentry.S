/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(trap_ex_divide, T_DIVIDE)
TRAPHANDLER_NOEC(trap_ex_debug, T_DEBUG)
TRAPHANDLER_NOEC(trap_ex_nmi, T_NMI)
TRAPHANDLER_NOEC(trap_ex_break_point, T_BRKPT)
TRAPHANDLER_NOEC(trap_ex_overflow, T_OFLOW)
TRAPHANDLER_NOEC(trap_ex_bound, T_BOUND)
TRAPHANDLER_NOEC(trap_ex_iop, T_ILLOP)
TRAPHANDLER_NOEC(trap_ex_device, T_DEVICE)
TRAPHANDLER(trap_ex_db_fault, T_DBLFLT)
TRAPHANDLER(trap_ex_tss, T_TSS)
TRAPHANDLER(trap_ex_segnp, T_SEGNP)
TRAPHANDLER(trap_ex_stack, T_STACK)
TRAPHANDLER(trap_ex_gp_fault, T_GPFLT)
TRAPHANDLER(trap_ex_pg_fault, T_PGFLT)
TRAPHANDLER_NOEC(trap_ex_fp_error, T_FPERR)
TRAPHANDLER(trap_ex_align, T_ALIGN)
TRAPHANDLER_NOEC(trap_ex_mcheck, T_MCHK)
TRAPHANDLER_NOEC(trap_ex_simderr, T_SIMDERR)
TRAPHANDLER_NOEC(trap_ex_syscall, T_SYSCALL)
TRAPHANDLER_NOEC(int_nr_0, IRQ_OFFSET)
TRAPHANDLER_NOEC(int_nr_1, IRQ_OFFSET+1)
TRAPHANDLER_NOEC(int_nr_2, IRQ_OFFSET+2)
TRAPHANDLER_NOEC(int_nr_3, IRQ_OFFSET+3)
TRAPHANDLER_NOEC(int_nr_4, IRQ_OFFSET+4)
TRAPHANDLER_NOEC(int_nr_5, IRQ_OFFSET+5)
TRAPHANDLER_NOEC(int_nr_6, IRQ_OFFSET+6)
TRAPHANDLER_NOEC(int_nr_7, IRQ_OFFSET+7)
TRAPHANDLER_NOEC(int_nr_8, IRQ_OFFSET+8)
TRAPHANDLER_NOEC(int_nr_9, IRQ_OFFSET+9)
TRAPHANDLER_NOEC(int_nr_10, IRQ_OFFSET+10)
TRAPHANDLER_NOEC(int_nr_11, IRQ_OFFSET+11)
TRAPHANDLER_NOEC(int_nr_12, IRQ_OFFSET+12)
TRAPHANDLER_NOEC(int_nr_13, IRQ_OFFSET+13)
TRAPHANDLER_NOEC(int_nr_14, IRQ_OFFSET+14)
TRAPHANDLER_NOEC(int_nr_15, IRQ_OFFSET+15)


/*
 * Lab 3: Your code here for _alltraps
 */

.globl _alltraps
_alltraps:
	pushl %ds
	pushl %es
	pushal
	movl $GD_KD, %eax
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	movl $0x0, %ebp     # nuke frame pointer
	call trap
	addl $0x4, %esp
	popal
	popl %es
	popl %ds
	addl $0x8, %esp /* skip tf_trapno and tf_errcode */
	iret
