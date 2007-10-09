// User-level IPC library routines

#include <inc/lib.h>
// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'fromenv' is nonnull, then store the IPC sender's envid in *fromenv.
// If 'perm' is nonnull, then store the IPC sender's page permission in *perm
//	(this is nonzero iff a page was successfully transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
//
// Hint:
//   Use 'env' to discover the value and who sent it.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
uint32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	int err;

	// LAB 4: Your code here.
	if (!pg)
		pg = (void *) UTOP;
	if (from_env_store)
		*from_env_store = 0;
	if (perm_store)
		*perm_store = 0;

	err = sys_ipc_recv(pg);
	if (err)
		return err;

	// We don't use the 'env' pointer here because it's
	// useless if the caller has forked with sfork()

	if (from_env_store)
		*from_env_store = envs[ENVX(sys_getenvid())].env_ipc_from;
	if (perm_store)
		*perm_store = envs[ENVX(sys_getenvid())].env_ipc_perm;

	return envs[ENVX(sys_getenvid())].env_ipc_value;
}

// Send 'val' (and 'pg' with 'perm', assuming 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	int err;

	// LAB 4: Your code here.
	for (; ; ) {
		err = sys_ipc_try_send(to_env, val, pg, perm);
		if (!err || err == 1)
			return;
		if (err != -E_IPC_NOT_RECV)
			panic("sys_ipc_try_send(): %e\n", err);
		sys_yield();
	}
}

