// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	
	if(((err & FEC_WR) == 0) || ((vpd[PDX(addr)] & PTE_P) == 0) || ((vpt[PGNUM(addr)] & PTE_COW) == 0))
		panic("pgfault: faulting access inproper");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.

	envid_t envid = sys_getenvid();
	r = sys_page_alloc(envid, PFTEMP, PTE_U|PTE_P|PTE_W);
	if(r < 0)
		panic("pgfault: sys_page_alloc error: %e", r);
	
	addr = ROUNDDOWN(addr, PGSIZE);		
	memmove(PFTEMP, addr, PGSIZE);
	r = sys_page_map(envid, PFTEMP, envid, addr, PTE_U|PTE_P|PTE_W);
	if(r < 0)
		panic("pgfault: sys_page_map error: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.

	void * srcva = (void *)(pn * PGSIZE);
	pte_t pt_entry = vpt[PGNUM(srcva)];

	if(((pt_entry & PTE_W) != 0) || ((pt_entry & PTE_COW) != 0))
	{
		r = sys_page_map(0, srcva, envid, srcva, PTE_P|PTE_U|PTE_COW);
		if(r < 0)
			panic("duppage: first sys_page_map error: %e", r);
		
		r = sys_page_map(0, srcva, 0, srcva, PTE_U|PTE_P|PTE_COW);
		if(r < 0)
			panic("duppage: second sys_page_map error: %e", r);
	}
	
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	
	int r;
	
	set_pgfault_handler(pgfault);	

	envid_t childid = sys_exofork();
	if(childid < 0)
		panic("fork: sys_exofork error: %e", childid);
	
	if(childid == 0)
	{
		thisenv = envs + ENVX(sys_getenvid());
		return 0;
	}
	
	uint32_t va;
	for(va = UTEXT; va < UXSTACKTOP - PGSIZE; va += PGSIZE)
	{
		if(((vpd[PDX(va)] & PTE_P) != 0) && ((vpt[PGNUM(va)] & (PTE_P|PTE_U)) != 0))
		{
			r = duppage(childid, PGNUM(va));
			if(r < 0)
				panic("fork: duppage error: %e", r);
		}
	}		

	r = sys_page_alloc(childid, (void *)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W);
	if(r < 0)
		panic("fork: sys_page_alloc error %e", r);

	extern void _pgfault_upcall(void);
	r = sys_env_set_pgfault_upcall(childid, _pgfault_upcall);
	if(r < 0)
		panic("fork: sys_set_pgfault_upcall error: %e", r);
	
	r = sys_env_set_status(childid, ENV_RUNNABLE);
	if(r < 0)
		panic("fork: sys_env_set_status error: %e", r);
	
	return childid;
}

static int
sduppage(envid_t envid, unsigned pn, int need_cow)
{
	int r;
	void * addr = (void *) ((uint32_t) pn * PGSIZE);
	pte_t pt_entry = vpt[PGNUM(addr)];

	if (need_cow || (pt_entry & PTE_COW) > 0)
	{
		if ((r = sys_page_map (0, addr, envid, addr, PTE_U|PTE_P|PTE_COW)) < 0)
			panic ("sduppage: sys_page_map error: %e", r);
		if ((r = sys_page_map (0, addr, 0, addr, PTE_U|PTE_P|PTE_COW)) < 0)
			panic ("sduppage: sys_page_map error: %e", r);
	} 
	else
	{
		if((pt_entry & PTE_W) > 0)
		{	
			if((r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P|PTE_W)) < 0)
				panic ("sduppage: sys_page_map error: %e", r);
		}
		else
		{
			if((r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P)) < 0)
				panic ("sduppage: sys_page_map error: %e", r);
		}
	}
	return 0;
}


// Challenge!
int
sfork(void)
{
	set_pgfault_handler(pgfault);
	
	envid_t childid;
	uint32_t va;
	int r;

	childid = sys_exofork();
	if(childid < 0)
		panic("sfork: sys_exofork error: %e", childid);

        if(childid == 0)
        {
                thisenv = envs + ENVX(sys_getenvid());
                return 0;
        }
	
	int isstack = 1;
	for(va = USTACKTOP - PGSIZE; va >= UTEXT; va -= PGSIZE)
	{
                if(((vpd[PDX(va)] & PTE_P) != 0) && ((vpt[PGNUM(va)] & (PTE_P|PTE_U)) != 0))
			sduppage(childid, PGNUM(va), isstack);
		else
			isstack = 0;
	}

        r = sys_page_alloc(childid, (void *)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W);
        if(r < 0)
                panic("fork: sys_page_alloc error %e", r);

        extern void _pgfault_upcall(void);
        r = sys_env_set_pgfault_upcall(childid, _pgfault_upcall);
        if(r < 0)
                panic("fork: sys_set_pgfault_upcall error: %e", r);

        r = sys_env_set_status(childid, ENV_RUNNABLE);
        if(r < 0)
                panic("fork: sys_env_set_status error: %e", r);

        return childid;
}
