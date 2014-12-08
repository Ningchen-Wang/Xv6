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
static int flag = 0;
static int g = 0;
struct history
{
	char cmds[MAX_HISTORY][MAX_CMD];
	int current;
    int record;
};
struct history his = {.current = 0, .record = 0};

int recordHistory(char* cmd)
{

	int i;
       
	for(i = 0; i < (sizeof(cmd) / sizeof(char)); i++)
	{
		his.cmds[his.current][i] = cmd[i];
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
//Test for history recording

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
  crt[pos] = ' ' | 0x0700;
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

char* match_commend(char* commendPrefix) {
    //consputc(strlen(commendPrefix)+'0');
    int i = 0;
    char* commendList[3];
    char **p = commendList;
    int flag = 0;
    char* ans = NULL;
    commendList[0] = "ls\0";
    commendList[1] = "cd\0";
    commendList[2] = NULL;
    while (*p != NULL) {
        i = 0;
        if (strlen(*p) < strlen(commendPrefix)) {p++; continue;}
        while (i < strlen(commendPrefix)) if ((*p)[i] == commendPrefix[i]) i++; else break;
        if (i >= strlen(commendPrefix)) {if (flag == 1) flag = 0; else {ans = *p; flag = 1;} }
        p++;
    }
    if (flag == 1) return ans; else return NULL;	
}

void
consoleintr(int (*getc)(void))
{
  int c;
  int i;

  acquire(&input.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      flag = 0;//history flag
      procdump();
      break;
    case '\t':
      flag = 0;//history flag
      //i = input.e;
      //input.buf[input.e++] = 'd';
      //consputc('d');
      /*while (i > input.w) {
          input.buf[input.e++ % INPUT_BUF] = input.buf[i-1];
          consputc(input.buf[i-1]);
          i--;
      } */
      if (input.e != input.w) {
          i = input.e - 1;
          while (input.buf[i % INPUT_BUF] != ' ' && i != input.w) i--;
          char commendPrefix[INPUT_BUF];
          int len = 0;
          while (i != input.e) commendPrefix[len++] = input.buf[i++ % INPUT_BUF];
          commendPrefix[len++] = '\0';
          char* match_ans;
          match_ans = match_commend(commendPrefix);
          if (match_ans != NULL) {
              i = len - 1;
              while (match_ans[i] != '\0') {
                  input.buf[input.e++ % INPUT_BUF] = match_ans[i];
                  consputc(match_ans[i++]);
              }
          } 
      }
      break;
    case C('U'):  // Kill line.
      flag = 0;//history flag
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
      }
      break;
    //case 0xE2: Test for up key
		case 0xE2:
	
            if(flag == 0)
            {
                g = his.current - 1;
                if (g < 0)
                   g = MAX_HISTORY-1;
                flag = 1;

            }
            else
            {
                
                if ( g  == his.current )
                   continue;
                g--;
                if (g < 0)
                   g = MAX_HISTORY-1;

                
            }
			if(his.record == 1)
			{
                while(input.e != input.w &&
                input.buf[(input.e-1) % INPUT_BUF] != '\n'){
                    input.e--;
                    consputc(BACKSPACE);
                }

                len = 0;
				int i;
                int f = g;

				for(i = 0; i < (sizeof(his.cmds[f]) / sizeof(char)); i++)
				{
                    if(his.cmds[f][i] == '\0') break;
					input.buf[input.e++ % INPUT_BUF] = his.cmds[f][i];
					consputc(his.cmds[f][i]);
                    str[len++] = his.cmds[f][i];
				}

			}
	  break;
    //case 0xE3: Test for down key
    case 0xE3:
			
            if(flag == 0)
            {
               continue;

            }
            else
            {
                int now;
                now = his.current - 1;
                if (now < 0)
                   now = MAX_HISTORY-1; 
                if ( g  == now )
                   continue;
                g++;
                if (g >= MAX_HISTORY)
                   g = 0;

                
            }
			if(his.record == 1)
			{
                while(input.e != input.w &&
                input.buf[(input.e-1) % INPUT_BUF] != '\n'){
                    input.e--;
                    consputc(BACKSPACE);
                }

                len = 0;
				int i;
                int f = g;

				for(i = 0; i < (sizeof(his.cmds[f]) / sizeof(char)); i++)
				{
                    if(his.cmds[f][i] == '\0') break;
					input.buf[input.e++ % INPUT_BUF] = his.cmds[f][i];
					consputc(his.cmds[f][i]);
                    str[len++] = his.cmds[f][i];
				}

			}
	  break; 
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
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        str[len++]=c;//Preparation for building a history record
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          flag = 0;
          input.w = input.e;
          str[len - 1] = '\0';//Add a '\0' in the end
          recordHistory(str);
          len=0;//reset len for the next recording preparation
          wakeup(&input.r);
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

