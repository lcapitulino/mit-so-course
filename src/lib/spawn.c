#include <inc/lib.h>
#include <inc/elf.h>

#define UTEMP2USTACK(addr)	((void*) (addr) + (USTACKTOP - PGSIZE) - UTEMP)
#define UTEMP2			(UTEMP + PGSIZE)
#define UTEMP3			(UTEMP2 + PGSIZE)

// Helper functions for spawn.
static int init_stack(envid_t child, const char **argv, uintptr_t *init_esp);
static int check_elf_header(const struct Elf *hdr);
static void dump_prog_header(const struct Proghdr *hdr);
static int segment_map_ro(int fd, envid_t child, const struct Proghdr *hdr);
static int segment_map_rw(int fd, envid_t child, const struct Proghdr *hdr);
static int copy_shared_pages(envid_t child);

// Spawn a child process from a program image loaded from the file system.
// prog: the pathname of the program to run.
// argv: pointer to null-terminated array of pointers to strings,
// 	 which will be passed to the child as its command-line arguments.
// Returns child envid on success, < 0 on failure.
int
spawn(const char *prog, const char **argv)
{
	unsigned char elf_buf[512];
	struct Trapframe child_tf;
	uint16_t phentsize, phnum;
	off_t phdr_offset;
	struct Elf *hdr;
	int i, err, fd;
	envid_t child;
	ssize_t bytes;

	// Insert your code, following approximately this procedure:
	//
	//   - Open the program file.
	//
	//   - Read the ELF header, as you have before, and sanity check its
	//     magic number.  (Check out your load_icode!)
	//
	//   - Use sys_exofork() to create a new environment.
	//
	//   - Set child_tf to an initial struct Trapframe for the child.
	//     Hint: The sys_exofork() system call has already created
	//     a good basis, in envs[ENVX(child)].env_tf.
	//     Hint: You must do something with the program's entry point.
	//     What?  (See load_icode!)
	//
	//   - Call the init_stack() function above to set up
	//     the initial stack page for the child environment.
	//
	//   - Map all of the program's segments that are of p_type
	//     ELF_PROG_LOAD into the new environment's address space.
	//     Use the p_flags field in the Proghdr for each segment
	//     to determine how to map the segment:
	//
	//	* If the ELF flags do not include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains text and read-only data.
	//	  Use read_map() to read the contents of this segment,
	//	  and map the pages it returns directly into the child
	//        so that multiple instances of the same program
	//	  will share the same copy of the program text.
	//        Be sure to map the program text read-only in the child.
	//        Read_map is like read but returns a pointer to the data in
	//        *blk rather than copying the data into another buffer.
	//
	//	* If the ELF segment flags DO include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains read/write data and bss.
	//	  As with load_icode() in Lab 3, such an ELF segment
	//	  occupies p_memsz bytes in memory, but only the FIRST
	//	  p_filesz bytes of the segment are actually loaded
	//	  from the executable file - you must clear the rest to zero.
	//        For each page to be mapped for a read/write segment,
	//        allocate a page in the parent temporarily at UTEMP,
	//        read() the appropriate portion of the file into that page
	//	  and/or use memset() to zero non-loaded portions.
	//	  (You can avoid calling memset(), if you like, if
	//	  page_alloc() returns zeroed pages already.)
	//        Then insert the page mapping into the child.
	//        Look at init_stack() for inspiration.
	//        Be sure you understand why you can't use read_map() here.
	//
	//     Note: None of the segment addresses or lengths above
	//     are guaranteed to be page-aligned, so you must deal with
	//     these non-page-aligned values appropriately.
	//     The ELF linker does, however, guarantee that no two segments
	//     will overlap on the same page; and it guarantees that
	//     PGOFF(ph->p_offset) == PGOFF(ph->p_va).
	//
	//   - Call sys_env_set_trapframe(child, &child_tf) to set up the
	//     correct initial eip and esp values in the child.
	//
	//   - Start the child process running with sys_env_set_status().

	// LAB 5: Your code here.

	// fork before open(), otherwise sys_exofork() will
	// duplicate fd in the child
	child = sys_exofork();
	if (child < 0)
		return (int) child;

	fd = open(prog, O_RDONLY);
	if (fd < 0) {
		err = fd;
		goto out_err;
	}

	memset(elf_buf, 0, sizeof(*hdr));
	bytes = readn(fd, elf_buf, sizeof(*hdr));
	if (bytes != sizeof(*hdr)) {
		err = -E_INVAL;
		goto out_err;
	}

	hdr = (struct Elf *) elf_buf;

	err = check_elf_header(hdr);
	if (err)
		goto out_err;

	child_tf = envs[ENVX(child)].env_tf;
	child_tf.tf_regs = envs[ENVX(child)].env_tf.tf_regs;
	child_tf.tf_eip = hdr->e_entry;
	err = init_stack(child, argv, &child_tf.tf_esp);
	if (err)
		goto out_err;

	err = sys_env_set_trapframe(child, &child_tf);
	if (err)
		goto out_err;

	phentsize = hdr->e_phentsize;
	phnum = hdr->e_phnum;
	phdr_offset = hdr->e_phoff;

	// Read ELF_PROG_LOAD segments into memory
	for (i = 0; i < phnum; i++) {
		struct Proghdr *phdr;

		// Always go to the right prog header
		err = seek(fd, phdr_offset + i * phentsize);
		if (err)
			goto out_err;

		memset(elf_buf, 0, phentsize);
		bytes = readn(fd, elf_buf, phentsize);
		if (bytes != phentsize) {
			err = -E_INVAL;
			goto out_err;
		}

		phdr = (struct Proghdr *) elf_buf;

		if (phdr->p_type != ELF_PROG_LOAD)
			continue;

		// XXX: The code assumes this
		assert(phdr->p_memsz >= phdr->p_filesz);

		if (phdr->p_flags & ELF_PROG_FLAG_WRITE)
			err = segment_map_rw(fd, child, phdr);
		else
			err = segment_map_ro(fd, child, phdr);
		if (err)
			goto out_err;
	}

	// Close fd before copy_shared_pages(), otherwise
	// it'll duplicate fd in the child
	close(fd);
	fd = -1;

	err = copy_shared_pages(child);
	if (err)
		goto out_err;

	err = sys_env_set_status(child, ENV_RUNNABLE);
	if (err)
		goto out_err;

out:
	if (fd >= 0)
		close(fd);
	return (err < 0 ? err : child);

out_err:
	sys_env_destroy(child);
	goto out;
}

