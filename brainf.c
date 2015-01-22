#include "types.h"
#include "stat.h"
#include "user.h"
#include "brainf.h"

#define MEMORYSIZE 1024

char code[512];
char input[100];
char mem[MEMORYSIZE];
int brainfFlag = 0;
int inputN = 0;

void run(void)
{   int l=strlen(code),ip=0,si=0,err=0;
    int i;
    for(i=0;i<MEMORYSIZE;i++)mem[i]=0;
    while(ip<l&&err==0)
    {

        switch (code[ip])
        {
            case '<':
                si--;
                if(si<0)err=2;
                break;
            case '>':
                si++;
                if(si>=MEMORYSIZE)err=2;
                break;
            case '+':
                if(mem[si]==127)err=4;
                mem[si]++;
                break;
            case '-':
                if(mem[si]==0)err=4;
                mem[si]--;
                break;
            case '.':
                printf(1, "%c",mem[si]);
                break;
            case ',':
		mem[si] = input[inputN++];
                break;
            case '[':
                if(mem[si]==0)
                {   while(ip<MEMORYSIZE)
                    {   ip++;
                        if(code[ip]==']'&&err==0)break;
                        if(code[ip]=='[')err++;
                        else if(code[ip]==']')err--;
                        if(ip>=l){err=8;break;}
                    }
                }
                break;
            case ']':
                if(mem[si]!=0)
                {   while(ip>=0)
                    {   ip--;
                        if(code[ip]=='['&&err==0)break;
                        if(code[ip]==']')err++;
                        else if(code[ip]=='[')err--;
                        if(ip<=0){err=8;break;}
                    }
                }
                break;
            case '/':
                break;
	    case ' ':
 		break;
	    case '\n':
		break;
            default:
                err=1;
                break;
        }
        if(err==0)ip++;
    }
    switch(err)
    {   case 0:
            break;
        case 1:
            printf(1,"-!--{%c}.%d is illegal char!",code[ip],ip);
            break;
        case 2:
            printf(1,"-!-- slop over");
            break;
        case 4:
            printf(1,"-!-- overflow");
            break;
        case 8:
            printf(1,"-!-- logical error!");
            break;
        default:
            break;
    }
}

void
brainf(int fd)
{
  int n;
  while((n = read(fd, code, sizeof(code))) > 0)
    write(1, code, n);
  if(n < 0){
    printf(1, "cat: read error\n");
    exit();
  }
  else
  {
	printf(1, "input:\n");
	brainfFlag = 1;
	while (brainfFlag)
	{
	  gets(input, 100);
          if (input[0] != ' ') brainfFlag =0;
	}
	printf(1, "reuslt:\n");
	run();
	printf(1, "\n");
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){
    printf(1, "usage: brainf filename\n");
    exit();
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      printf(1, "brainf: cannot open %s\n", argv[i]);
      exit();
    }
    brainf(fd);
    close(fd);
  }
  exit();
}

