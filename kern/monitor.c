// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>

#include <kern/monitor.h>

#include <kern/kdebug.h>
#include <kern/trap.h>

#include <kern/env.h>

#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace","Display information about the stack",mon_backtrace },
	{ "showmapping","Display virtual and physical page mapping",mon_showmapping },
	{ "setperm","Set virtual page permission",mon_setperm },
	{ "dump","Set virtual page permission",mon_dump },
	{"si","Single-Step debug",mon_si },
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
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	int i;
	uint32_t *ebp, *eip;
	uint32_t arg[5];
	struct Eipdebuginfo info;
	
	ebp = (uint32_t *)read_ebp();
	eip = (uint32_t *)ebp[1];
	for(i = 0; i <= 5; i++)
		arg[i] = ebp[2 + i];

	cprintf("Stack backtrace\n");
	while(ebp != 0)
	{
		cprintf("  ebp %08x eip %08x args %08x %08x %08x %08x %08x\n",
			ebp, eip, arg[0], arg[1], arg[2], arg[3], arg[4]);

		debuginfo_eip((uintptr_t)eip, &info);
		cprintf("    %s:%d: %.*s+%d\n", 
			info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, (int)eip - (int)info.eip_fn_addr);

		ebp = (uint32_t *)ebp[0];
		
		//bugs?
		if(ebp == 0)
			break;
		eip = (uint32_t *)ebp[1];
		for(i = 0; i <= 5; i++)
			arg[i] = ebp[2 + i];
	}
	return 0;
}

int 
mon_showmapping(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3)
	{
		cprintf("Command format: showmapping va_start va_end\n");
		return 0;
	}

	uintptr_t va_start = strtol(argv[1], 0, 0);
	uintptr_t va_end = strtol(argv[2], 0, 0);

	if(va_start != ROUNDUP(va_start, PGSIZE) ||
	   va_end != ROUNDUP(va_end, PGSIZE) ||
	   va_start > va_end)
	{
		cprintf("Invalid Address!\n");
		return 0;
	}

	uintptr_t va;
	pte_t * pt_entry;
	for(va = va_start; va < va_end; va += PGSIZE)
	{
		cprintf("0x%08x - 0x%08x    ", va, va + PGSIZE);
		pt_entry = pgdir_walk(kern_pgdir, (void *)va, 0);

		if((pt_entry == NULL) || !(*pt_entry & PTE_P))
			cprintf("Not Mapped\n");
		else
		{
			cprintf("Mapped to 0x%08x    ", PTE_ADDR(* pt_entry));
			if(*pt_entry & PTE_U)
				cprintf("User: ");
			else
				cprintf("Kernel: ");
			if(*pt_entry & PTE_W)
				cprintf("Read/Write\n");
			else
				cprintf("Read Only\n");
		}
	}
	return 0;
}


int 
mon_setperm(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 4)
	{
		cprintf("Command format: setperm va_start va_end permission\n");
		return 0;
	}

	uintptr_t va_start = strtol(argv[1], 0, 0);
	uintptr_t va_end = strtol(argv[2], 0, 0);
	char * perm = argv[3];
	
	if((va_start != ROUNDUP(va_start, PGSIZE)) || (va_end != ROUNDUP(va_end, PGSIZE)))
		cprintf("Invalid Address!\n");

	uintptr_t va;
	for(va = va_start; va < va_end; va += PGSIZE)
	{
		pte_t * pt_entry = pgdir_walk(kern_pgdir, (void *)va, 0);
		if((pt_entry == NULL) || !(*pt_entry & PTE_P))
			cprintf("Not Mapped\n");
		
		if(perm[0] == 'u')
			*pt_entry |= PTE_U;
		if(perm[0] == 'k')
			*pt_entry &= ~PTE_U;

		if(perm[1] == 'w')
			*pt_entry |= PTE_W;
		if(perm[1] == 'r')
			*pt_entry &= ~PTE_W;
	}
	return 0;
}


int 
mon_dump(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 4)
	{
		cprintf("Command format: dump v/p va/pa size");
		return 0;
	}
	
	uint32_t startAddr;
	uint32_t endAddr;
	if(!strcmp(argv[1], "v"))
	{
		startAddr = strtol(argv[2], 0, 0);
		endAddr = strtol(argv[3], 0, 0) * 4 + startAddr;
	}
	else if(!strcmp(argv[1], "p"))	
	{
		startAddr = (uint32_t)KADDR(strtol(argv[2], 0, 0));
		endAddr = strtol(argv[3], 0, 0) * 4 + startAddr;
	}
	else
	{
		cprintf("Invalid Address Type!\n");
		return 0;
	}

	if(startAddr != ROUNDUP(startAddr, 4) || endAddr != ROUNDUP(endAddr, 4))
	{
		cprintf("Invalid Address!\n");
		return 0;
	}

	uint32_t addr;
	int i;
	for(addr = startAddr; addr < endAddr; addr += 16)
	{
		cprintf("0x%08x: ", addr);
		for(i = 0; i < 4; i++)
			cprintf("0x%08x ", *(uint32_t *)(addr + 4 * i));
		cprintf("\n");
	}
	return 0;
}

int 
mon_si(int argc, char ** argv, struct Trapframe *tf)
{
	tf->tf_eflags |= FL_TF;
	env_run(curenv);
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


	if (tf != NULL)
		print_trapframe(tf);


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