// Spawn, taking command-line arguments array directly on the stack.
int
spawnl(const char *prog, const char *arg0, ...)
{
	return spawn(prog, &arg0);
}


// Set up the initial stack page for the new child process with envid 'child'
// using the arguments array pointed to by 'argv',
// which is a null-terminated array of pointers to null-terminated strings.
//
// On success, returns 0 and sets *init_esp
// to the initial stack pointer with which the child should start.
// Returns < 0 on failure.
static int
init_stack(envid_t child, const char **argv, uintptr_t *init_esp)
{
	size_t string_size;
	int argc, i, r;
	char *string_store;
	uintptr_t *argv_store;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (string_size).
	string_size = 0;
	for (argc = 0; argv[argc] != 0; argc++)
		string_size += strlen(argv[argc]) + 1;

	// Determine where to place the strings and the argv array.
	// Set up pointers into the temporary page 'UTEMP'; we'll map a page
	// there later, then remap that page into the child environment
	// at (USTACKTOP - PGSIZE).
	// strings is the topmost thing on the stack.
	string_store = (char*) UTEMP + PGSIZE - string_size;
	// argv is below that.  There's one argument pointer per argument, plus
	// a null pointer.
	argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));
	
	// Make sure that argv, strings, and the 2 words that hold 'argc'
	// and 'argv' themselves will all fit in a single stack page.
	if ((void*) (argv_store - 2) < (void*) UTEMP)
		return -E_NO_MEM;

	// Allocate the single stack page at UTEMP.
	if ((r = sys_page_alloc(0, (void*) UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		return r;

	// Replace this with your code to:
	//
	//	* Initialize 'argv_store[i]' to point to argument string i,
	//	  for all 0 <= i < argc.
	//	  Also, copy the argument strings from 'argv' into the
	//	  newly-allocated stack page.
	//	  Hint: Copy the argument strings into string_store.
	//	  Hint: Make sure that argv_store uses addresses valid in the
	//	  CHILD'S environment!  The string_store variable itself
	//	  points into page UTEMP, but the child environment will have
	//	  this page mapped at USTACKTOP - PGSIZE.  Check out the
	//	  UTEMP2USTACK macro defined above.
	//
	//	* Set 'argv_store[argc]' to 0 to null-terminate the args array.
	//
	//	* Push two more words onto the child's stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the child's umain() function.
	//	  argv should be below argc on the stack.
	//	  (Again, argv should use an address valid in the child's
	//	  environment.)
	//
	//	* Set *init_esp to the initial stack pointer for the child,
	//	  (Again, use an address valid in the child's environment.)
	//
	// LAB 5: Your code here.
	for (i = 0; i < argc; i++) {
		argv_store[i] = UTEMP2USTACK(string_store);
		string_size = strlen(argv[i]) + 1;
		memcpy(string_store, argv[i], string_size);
		string_store += string_size;
	}

	argv_store[argc] = 0;

	*(argv_store - 1) = UTEMP2USTACK(&argv_store[0]);
	*(argv_store - 2) = argc;

	*init_esp = UTEMP2USTACK(argv_store - 2);

	// After completing the stack, map it into the child's address space
	// and unmap it from ours!
	if ((r = sys_page_map(0, UTEMP, child, (void*) (USTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		goto error;
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		goto error;

	return 0;

error:
	sys_page_unmap(0, UTEMP);
	return r;
}

// Check if the Elf header is valid.
// 
// This function does a lot of sanity checks and assures that
// the elf header describes a i386 executable file.
// 
// Returns 0 on sucess, -E_INVAL otherwise
static int
check_elf_header(const struct Elf *hdr)
{
	if (!elf_header_is_valid(hdr))
		return -E_INVAL;

	if (hdr->e_type != ET_EXEC)
		return -E_INVAL;

	if (hdr->e_machine != EM_386)
		return -E_INVAL;

	if (hdr->e_entry <= 0)
		return -E_INVAL;

	if (hdr->e_phoff <= 0)
		return -E_INVAL;

	if (hdr->e_phentsize <= 0)
		return -E_INVAL;

	if (hdr->e_phnum <= 0)
		return -E_INVAL;

	return 0;
}

#if 0
    Only for debug purposes

// Dump Elf program header
static void
dump_prog_header(const struct Proghdr *hdr)
{
	switch (hdr->p_type) {
	case 0:
		cprintf("PT_NULL");
		break;
	case 1:
		cprintf("PT_LOAD");
		break;
	case 2:
		cprintf("PT_DYNAMIC");
		break;
	case 3:
		cprintf("PT_INTERP");
		break;
	case 4:
		cprintf("PT_NOTE");
		break;
	case 5:
		cprintf("PT_SHLIB");
		break;
	case 6:
		cprintf("PT_PHDR");
		break;
	default:
		cprintf("Unknown");
		break;
	}

	cprintf(": ");
	cprintf("Offset:   0x%08x ", hdr->p_offset);
	cprintf("VirtAddr: 0x%08x ", hdr->p_va);
	cprintf("MemSiz:   0x%08x", hdr->p_memsz);
	cprintf("\n");
}
#endif

// Map a RO segment from file (fd) into child.
// 
// This function maps the entire segment described by 'hdr'
// into the address range also described by 'hdr'.
// 
// Note that there's no copy involved in this function. We're
// using read_map() which maps the disk blocks into the
// caller's memory and then we map those pages into the
// child's address space.
// 
// This function is supposed to deal with not-aligned address
// correctly.
// 
// Return 0 on success, (negative) error code on failure
static int
segment_map_ro(int fd, envid_t child, const struct Proghdr *hdr)
{
	int err;
	void *blk;
	uintptr_t va;
	off_t offset;
	size_t i, memsz;

	memsz = ROUNDUP(hdr->p_memsz, PGSIZE);
	offset = ROUNDDOWN(hdr->p_offset, PGSIZE);
	va = ROUNDDOWN(hdr->p_va, PGSIZE);

	for (i = 0; i < memsz; i += PGSIZE) {
		err = read_map(fd, offset + i, &blk);
		if (err)
			return err;

		err = sys_page_map(0, blk, child, (void *) va + i,PTE_P|PTE_U);
		if (err)
			return err;
	}

	return 0;
}

#if 0

-> See in segment_map_rw() why this is commented.

// Handle not-aligned addresses
// 
// If a virtual address is not-aligned, we should copy it into
// the right offset inside its page in memory.
// 
// Return 0 on success, (negative) error code on failure
static int
map_unaligned_va(int fd, envid_t child, uintptr_t va, size_t *n)
{
	int err;
	size_t ret, bytes;

	err = sys_page_alloc(0, UTEMP, PTE_P|PTE_U|PTE_W);
	if (err)
		return err;

	bytes = PGSIZE - PGOFF(va);
	ret = readn(fd, UTEMP + PGOFF(va), bytes);
	if (ret != bytes)
		return -E_INVAL;

	err = sys_page_map(0, UTEMP, child, (void *) va, PTE_P|PTE_U|PTE_W);
	if (err)
		return err;

	err = sys_page_unmap(0, UTEMP);
	if (err)
		return err;

	*n = bytes;
	return 0;
}
#endif

// Map a RW segment from file (fd) into child.
// 
// This function reads the entire segment described by 'hdr'
// into the address range also described by 'hdr'.
// 
// It's supposed to deal with non-aligned addresses.
// 
// Return 0 on success, (negative) error code on failure
static int
segment_map_rw(int fd, envid_t child, const struct Proghdr *hdr)
{
	int err;
	uintptr_t va;
	size_t i, memsz, filesz;

	err = seek(fd, hdr->p_offset);
	if (err)
		return err;

	filesz = hdr->p_filesz;
	memsz = ROUNDUP(hdr->p_memsz, PGSIZE);
	va = ROUNDDOWN(hdr->p_va, PGSIZE);
	if (va != hdr->p_va)
		panic("Time to test map_unaligned_va()!!\n");

#if 0
	// This function is commented because I don't have any
	// test case for it. If you've triggered the panic()
	// above, please uncomment and give it a try.
	//                              lcapitulino
	if (va != hdr->p_va) {
		size_t n;

		err = map_unaligned_va(fd, child, hdr->p_va, &n);
		if (err)
			return err;
		memsz -= PGSIZE;
		filesz -= n;
	}
#endif

	for (i = 0; i < memsz; i += PGSIZE) {
		size_t ret, bytes;

		err = sys_page_alloc(0, UTEMP, PTE_P|PTE_U|PTE_W);
		if (err)
			return err;

		bytes = filesz > PGSIZE ? PGSIZE : filesz;

		ret = readn(fd, UTEMP, bytes);
		if (ret != bytes)
			return -E_INVAL;

		filesz -= ret;
		assert(filesz >= 0);

		err = sys_page_map(0, UTEMP, child, (void *) va + i,
				   PTE_P|PTE_U|PTE_W);
		if (err)
			return err;

		err = sys_page_unmap(0, UTEMP);
		if (err)
			return err;
	}

	return 0;
}

static int map_shared_page(envid_t child, uint32_t pn)
{
	int perm;
	void *addr;

	addr = (void *) (pn * PGSIZE);
	perm = vpt[pn] & PTE_USER;

	return sys_page_map(0, addr, child, addr, perm);
}

static int copy_shared_pages(envid_t child)
{
	uint32_t pn;
	int pdeno, pteno, err;

	pn = 0;
	for (pdeno = 0; pdeno < VPD(UTOP); pdeno++) {
		if (vpd[pdeno] == 0) {
			// skip empty PDEs
			pn += NPTENTRIES;
			continue;
		}

		for (pteno = 0; pteno < NPTENTRIES; pteno++,pn++) {
			if (vpt[pn] == 0)
				continue;

			if (!(vpt[pn] & PTE_SHARE))
				continue;

			err = map_shared_page(child, pn);
			if (err)
				return err;
		}
	}

	return 0;
}


