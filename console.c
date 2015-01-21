// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
//#include "user.h"

//Test for history recording
#define MAX_HISTORY 3
#define MAX_CMD 100

char cmdlist[100][100];
int tab_times = 0;
int match_length = 0;

struct history
{
	char cmds[MAX_HISTORY][MAX_CMD];
	int current;
    int record;
    int flag;
    int pos;
    int recordNum;
};
struct history his = {.current = 0, .record = 0, .flag = 0, .pos = 0, .recordNum = 0};

int recordHistory(char* cmd)
{
	int i;
    if(his.recordNum < MAX_HISTORY) his.recordNum++;
	for(i = 0; i < MAX_CMD; i++)
	{
		his.cmds[his.current][i] = cmd[i];
        if(cmd[i] == '\0') break;
	}
	his.current++;
    if(his.current >= MAX_HISTORY)
	{
		his.current = 0;
	}
    his.record = 1;
	return 0;
}

char* fetchHistory()
{
	return his.cmds[his.current];
}

int len;
char str[MAX_CMD];
//End

#define NULL 0x0

static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];
  
  cli();
  cons.locking = 0;
  cprintf("cpu%d: panic: ", cpu->id);
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

//Test for left and right arrow
char buffer[MAX_CMD];
int bufferPos = 0;

int concatInput()
{
    int i = bufferPos;
    int pos;
    outb(CRTPORT, 14);
    pos = inb(CRTPORT+1) << 8;
    outb(CRTPORT, 15);
    pos |= inb(CRTPORT+1);
    for(i = bufferPos - 1; i >= 0; i--)
    {
        crt[pos++] = (buffer[i] & 0xff) | 0x0700;
    }
    //printint(pos, 10, 1);
    crt[pos] = ' ' | 0x0700;
    return 0;
}
//

static void
cgaputc(int c)
{
  int pos;
  
  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } //else if(c == '\t') crt[pos++] = ('l'&0xff) | 0x700;
  //Test
  else if(c == 0xe4){
    if(pos > 0){
        --pos;
        buffer[bufferPos++] = crt[pos];
    }
  }
  //
         else crt[pos++] = (c&0xff) | 0x0700;  // black on white
  
  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }
  
  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  if(c != 0xe4) crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}

#define INPUT_BUF 128
struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x

int getCommondList() {
    struct inode *ip = namei("/");
    struct dirent de;
    int i = 0;
    int j = 0;
    char file_path[100];
    file_path[0] = '/';
    while (1) {
       readi(ip, (char*)&de, sizeof(de)*i, sizeof(de));
       if(de.inum == 0)
           break;
       memmove(file_path + 1, de.name, DIRSIZ);
       file_path[1 + DIRSIZ] = 0;
       struct inode *singlefile = namei(file_path);
       //cprintf("%d %d %d\n", temp, de.inum, singlefile -> type);
       if(singlefile -> type == 0 || singlefile -> type == 2) {
           strncpy(cmdlist[j], de.name, DIRSIZ);
           //cprintf("%s \n", cmdlist[j]);
           j++;
       } 
       //cprintf("%d ", singlefile -> type);
       //cprintf("%s \n", file_path);
       i++;
    }
    //ip = proc->cwd;    
    //int x = ip->type;
    //cprintf("%d %s", x, de.name);
    return j;
}

char* match_commond(char* commondPrefix) {
    //consputc(strlen(commondPrefix)+'0');
    int i = 0;
	int j = 0;
    int num = 0;
    num = getCommondList();
    int flag = 0;
    char* firstmatch = NULL;
	//cprintf("%s\n", commondPrefix);
    while (j != num) {
        i = 0; 
		//cprintf("%d %s\n", j, cmdlist[j]);
        if (strlen(cmdlist[j]) < strlen(commondPrefix)) {j++; continue;}
        while (i < strlen(commondPrefix)) if (cmdlist[j][i] == commondPrefix[i]) i++; else break;
        if (i >= strlen(commondPrefix)) {
			if (flag == 0) firstmatch = cmdlist[j];
			flag++; 
            if(flag == tab_times) return cmdlist[j];
 		}
        j++;
        //num--;
    }
    //cprintf("%d %d %d\n", flag, tab_times, num);
    if (flag < tab_times) {tab_times = 1;  return firstmatch;}	else return NULL;
}

