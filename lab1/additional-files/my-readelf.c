#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <elf.h>

static void show_section_type(Elf32_Word type)
{
	switch (type) {
	case 0:
		printf("SHT_NULL\n");
		break;
	case 1:
		printf("SHT_PROGBITS\n");
		break;
	case 2:
		printf("SHT_SYMTAB\n");
		break;
	case 3:
		printf("SHT_STRTAB\n");
		break;
	case 4:
		printf("SHT_RELA\n");
		break;
	case 5:
		printf("SHT_HASH\n");
		break;
	case 6:
		printf("SHT_DYNAMIC\n");
		break;
	case 7:
		printf("SHT_NOTE\n");
		break;
	case 8:
		printf("SHT_NOBITS\n");
		break;
	case 9:
		printf("SHT_REL\n");
		break;
	case 10:
		printf("SHT_SHLIB\n");
		break;
	case 11:
		printf("SHT_DYNSYM\n");
		break;
	default:
		printf("%#x\n", type);
	}
}

struct Stab {
	uint32_t n_strx;	// index into string table of name
	uint8_t n_type;         // type of symbol
	uint8_t n_other;        // misc info (usually empty)
	uint16_t n_desc;        // description field
	uintptr_t n_value;	// value of symbol
};

int main(int argc, char *argv[])
{
	ssize_t bytes;
	Elf32_Ehdr header;
	Elf32_Shdr section;
	struct Stab symbol;
	char *p, *file, *strtab;
	int err, i, fd, found;

	if (argc != 2) {
		printf("usage: %s < file >\n", argv[0]);
		exit(1);
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open()");
		exit(1);
	}

	/* Read Elf header */
	memset(&header, 0, sizeof(Elf32_Ehdr));
	bytes = read(fd, &header, sizeof(Elf32_Ehdr));
	if (bytes != sizeof(Elf32_Ehdr)) {
		fprintf(stderr, "read() failed\n");
		exit(1);
	}

	/* read .stabstr in memory */
	err = (int) lseek(fd, header.e_shoff, SEEK_SET);
	if (err < 0) {
		perror("lseek()");
		exit(1);
	}

	for (i = 0; i < header.e_shnum; i++) {
		memset(&section, 0, sizeof(Elf32_Shdr));
		bytes = read(fd, &section, header.e_shentsize);
		if (bytes != header.e_shentsize) {
			fprintf(stderr, "couldn't read section %d\n", i);
			exit(1);
		}

		if (section.sh_type == SHT_STRTAB)
				break;
	}

	err = (int) lseek(fd, section.sh_offset, SEEK_SET);
	if (err < 0) {
		perror("lseek()");
		exit(1);
	}

	strtab = malloc(section.sh_size);
	if (!strtab) {
		perror("malloc()");
		exit(1);
	}

	bytes = read(fd, strtab, section.sh_size);
	if (bytes != section.sh_size) {
		fprintf(stderr, "cound't read strtab\n");
		exit(1);
	}

#if 0
	p = strtab + 1;
	for (i = 0; i < 40; i++) {
		printf("%s\n", p);
		p = strchr(p, '\0');
		if (!p)
			exit(0);
		p++;
	}
	exit(0);
#endif

	/* find the .stab */
	err = (int) lseek(fd, header.e_shoff, SEEK_SET);
	if (err < 0) {
		perror("lseek()");
		exit(1);
	}

	found = 0;

	for (i = 0; i < header.e_shnum; i++) {
		memset(&section, 0, sizeof(Elf32_Shdr));
		bytes = read(fd, &section, header.e_shentsize);
		if (bytes != header.e_shentsize) {
			fprintf(stderr, "couldn't read section %d\n", i);
			exit(1);
		}
		if (section.sh_type == SHT_PROGBITS) {
			if (found == 2)
				break;
			found++;
		}
	}

	/* go to .stab section */
	err = (int) lseek(fd, section.sh_offset, SEEK_SET);
	if (err < 0) {
		perror("lseek()");
		exit(1);
	}

	for (i = 0; i < (section.sh_size / section.sh_entsize); i++) {
		memset(&symbol, 0, sizeof(Elf32_Sym));
		bytes = read(fd, &symbol, section.sh_entsize);
		if (bytes != section.sh_entsize) {
			fprintf(stderr, "couldn't read symbol %d\n", i);
			exit(1);
		}
		if (symbol.n_type != 0x24)
			continue;

		printf("%#x %s\n", symbol.n_value, strtab + symbol.n_strx);
	}

	close(fd);
	return 0;
}
