#include <inc/lib.h>

static const char *msg = "capitulino";
static const char *file = "out";

void
create_file(void)
{
	int err, fd;
	ssize_t bytes;

	fd = open(file, O_RDWR);
	assert(fd >= 0);

	bytes = write(fd, msg, strlen(msg));
	assert(bytes == strlen(msg));

	err = close(fd);
	assert(err == 0);
}

void
read_created_file(char *p)
{
	int fd;
	ssize_t bytes;

	fd = open(file, O_RDWR);
	assert(fd >= 0);

	bytes = readn(fd, p, strlen(msg));
	assert(bytes == strlen(msg));

	close(fd);
}

void
umain(void)
{
	char buf[32];

	create_file();

	memset(buf, 0, sizeof(buf));
	read_created_file(buf);

	if (strncmp(buf, msg, strlen(msg))) {
		cprintf("buf: %s\n", buf);
		panic("SHIT\n");
	}
}
