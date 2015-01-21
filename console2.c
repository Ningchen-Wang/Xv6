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
#include "execvim.h"

//#include "user.h"

//Test for history recording
#define MAX_HISTORY 3
#define MAX_CMD 100
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

int firstVim = 1;
int startPos;
int endPos;

int checkVimStart(int pos)
{
  return 
  (pos >= 0) && (crt[pos] != (('$' & 0xff) |0x0700)
  || crt[pos+1]!= ((' ' & 0xff) |0x0700)
  || crt[pos+2]!= (('v' & 0xff) |0x0700)
  || crt[pos+3]!= (('i' & 0xff) |0x0700)
  || crt[pos+4]!= (('m' & 0xff) |0x0700)
  || crt[pos+5]!= ((' ' & 0xff) |0x0700));
}

void
consoleintr(int (*getc)(void))
{
  int c;
  int i;
  if (execvim == 1)
  {
     acquire(&input.lock);
     while((c = getc()) >= 0){
	int filePos;
	if (c != 0)
	{
	  outb(CRTPORT, 14);
	  filePos = inb(CRTPORT+1) << 8;
	  outb(CRTPORT, 15);
	  filePos |= inb(CRTPORT+1);
	
	  if (firstVim)
	  {
	  endPos =filePos;
	  if (endPos % 80 !=0)
		endPos =filePos +80 - filePos % 80;
	  for (startPos = filePos - filePos % 80; checkVimStart(startPos); startPos --);
	  if (startPos < 0)
		startPos = 0;
	  else
          	startPos += 80 - startPos % 80;
	  if (startPos == endPos)
	  {
	    int i;
	    for (i= startPos; i<startPos+80; i++)
		crt[i] = (' '&0xff) | 0x0700;
	    endPos += 80;
	  }
	  firstVim = 0;
	  filePos = startPos;
	  }
        }

     if (c != 0x1B && c!= 0)
     {
	switch (c){
        //up
	case 0xe2:
	  if (filePos >= startPos + 80)
		filePos -= 80;
          break;

	//down
        case 0xe3:
	  if (filePos < endPos - 80)
		filePos += 80;
          break;

	//left
        case 0xe4:
	  if (filePos > startPos )
		filePos--;
          break;
	
	//right
	case 0xe5:
	  if (filePos < endPos-1 )
		filePos++;
          break;
	
	case '\n':
	  if (endPos == 23*80 )
	  {
	    memmove(crt, crt+80, sizeof(crt[0])*(filePos/80)*80);
	    int i;
	    for (i = (filePos/80)*80; i< (filePos/80)*80 +80; i++)
		crt[i] = (' '&0xff) | 0x700;
	    startPos -= 80;
	  }
	  else 
	  {
	    filePos += 80 - filePos%80;
	    memmove(crt+filePos+80, crt+filePos, sizeof(crt[0])*(endPos-filePos));
	    int i;
            for (i = filePos; i<filePos+80; i++)
		crt[i] = (' '&0xff) | 0x700;
            endPos += 80; 
	  }
	  filePos -= filePos%80; 
          break;
	case C('H'):
	  if (filePos > startPos)
	  {
	    int i;
            int rmline = 1;

		if (crt[filePos] != ((' '&0xff) | 0x0700))
		{
		  rmline = 0;

		}
	    if (crt[filePos]  == ((' '&0xff) | 0x0700) && filePos % 80 ==0  && rmline)
	    {
		memmove(crt+filePos, crt+filePos+80, sizeof(crt[0])*(endPos-filePos-80));
		for (i = endPos -80; i< endPos; i++)
		  crt[i] = (' '&0xff) | 0x0700;
		endPos -= 80;
		while (crt[--filePos] == ((' '&0xff) | 0x0700) && filePos %80 != 0);
		if (crt[filePos] != ((' '&0xff) | 0x0700))
		  filePos ++;
	    }
	    else
	    {
		crt[--filePos] = ((' '&0xff) | 0x0700);
                for (i = filePos; i< endPos; i++)
		{
		  if (i % 80 ==79 && crt[i] == ((' '&0xff) | 0x0700))
			break;
		  crt[i] = crt[i+1];
		}
		if (filePos % 80 == 79){
		  while (crt[--filePos] == ((' '&0xff) | 0x0700) && filePos % 80 !=0);
		  if (filePos % 80 == 0 && crt[filePos] == ((' '&0xff) | 0x0700))
		  {
		    memmove(crt+filePos, crt+filePos+80, sizeof(crt[0])*(endPos-filePos-80));
		    for (i= endPos -80; i < endPos; i++)
			crt[i] = ((' '&0xff) | 0x0700);
		    endPos -= 80;
		  }
		  else
		    filePos++;
		}
	    }
          }
	  break;
	
	default:
	  i = filePos + 79 - filePos % 80;
	  while (crt[i] != ((' '&0xff) | 0x0700))
	    i += 80;
          if (i < endPos)
	  {
	    for (; i > filePos; i--)
              crt[i] = crt[i-1];
	  }
	  else
          {
	    i = filePos + 80 - filePos % 80;
	    memmove(crt+i+80, crt+i, sizeof(crt[0])*(endPos-i));
	    int j;
	    for (j = i; j < i + 80; j++)
		crt[j] = (' '&0xff) | 0x0700;
	    for (; i > filePos; i--)
		crt[i] = crt[i-1];
	    endPos += 80;
	  }
	  crt[filePos ++] = (c&0xff) | 0x0700;
	  break;
	}
	if ((endPos/80) >= 24){
	  memmove(crt, crt+80, sizeof(crt[0])*80*23);
	  startPos -= 80;
	  endPos -= 80;
	  filePos -= 80;
	  int i;
	  for (i = endPos; i< endPos +80; i++)
	    crt[i] = (' '&0xff) | 0x0700;
	}
	
	outb(CRTPORT, 14);
	outb(CRTPORT+1, filePos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, filePos);

	}

	else if (c != 0)
	{
	  firstVim = 1;
	  int i ,j;
	  memset(content, 0, sizeof(char)*2000);
	  for (i = startPos, j=0; i< endPos; i++)
	  {
	    if (crt[i] != ((' '&0xff) | 0x0700))
	    {
		content[j++] = crt[i] & 0x00ff;
	    }
	    else
	    {
	      int k;
	      for (k= i+1; (k%80) != 0 && crt[k] == ((' '&0xff) | 0x0700); k++);
	      if (k % 80 == 0)
	      {
		content[j++] = '\n';
		i = k - 1;
	      }
	      else
	      {
		for (; crt[i] == ((' '&0xff) | 0x0700); i++)
		  content[j++] = ' ';
		i--;
	      }
	    }
	  }
	    content[j++] = 0;
	    outb(CRTPORT, 14);
	    outb(CRTPORT+1, endPos >> 8);
	    outb(CRTPORT, 15);
	    outb(CRTPORT+1, endPos);
	    execvim = 0;
	}
    }
    release(&input.lock);
    return;	
  }
  else
  {
  acquire(&input.lock);
  while((c = getc()) >= 0){
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
          i = input.e - 1;
          while (input.buf[i % INPUT_BUF] != ' ' && i != input.w) i--;
          char commendPrefix[INPUT_BUF];
          int length = 0;
          while (i != input.e) commendPrefix[length++] = input.buf[i++ % INPUT_BUF];
          commendPrefix[length++] = '\0';
          char* match_ans;
          match_ans = match_commend(commendPrefix);
          if (match_ans != NULL) {
              i = length - 1;
              while (match_ans[i] != '\0') {
                  input.buf[input.e++ % INPUT_BUF] = match_ans[i];
                  str[len++] = match_ans[i];
                  consputc(match_ans[i++]);
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

