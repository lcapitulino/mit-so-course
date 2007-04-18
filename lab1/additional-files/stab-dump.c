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
	char *stabstr;
	struct stat buf;
	int found, err, fd;
	Elf32_Ehdr *header;
	struct Stab *stab, *estab;
	Elf32_Shdr *sh, *esh;

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

	/* get .stabstr */
	sh = (Elf32_Shdr *) ((void *) header + header->e_shoff);
	esh = sh + header->e_shnum;
	for (; sh < esh; sh++)
		if (sh->sh_type == SHT_STRTAB) {
			stabstr = (char *) ((void *) header + sh->sh_offset);
			break;
		}

	/* get .stab */
	found = 0;
	sh = (Elf32_Shdr *) ((void *) header + header->e_shoff);
	for (; sh < esh; sh++)
		if (sh->sh_type == SHT_PROGBITS) {
			if (found == 2)
				break;
			found++;
		}

	stab = (struct Stab *) ((void *) header + sh->sh_offset);
	estab = stab + sh->sh_size / sh->sh_entsize;
	for (; stab < estab; stab++)
		if (stab->n_type == 0x24)
			printf("%#x %s\n", stab->n_value,
			       stab->n_strx + stabstr);

	munmap(header, buf.st_size);
	return 0;
}
