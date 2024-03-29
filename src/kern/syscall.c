/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, PTE_U);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console.
// Returns the character.
static int
sys_cgetc(void)
{
	int c;

	// The cons_getc() primitive doesn't wait for a character,
	// but the sys_cgetc() system call does.
	while ((c = cons_getc()) == 0)
		/* do nothing */;

	return c;
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
// The system call returns 0.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
static envid_t
sys_exofork(void)
{
	int err;
	struct Env *e;

	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	err = env_alloc(&e, curenv->env_id);
	if (err)
		return err;

	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = curenv->env_tf;
	e->env_tf.tf_regs.reg_eax = 0;

	return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	int err;
	struct Env *e;

  	// Hint: Use the 'envid2env' function from kern/env.c to translate an
  	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	if ((status != ENV_RUNNABLE) && (status != ENV_NOT_RUNNABLE))
		return -E_INVAL;

	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	e->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	int err;
	struct Env *e;

	// LAB 4: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!

	if (!tf)
		return -E_INVAL;

	if ((uintptr_t) tf >= UTOP)
		return -E_INVAL;

	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	e->env_tf = *tf;
	e->env_tf.tf_regs = tf->tf_regs;

	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_cs = GD_UT | 3;
	e->env_tf.tf_eflags |= FL_IF;

	return 0;
}

// Get envid's trap frame.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to get envid.
static int
sys_env_get_trapframe(envid_t envid, struct Trapframe *tf)
{
	int err;
	struct Env *e;

	if (tf && (uintptr_t) tf >= UTOP)
		return -E_INVAL;

	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	*tf = e->env_tf;
	tf->tf_regs = e->env_tf.tf_regs;

	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	int err;
	struct Env *e;

	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	e->env_pgfault_upcall = func;
	return 0;
}

// Check 'perm' as specified by sys_page_alloc()
// 
// Return 0 if 'perm' is ok, -E_INVAL otherwise
static int
check_page_perm(int perm)
{
	if (!(perm & (PTE_U|PTE_P)))
		return -E_INVAL;

	perm &= ~(PTE_U|PTE_P|PTE_AVAIL|PTE_W);
	if (perm)
		return -E_INVAL;

	return 0;
}

// Check 'va' as specified by sys_page_alloc()
// 
// Return 0 if 'va' is ok, -E_INVAL otherwise
static int
check_user_va(uintptr_t va)
{
	if ((va >= UTOP) || (va % PGSIZE))
		return -E_INVAL;

	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	int err;
	struct Env *e;
	struct Page *pp;

	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// permission checks
	err = check_page_perm(perm);
	if (err)
		return err;

	// va checks
	err = check_user_va((uintptr_t) va);
	if (err)
		return err;

	// env checks
	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	// go!
	err = page_alloc(&pp);
	if (err)
		return err;

	err = page_insert(e->env_pgdir, pp, va, perm);
	if (err) {
		page_free(pp);
		return err;
	}

	memset(page2kva(pp), 0, PGSIZE);

	return 0;
}

// Does the actual page map work
static int
page_map(struct Env *srcenv, void *srcva,
	 struct Env *dstenv, void *dstva, int perm)
{
	pte_t *pte;
	struct Page *pp;

	pp = page_lookup(srcenv->env_pgdir, srcva, &pte);
	if (!pp)
		return -E_INVAL;

	if (perm & PTE_W) {
		if (!(srcenv->env_pgdir[PDX(srcva)] & PTE_W))
			return -E_INVAL;

		if (!(*pte & PTE_W))
			return -E_INVAL;
	}

	// the real job...
	return page_insert(dstenv->env_pgdir, pp, dstva, perm);
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	int err;
	struct Env *srcenv, *dstenv;

	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// permission checks
	err = check_page_perm(perm);
	if (err)
		return err;

	// va checks
	err = check_user_va((uintptr_t) srcva);
	if (err)
		return err;

	err = check_user_va((uintptr_t) dstva);
	if (err)
		return err;

	// env checks
	err = envid2env(srcenvid, &srcenv, 1);
	if (err)
		return err;

	err = envid2env(dstenvid, &dstenv, 1);
	if (err)
		return err;

	// the real job
	return page_map(srcenv, srcva, dstenv, dstva, perm);
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	int err;
	struct Env *e;

	// Hint: This function is a wrapper around page_remove().

	// va checks
	err = check_user_va((uintptr_t) va);
	if (err)
		return err;

	// env checks
	err = envid2env(envid, &e, 1);
	if (err)
		return err;

	page_remove(e->env_pgdir, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If va != 0, then also send page currently mapped at 'va',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target has not requested IPC with sys_ipc_recv.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again.
//
// If the sender sends a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc doesn't happen unless no errors occur.
//
// Returns 0 on success where no page mapping occurs,
// 1 on success where a page mapping occurs, and < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	int err, ret;
	struct Env *recenv, *sndenv;

	// LAB 4: Your code here.
	if (srcva) {
		if ((uintptr_t) srcva >= UTOP)
			return -E_INVAL;

		if ((uintptr_t) srcva % PGSIZE)
			return -E_INVAL;

		if (check_page_perm(perm))
			return -E_INVAL;
	}

	err = envid2env(envid, &recenv, 0);
	if (err)
		return err;

	if (!recenv->env_ipc_recving)
		return -E_IPC_NOT_RECV;

	err = envid2env(0, &sndenv, 0);
	if (err)
		return err;

	ret = 0;
	recenv->env_ipc_perm = 0;
	recenv->env_ipc_recving = 0;

	if (srcva && (uintptr_t) recenv->env_ipc_dstva < UTOP) {
		err = page_map(sndenv, srcva, recenv, recenv->env_ipc_dstva,
			       perm);
		if (err)
			return err;
		recenv->env_ipc_perm = perm;
		ret = 1;
	}

	recenv->env_ipc_from = curenv->env_id;
	recenv->env_ipc_value = value;
	recenv->env_status = ENV_RUNNABLE;

	return ret;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	int err;
	struct Env *e;

	// LAB 4: Your code here.
	err = envid2env(0, &e, 0);
	if (err)
		return err;

	e->env_ipc_dstva = (void *) UTOP;

	if ((uintptr_t) dstva < UTOP) {
		if ((uintptr_t) dstva % PGSIZE)
			return -E_INVAL;
		e->env_ipc_dstva = dstva;
	}

	e->env_ipc_recving = 1;
	e->env_status = ENV_NOT_RUNNABLE;

	return 0;
}


// Dispatches to the correct kernel function, passing the arguments.
uint32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((const char *) a1, a2);
		break;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		return sys_getenvid();
	case SYS_env_destroy:
		return sys_env_destroy(a1);
	case SYS_yield:
		sys_yield();
		break;
	case SYS_exofork:
		return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status(a1, a2);
	case SYS_page_alloc:
		return sys_page_alloc(a1, (void *) a2, a3);
	case SYS_page_map:
		return sys_page_map(a1, (void *) a2, a3, (void *) a4, a5);
	case SYS_page_unmap:
		return sys_page_unmap(a1, (void *) a2);
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall(a1, (void *) a2);
	case SYS_ipc_try_send:
		return sys_ipc_try_send(a1, a2, (void *) a3, a4);
	case SYS_ipc_recv:
		return sys_ipc_recv((void *) a1);
	case SYS_env_set_trapframe:
		return sys_env_set_trapframe(a1, (struct Trapframe *) a2);
	case SYS_env_get_trapframe:
		return sys_env_get_trapframe(a1, (struct Trapframe *) a2);
	default:
		return -E_INVAL;
	}

	return 0;
}

