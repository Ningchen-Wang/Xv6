#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
	int fd;
	//Invalid argument number
	if (argc <= 3)
	{
		exit();
	}
	char *dir = argv[2];
	chdir(dir);
	

	if ((fd = open(argv[1], O_RDWR)) < 0)
	{
		printf(1, "vim: cannot open %s\n", argv[1]);
		chdir("/");
		exit();
	}
	
	int n, len = 0;
	char readbuf[512];
	memset(readbuf, 0, sizeof(char)*512);
	while ((n = read(fd, readbuf, 512)) > 0)
	{
		len += n;
		memset(readbuf, 0, sizeof(char)*512);
	}
	close(fd);

	if ((fd = open(argv[1], O_CREATE | O_RDWR)) < 0)
	{
		printf(1, "vim: cannot open %s\n", argv[1]);
		chdir("/");
		exit();
	}

	write(fd, argv[3], strlen(argv[3]));

	if (len > strlen(argv[3]))
	{
		char empty[2000];
		memset(empty, 0, 2000);
		write(fd, empty, len -strlen(argv[3]));		
	}	
	close(fd);
	chdir("/");
	exit();
}

