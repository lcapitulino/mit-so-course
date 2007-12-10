#include <inc/lib.h>

const char *file = "new-file.txt";
const char *contents = "JOS rulez!!!\n";

void
umain(void)
{
	int fd;
	ssize_t bytes;

	fd = open(file, O_WRONLY | O_CREAT);
	if (fd < 0) {
		fprintf(1, "could not open '%s': %e\n", file, fd);
		exit();
	}

	bytes = write(fd, contents, strlen(contents));
	if (bytes != strlen(contents)) {
		fprintf(1, "write() didn't write everything\n");
		exit();
	}

	close(fd);
	cprintf("File seems to be created... Now do 'cat %s'\n", file);
}
