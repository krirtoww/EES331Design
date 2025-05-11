#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>

int main(int argc , char **argv)
{
	int fd_ult;
	int len;
	int val[2] = {0,0};
	char buffer[9];
	//unsigned long len = 0;
	fd_ult = open("/dev/ccd_ult",O_RDWR);
	if (fd_ult < 0)
	{
		printf("error\n");
	}
	int j;
	while(1)
	{	
		sleep(1);
		len = read(fd_ult,buffer,sizeof(buffer)-1);
		buffer[len] = '\0';
		memcpy(val, buffer, len);
		if(val[1] == -1)
			printf("out of range\n");
		else
			printf("dis:%d state:%d\n",val[0]/58,val[1]);
	}
}
