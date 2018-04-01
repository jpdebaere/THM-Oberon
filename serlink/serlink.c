/*
 * serlink.c -- serial line link support
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>


#define BLOCK_SIZE	512

#define LINE_SIZE	200
#define MAX_TOKENS	20

#define REQ		((unsigned char) 0x20)
#define REC		((unsigned char) 0x21)
#define SND		((unsigned char) 0x22)
#define ACK		((unsigned char) 0x10)
#define NAK		((unsigned char) 0x11)


static int sfd = -1;
static struct termios origOptions;
static struct termios currOptions;

static int run;


void serialClose(void);


void error(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Error: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  serialClose();
  exit(1);
}


void serialOpen(char *serialPort) {
  sfd = open(serialPort, O_RDWR | O_NOCTTY | O_NDELAY);
  if (sfd == -1) {
    error("cannot open serial port '%s'", serialPort);
  }
  tcgetattr(sfd, &origOptions);
  currOptions = origOptions;
  cfsetispeed(&currOptions, B9600);
  cfsetospeed(&currOptions, B9600);
  currOptions.c_cflag |= (CLOCAL | CREAD);
  currOptions.c_cflag &= ~PARENB;
  currOptions.c_cflag &= ~CSTOPB;
  currOptions.c_cflag &= ~CSIZE;
  currOptions.c_cflag |= CS8;
  currOptions.c_cflag &= ~CRTSCTS;
  currOptions.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG | IEXTEN);
  currOptions.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK);
  currOptions.c_iflag &= ~(INPCK | ISTRIP | INLCR | IGNCR | ICRNL);
  currOptions.c_iflag &= ~(IXON | IXOFF | IXANY);
  currOptions.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONOCR | ONLRET);
  tcsetattr(sfd, TCSANOW, &currOptions);
}


void serialClose(void) {
  if (sfd < 0) {
    return;
  }
  tcsetattr(sfd, TCSANOW, &origOptions);
  close(sfd);
  sfd = -1;
}


int serialSnd(unsigned char b) {
  int n;

  n = write(sfd, &b, 1);
  return n == 1;
}


int serialRcv(unsigned char *bp) {
  int n;

  n = read(sfd, bp, 1);
  return n == 1;
}


/**************************************************************/


unsigned char rcvByte(void) {
  unsigned char b;

  while (!serialRcv(&b)) ;
  return b;
}


int rcvInt(void) {
  int i;
  unsigned char b;

  i = 0;
  while (!serialRcv(&b)) ;
  i |= (unsigned int) b <<  0;
  while (!serialRcv(&b)) ;
  i |= (unsigned int) b <<  8;
  while (!serialRcv(&b)) ;
  i |= (unsigned int) b << 16;
  while (!serialRcv(&b)) ;
  i |= (unsigned int) b << 24;
  return i;
}


void sndByte(unsigned char b) {
  while (!serialSnd(b)) ;
}


void sndInt(unsigned int i) {
  while (!serialSnd((i >>  0) & 0xFF)) ;
  while (!serialSnd((i >>  8) & 0xFF)) ;
  while (!serialSnd((i >> 16) & 0xFF)) ;
  while (!serialSnd((i >> 24) & 0xFF)) ;
}


void sndStr(char *s) {
  while (*s != '\0') {
    sndByte(*s);
    s++;
  }
  sndByte(0);
}


/**************************************************************/


void sendBootFile(FILE *bootFile, unsigned int addr) {
  unsigned char buf[BLOCK_SIZE];
  int n, i;

  while (1) {
    n = fread(buf, 1, BLOCK_SIZE, bootFile);
    if (n < 0) {
      error("cannot read boot file");
    }
    if (n == 0) {
      break;
    }
    sndInt(n);
    sndInt(addr);
    for (i = 0; i < n; i++) {
      sndByte(buf[i]);
    }
    addr += n;
    if (n < BLOCK_SIZE) {
      break;
    }
  }
  sndInt(0);
}


/**************************************************************/


