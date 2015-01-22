#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
	//Invalid argument number
	if (argc == 1 || argc == 2)
	{
		printf(1, "rename Usage: rename [src_file] [dest_file]\n");
		exit();
	}
	int result = link(argv[1], argv[2]);
	//link operation success
	if (result == 0)
		unlink(argv[1]);
	else
		printf(1, "rename failed, check if this file or directory exists.\n");
	exit();
}

