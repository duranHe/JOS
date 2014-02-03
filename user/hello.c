// hello, world
#include <inc/lib.h>
int share = 1;
void
umain(int argc, char **argv)
{
	//cprintf("hello, world\n");
	//cprintf("i am environment %08x\n", thisenv->env_id);

	/* priority schedule
	struct Env * cur;
	int childid, curid;

	if((childid = fork()) != 0)
	{
		sys_env_set_priority(childid, 3);
		if((childid = fork()) != 0)
		{
			sys_env_set_priority(childid, 2);
			if((childid = fork()) != 0)
				sys_env_set_priority(childid, 1);
		}
	}

	curid = sys_getenvid();
	cur = (struct Env *)envs + ENVX(curid);
	
	int i;
	for(i = 0; i < 2; i++)
	{	
		cprintf("%04x: my priority: %d\n", curid, cur->env_priority);
		sys_yield();
	}*/

	/*checkpoint
	cprintf("Checkpoint: %04x: my priority is: %d\n", thisenv->env_priority);
	sys_env_checkpoint(thisenv->env_id, 0);

	int i;
	for(i = 1; i <= 3; i++)
	{
		sys_env_set_priority(thisenv->env_id, i);
		cprintf("my priority is: %d\n", thisenv->env_priority);
	}
	
	sys_env_checkpoint(thisenv->env_id, 1);
	cprintf("Restore checkpoint: %04x: my priority is: %d\n", thisenv->env_priority);*/

	/*int child = sfork ();
	if (child != 0) 	
	{
		cprintf ("I am parent with share num = %d\n", share);
		share = 2;
	} 
	else 
	{
		cprintf ("I am child with share num = %d\n", share);
	}*/

	envid_t child;
	if((child = fork()) != 0)
	{
		cprintf("i am father\n");
		ipc_send(child, 0, 0, 0);
	}
	else
	{
		cprintf("i am child\n");
		sys_yield();
	}

}