void getAndShowAnswer(void) {
  int run;
  unsigned char type;
  unsigned char b;
  int n;

  run = 1;
  while (run) {
    type = rcvByte();
    switch (type) {
      case 1:
        /* integer */
        n = rcvInt();
        printf("%d  ", n);
        break;
      case 2:
        /* hex */
        n = rcvInt();
        printf("0x%08X  ", n);
        break;
      case 3:
        /* real */
        printf("!!! real not yet\n");
        break;
      case 4:
        /* string */
        while (1) {
          b = rcvByte();
          if (b == 0) {
            break;
          }
          printf("%c", b);
        }
        printf("  ");
        break;
      case 5:
        /* bit */
        b = rcvByte();
        printf("%c", b);
        break;
      case 6:
        /* line */
        printf("\n");
        break;
      case 7:
        /* end */
        run = 0;
        break;
      default:
        printf("unknown type byte %u\n", (unsigned int) type);
    }
  }
  printf("\n");
}


/**************************************************************/


void ping(int argc, char *argv[]) {
  unsigned char b;

  sndByte(REQ);
  b = rcvByte();
  if (b == ACK) {
    printf("ACK\n");
  } else
  if (b == NAK) {
    printf("NAK\n");
  } else {
    printf("error: unknown answer\n");
  }
}


void h2o(int argc, char *argv[]) {
  char *name;
  FILE *file;
  unsigned char b;
  unsigned char buf[255];
  int n, i;

  name = argv[1];
  file = fopen(name, "r");
  if (file == NULL) {
    printf("error: cannot open file '%s' for read on host\n", name);
    return;
  }
  sndByte(REC);
  sndStr(name);
  b = rcvByte();
  if (b != ACK) {
    printf("error: no ACK for filename '%s' from Oberon system\n", name);
    fclose(file);
    return;
  }
  while (1) {
    n = fread(buf, 1, 255, file);
    if (n < 0) {
      error("cannot read local file");
    }
    if (n == 0) {
      sndByte(0);
      b = rcvByte();
      if (b != ACK) {
        printf("error: no ACK for file data from Oberon system\n");
        fclose(file);
        return;
      }
      break;
    }
    sndByte(n & 0xFF);
    for (i = 0; i < n; i++) {
      sndByte(buf[i]);
    }
    b = rcvByte();
    if (b != ACK) {
      printf("error: no ACK for file data from Oberon system\n");
      fclose(file);
      return;
    }
    if (n < 255) {
      break;
    }
  }
  b = rcvByte();
  if (b != ACK) {
    printf("error: no ACK for file '%s' from Oberon system\n", name);
  } else {
    printf("ACK for file '%s' from Oberon system\n", name);
  }
  fclose(file);
}


void o2h(int argc, char *argv[]) {
  char *name;
  FILE *file;
  unsigned char b;
  unsigned char buf[255];
  int n, i;

  name = argv[1];
  file = fopen(name, "w");
  if (file == NULL) {
    printf("error: cannot open file '%s' for write on host\n", name);
    return;
  }
  sndByte(SND);
  sndStr(name);
  b = rcvByte();
  if (b != ACK) {
    printf("error: no ACK for filename '%s' from Oberon system\n", name);
    fclose(file);
    return;
  }
  while (1) {
    n = rcvByte();
    if (n == 0) {
      sndByte(ACK);
      break;
    }
    for (i = 0; i < n; i++) {
      buf[i] = rcvByte();
    }
    if (fwrite(buf, 1, n, file) != n) {
      error("cannot write local file");
    }
    sndByte(ACK);
    if (n < 255) {
      break;
    }
  }
  printf("ACK for file '%s' sent to Oberon system\n", name);
  fclose(file);
}


void quit(int argc, char *argv[]) {
  run = 0;
}


/**************************************************************/


void mirror(int argc, char *argv[]) {
  int arg;
  char *endp;

  arg = strtol(argv[1], &endp, 10);
  if (*endp != '\0') {
    printf("error: cannot read integer argument\n");
    return;
  }
  sndInt(0);
  sndInt(arg);
  getAndShowAnswer();
}


void fill(int argc, char *argv[]) {
  int arg;
  char *endp;

  arg = strtol(argv[1], &endp, 0);
  if (*endp != '\0') {
    printf("error: cannot read integer argument\n");
    return;
  }
  sndInt(2);
  sndInt(arg);
  getAndShowAnswer();
}


void shfile(int argc, char *argv[]) {
  sndInt(4);
  sndStr(argv[1]);
  getAndShowAnswer();
}


void watch(int argc, char *argv[]) {
  sndInt(7);
  getAndShowAnswer();
}


void shmod(int argc, char *argv[]) {
  sndInt(10);
  getAndShowAnswer();
}


void shcmd(int argc, char *argv[]) {
  sndInt(11);
  sndStr(argv[1]);
  getAndShowAnswer();
}


