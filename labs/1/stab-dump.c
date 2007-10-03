/*
 * stab-dump: Dump JOS kernel symbol table
 * 
 * Stab format info at:
 * http://sources.redhat.com/gdb/onlinedocs/stabs_toc.html
 * 
 * Luiz Fernando N. Capitulino
 * <lcapitulino@gmail.com
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>

struct Stab {
	uint32_t n_strx;	// index into string table of name
	uint8_t n_type;         // type of symbol
	uint8_t n_other;        // misc info (usually empty)
	uint16_t n_desc;        // description field
	uintptr_t n_value;	// value of symbol
};

int main(int argc, char *argv[])
{
	int err, fd;
	char *stabstr;
	struct stat buf;
	Elf32_Ehdr *header;
	Elf32_Shdr *sh, *esh;
	struct Stab *stab, *estab;

	if (argc != 2) {
		printf("usage: %s < file >\n", argv[0]);
		exit(1);
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open()");
		exit(1);
	}

	memset(&buf, 0, sizeof(buf));
	err = fstat(fd, &buf);
	if (err) {
		perror("fstat()");
		exit(1);
	}

	header = (Elf32_Ehdr *) mmap(NULL, buf.st_size, PROT_READ,
				     MAP_PRIVATE, fd, 0);
	if (header == MAP_FAILED) {
		perror("mmap()");
		exit(1);
	}
	close(fd);

	/*
	 * Get .stabstr and .stab
	 * 
	 * XXX: This code makes the following assumptions:
	 * 
	 * 1 .stabstr is the first SHT_STRTAB section
	 * 2 .stab section is the section that comes before .stabstr
	 *
	 * This looks like hardcoded info for me, since we're couting
	 * on the format of our kernel binary image. But I don't know
	 * how to make this right.
	 */
	sh = (Elf32_Shdr *) ((void *) header + header->e_shoff);
	esh = sh + header->e_shnum;
	for (; sh < esh; sh++)
		if (sh->sh_type == SHT_STRTAB) {
			stabstr = (char *) ((void *) header + sh->sh_offset);
			--sh;
			break;
		}

	stab = (struct Stab *) ((void *) header + sh->sh_offset);
	estab = stab + sh->sh_size / sh->sh_entsize;
	for (; stab < estab; stab++)
		if (stab->n_type == 0x24) {
			char *p;

			printf("%#x ", stab->n_value);

			/* Stab format for functions is:
			 * 
			 *     func_name:F(x,y)
			 * 
			 * But we're only interested in func_name
			 */
			for (p = stab->n_strx + stabstr; *p != ':'; p++)
				putchar(*p);
			putchar('\n');
		}

	munmap(header, buf.st_size);
	return 0;
}
