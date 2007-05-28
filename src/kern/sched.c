#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

static inline void
run_if_runnable(struct Env *e)
{
	if (e->env_status == ENV_RUNNABLE)
		env_run(e);
}

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	int i;

	// Implement simple round-robin scheduling.
	// Search through 'envs' for a runnable environment,
	// in circular fashion starting after the previously running env,
	// and switch to the first such environment found.
	// It's OK to choose the previously running env if no other env
	// is runnable.
	// But never choose envs[0], the idle environment,
	// unless NOTHING else is runnable.

	// LAB 4: Your code here.
	if (!curenv) {
		for (i = 1; i < NENV; i++)
			run_if_runnable(&envs[i]);
	} else {
		for (i = ENVX(curenv->env_id) + 1; i < NENV; i++)
			run_if_runnable(&envs[i]);

		for (i = 1; i < ENVX(curenv->env_id); i++)
			run_if_runnable(&envs[i]);

		run_if_runnable(curenv);
	}

	// Run the special idle environment when nothing else is runnable.
	if (envs[0].env_status == ENV_RUNNABLE)
		env_run(&envs[0]);
	else {
		cprintf("Destroyed all environments - nothing more to do!\n");
		while (1)
			monitor(NULL);
	}
}