void ldboot(int argc, char *argv[]) {
  sndInt(100);
  sndStr(argv[1]);
  getAndShowAnswer();
}


void clrdir(int argc, char *argv[]) {
  sndInt(101);
  getAndShowAnswer();
}


/**************************************************************/


typedef struct {
  char *name;
  int minArgc;
  void (*func)(int argc, char *argv[]);
} Cmd;


Cmd cmds[] = {
  { "p",        1, ping     },
  { "h2o",      2, h2o      },
  { "o2h",      2, o2h      },
  { "q",        1, quit     },
  /* --------------------- */
  { "mirror",   2, mirror   },
  { "fill",     2, fill     },
  { "shfile",   2, shfile   },
  { "watch",    1, watch    },
  { "shmod",    1, shmod    },
  { "shcmd",    2, shcmd    },
  { "ldboot",   2, ldboot   },
  { "clrdir",   1, clrdir   },
};


Cmd *lookupCmd(char *name) {
  int i;

  for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    if (strcmp(cmds[i].name, name) == 0) {
      return &cmds[i];
    }
  }
  return NULL;
}


/**************************************************************/


void usage(char *myself) {
  printf("Usage: %s -p <serial port> [-b <boot file>]\n", myself);
  exit(1);
}


int tokenize(char *line, char *tokens[], int maxTokens) {
  int n;
  char *p;

  n = 0;
  p = strtok(line, " \t\n\r");
  while (p != NULL) {
    if (n < maxTokens) {
      tokens[n++] = p;
    }
    p = strtok(NULL, " \t\n\r");
  }
  return n;
}


int main(int argc, char *argv[]) {
  int i;
  char *argp;
  char *serialPort;
  char *bootName;
  FILE *bootFile;
  unsigned char b;
  char line[LINE_SIZE];
  char *tokens[MAX_TOKENS];
  int n;
  Cmd *cmd;

  serialPort = NULL;
  bootName = NULL;
  for (i = 1; i < argc; i++) {
    argp = argv[i];
    if (*argp == '-') {
      /* option */
      if (strcmp(argp, "-p") == 0) {
        if (i == argc - 1 || serialPort != NULL) {
          usage(argv[0]);
        }
        i++;
        serialPort = argv[i];
      } else
      if (strcmp(argp, "-b") == 0) {
        if (i == argc - 1 || bootName != NULL) {
          usage(argv[0]);
        }
        i++;
        bootName = argv[i];
      } else {
        usage(argv[0]);
      }
    } else {
      usage(argv[0]);
    }
  }
  if (serialPort == NULL) {
    error("no serial port specified");
  }
  serialOpen(serialPort);
  while (serialRcv(&b)) ;
  if (bootName != NULL) {
    bootFile = fopen(bootName, "r");
    if (bootFile == NULL) {
      error("cannot open boot file '%s'", bootName);
    }
    sendBootFile(bootFile, 0);
    fclose(bootFile);
  }
  run = 1;
  while (run) {
    printf("\n");
    printf("Commands (Oberon0 and PCLink1):\n");
    printf("  p                   check if Oberon system is responding\n");
    printf("  h2o    <filename>   transfer <filename> from host to Oberon\n");
    printf("  o2h    <filename>   transfer <filename> from Oberon to host\n");
    printf("  q                   quit\n");
    printf("Commands (Oberon0 only):\n");
    printf("  mirror <integer>    mirror <integer> back\n");
    printf("  fill   <integer>    fill display with <integer>\n");
    printf("  shfile <filename>   show file <filename>\n");
    printf("  watch               watch\n");
    printf("  shmod               show modules\n");
    printf("  shcmd  <modname>    show commands for <modname>\n");
    printf("  ldboot <filename>   load boot area from <filename>\n");
    printf("  clrdir              clear directory\n");
    printf("cmd > ");
    fflush(stdout);
    if (fgets(line, LINE_SIZE, stdin) == NULL) {
      printf("\n");
      break;
    }
    n = tokenize(line, tokens, MAX_TOKENS);
    if (n == 0) {
      continue;
    }
    cmd = lookupCmd(tokens[0]);
    printf("\n");
    if (cmd == NULL) {
      printf("error: unknown command '%s'\n", tokens[0]);
      continue;
    }
    if (n < cmd->minArgc) {
      printf("error: too few arguments for command '%s'\n", tokens[0]);
      continue;
    }
    (*cmd->func)(n, tokens);
  }
  serialClose();
  return 0;
}
