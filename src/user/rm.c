#include <inc/lib.h>

void
usage(void)
{
	fprintf(1, "usage: rm [ files ]\n");
	exit();
}

void
umain(int argc, char **argv)
{
	int err, i;

	if (argc < 2)
		usage();

	for (i = 1; i < argc; i++) {
		err = remove(argv[i]);
		if (err) {
			fprintf(1, "could not remove '%s': %e\n",
				argv[i], err);
		}
	}
}
