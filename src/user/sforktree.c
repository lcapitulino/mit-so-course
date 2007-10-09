// Fork a binary tree of processes and display their structure,
// uses sfork().
// 
// XXX: I'm not quite sure if the 'env' changes should be here or if
// it should be set other way (eg, in the lib layer),

#include <inc/lib.h>

#define DEPTH 10

void forktree(const char *cur);

void
forkchild(const char *cur, char branch)
{
	char nxt[DEPTH+1];

	if (strlen(cur) >= DEPTH)
		return;

	env = &envs[ENVX(sys_getenvid())];
	snprintf(nxt, DEPTH+1, "%s%c", cur, branch);
	if (sfork() == 0) {
		forktree(nxt);
		exit();
	}
}

void
forktree(const char *cur)
{
	env = &envs[ENVX(sys_getenvid())];
	cprintf("%04x: I am '%s'\n", sys_getenvid(), cur);

	forkchild(cur, '0');
	forkchild(cur, '1');
}

void
umain(void)
{
	forktree("");
}