void
consoleintr(int (*getc)(void))
{
  int c;
  int i;

  acquire(&input.lock);
  char commondPrefix[INPUT_BUF];
  int length;
  while((c = getc()) >= 0){
    if(c != '\t' && c != 0) {tab_times = 0;}
    switch(c){
    case C('P'):  // Process listing.
      his.flag = 0;//history flag
      bufferPos = 0;
      procdump();
      break;
    case '\t':
      bufferPos = 0;
      his.flag = 0;//history flag
      if (input.e != input.w) {
      	  tab_times ++;
          //cprintf("%d %d", tab_times, match_length);
          if (tab_times == 1) {
              match_length = 0;
          } 
          else {
              while (match_length != 0) {
                  if(input.e != input.w){
                      input.e--;
                      consputc(BACKSPACE);
                      if(len > 0) len--;
	                  concatInput();
                  }
                  match_length--;
              }
          }
          i = input.e - 1;
          while (input.buf[i % INPUT_BUF] != ' ' && i != input.w) i--;
          length = 0;
          while (i != input.e) commondPrefix[length++] = input.buf[i++ % INPUT_BUF];
          commondPrefix[length++] = '\0';
          //cprintf("%s %d", commondPrefix, tab_times);
          char* match_ans;
          match_ans = match_commond(commondPrefix);
          if (match_ans != NULL) {
              i = length - 1;
              while (match_ans[i] != '\0') {
                  input.buf[input.e++ % INPUT_BUF] = match_ans[i];
                  str[len++] = match_ans[i];
                  consputc(match_ans[i++]);
                  match_length++;
                  //cprintf("%d", match_length);
              }
          } 
      }
      break;
    case C('U'):  // Kill line.
      his.flag = 0;//history flag
      bufferPos = 0;
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
        if(len > 0) len--;
        concatInput();
      }
      break;
    //case 0xE2: Test for up key
	case 0xE2:
        if (his.recordNum == 0)
           continue;
        if(his.flag == 0)
        {
            his.pos = his.current - 1;
            if (his.pos < 0) 
                his.pos = his.recordNum-1;
            his.flag = 1;
        }
        else
        {
            if (his.pos  == his.current )
               continue;
            if (his.recordNum < MAX_HISTORY && his.pos == 0)
               continue;
            his.pos--;
            if (his.pos < 0)
               his.pos = his.recordNum-1;

        }
	bufferPos = 0;
	    if(his.record == 1)
	    {
            while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
                input.e--;
                consputc(BACKSPACE);
            }

            len = 0;
		    int i;

		    for(i = 0; i < (sizeof(his.cmds[his.pos]) / sizeof(char)); i++)
		    {
                if(his.cmds[his.pos][i] == '\0') break;
			    input.buf[input.e++ % INPUT_BUF] = his.cmds[his.pos][i];
			    consputc(his.cmds[his.pos][i]);
                str[len++] = his.cmds[his.pos][i];
		    }

	    }
	  break;
    //case 0xE3: Test for down key
    case 0xE3:	
           if (his.recordNum == 0)
              continue;
            if(his.flag == 0)
            {
               continue;
            }
            else
            {
                int now;
                now = his.current - 1;
                if (now < 0)
                   now = his.recordNum-1; 
                if (his.pos  == now)
                   continue;
                if (his.recordNum < MAX_HISTORY && his.pos == his.recordNum - 1)
                   continue;
                his.pos++;
                if (his.pos >= his.recordNum)
                   his.pos = 0; 
            }
	    bufferPos = 0;
			if(his.record == 1)
			{
                while(input.e != input.w &&
                input.buf[(input.e - 1) % INPUT_BUF] != '\n'){
                    input.e--;
                    consputc(BACKSPACE);
                }

                len = 0;
				int i;

				for(i = 0; i < (sizeof(his.cmds[his.pos]) / sizeof(char)); i++)
				{
                    if(his.cmds[his.pos][i] == '\0') break;
					input.buf[input.e++ % INPUT_BUF] = his.cmds[his.pos][i];
					consputc(his.cmds[his.pos][i]);
                    str[len++] = his.cmds[his.pos][i];
				}
			}
	  break; 
  //Test
	case 0xE4:
	    if(input.e != input.w){
        input.e--;
        //consputc(BACKSPACE);
        consputc(0xe4);
        if(len > 0) len--;
      }
      break;
    /*
    case 0xE5:
      if (bufferPos > 0)
      {
          input.buf[input.e++ % INPUT_BUF] = buffer[bufferPos-1];
          str[len++] = buffer[bufferPos-1];//Preparation for building a history record
          consputc(buffer[bufferPos-1]);
          bufferPos --;
          concatInput();
      }
 
       
	
      break;
    */
  //
    /*
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    */
    case 0xE5:
	if (bufferPos > 0)
	{
		bufferPos --;
	    	input.buf[input.e++ % INPUT_BUF] = buffer[bufferPos];
		str[len++] = buffer[bufferPos];//Preparation for building a history record
		consputc(buffer[bufferPos]);
		concatInput();
	}
	break;

    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        if(c == '\n'){
            int i;
      		for(i = bufferPos - 1; i >= 0; i--)
		    {
			    input.buf[input.e++ % INPUT_BUF] = buffer[i];
			    str[len++] = buffer[i];
		    }
		    bufferPos = 0;
		}    
        input.buf[input.e++ % INPUT_BUF] = c;
        str[len++] = c;//Preparation for building a history record
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF){
          his.flag = 0;
          input.w = input.e;
          str[len - 1] = '\0';//Add a '\0' in the end
          recordHistory(str);
          len = 0;//reset len for the next recording preparation
          wakeup(&input.r);
        }
        else{
            concatInput();
        }
      }
	
      break;
    }
  }
  release(&input.lock);
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&input.lock);
  while(n > 0){
    while(input.r == input.w){
      if(proc->killed){
        release(&input.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &input.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&input.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");
  initlock(&input.lock, "input");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  picenable(IRQ_KBD);
  ioapicenable(IRQ_KBD, 0);
}

