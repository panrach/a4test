#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "record.h"

#define MAX_QUERY (NAME_LEN_MAX+1)
#define MAX_REPLY 11

void perror_die(const char *msg)
{
  perror(msg);
  exit(1);
}

void ignore_sig(int sig)
{
  struct sigaction a;
  a.sa_handler = SIG_IGN;
  a.sa_flags = 0;
  sigaction(sig, &a, NULL);
}

// Parse dot-address and port number.
// If parser error, will exit(1).
void parse_addr(struct sockaddr_in *a, const char *port)
{
  memset(a, 0, sizeof(struct sockaddr_in));
  a->sin_addr.s_addr = htonl(INADDR_ANY);
  int p;
  if (sscanf(port, "%d", &p) != 1 || p < 1 || p > 65535) {
    fprintf(stderr, "%s is not a port number.\n", port);
    exit(1);
  }
  a->sin_port = htons((uint16_t)p);
  a->sin_family = AF_INET;
}

// Create socket and call bind() and listen(). If error, will exit(1).
int server_socket(const struct sockaddr_in *a)
{
  int s;
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1)
    perror_die("socket()");
  int one = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
    perror("setsockopt(SO_REUSEADDR, true)");
  }
  if (bind(s, (const struct sockaddr *)a, sizeof(struct sockaddr_in)) == -1)
    perror_die("bind()");
  if (listen(s, 5) == -1)
    perror_die("listen()");
  return s;
}

// I'm hardcoding data, the struct doesn't need to follow any file format.
typedef struct tuple {
  unsigned points;
  char name[NAME_LEN_MAX+1];   // nul-terminated
} tuple;

tuple my_records[] = {
  { 49365,  "Hermione Granger" },
  // The following maxes out name length and reply length
  { 2488897010, "Jonathan Edward Peter Chapman" },
  { 19129, "Victor Frankenstein" },
  { 177,  "Frodo Baggins" },
  { 64265, "Ada Lovelace" }
};

int get_points(const char *name, unsigned *answer)
{
  for (int i = 0; i < sizeof(my_records)/sizeof(my_records[0]); i++) {
    if (strcmp(name, my_records[i].name) == 0) {
      *answer = my_records[i].points;
      return 1;
    }
  }
  return 0;
}

// Parameters: client socket.
void do_client(int c)
{
  FILE *f = fdopen(c, "r");
  if (f == NULL)
    perror_die("fdopen()");

  char namebuf[MAX_QUERY+1];   // +1 for fgets adding \0
  char replybuf[MAX_REPLY+1];  // +1 for snprintf adding \0
  int replylen;
  unsigned answer;
  for (;;) {
    if (fgets(namebuf, MAX_QUERY+1, f) == NULL)
      break;
    // Find \n and replace it by \0 for get_points().
    // If \n not found, bad client.
    char *nl = memchr(namebuf, '\n', MAX_QUERY);
    if (nl == NULL)
      break;
    *nl = '\0';
    if (strcmp(namebuf, "Bond, James Bond") == 0) {
      // split reply
      const char *reply = "3178689\n";
      int len = strlen(reply);
      int mid = len / 2;
      if (write(c, reply, mid) == -1)
        break;
      // wait 5 milliseconds before sending the rest
      struct timespec timeout = {0, 5000000};
      nanosleep(&timeout, NULL);
      if (write(c, reply + mid, len - mid) == -1)
        break;
    } else if (strcmp(namebuf, "Integer Overflow") == 0) {
      // reply is 11 bytes but no newline
      if (write(c, "18457062978", 11) == -1)
        break;
    } else if (strcmp(namebuf, "Age of Vampires") == 0) {
      // reply unlimited bytes but no newline
      char buf[1024];
      for (int i = 0; i < 1024/16; i++) {
        memcpy(buf + i*16, "3041579826978832", 16);
      }
      while (write(c, buf, 1024) != -1);
      break;
    } else if (strcmp(namebuf, "Terminator") == 0) {
      // no reply, disconnect
      break;
    } else {
      // normal reply
      if (get_points(namebuf, &answer)) {
        snprintf(replybuf, MAX_REPLY+1, "%u\n", answer);
        replylen = strlen(replybuf);
      } else {
        memcpy(replybuf, "none\n", 5);
        replylen = 5;
      }
      if (write(c, replybuf, replylen) == -1)
        break;
    }
  }

  fclose(f);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "Need 1 argument: port\n");
    exit(1);
  }

  struct sockaddr_in addr;
  parse_addr(&addr, argv[1]);

  int s = server_socket(&addr);

  ignore_sig(SIGCHLD);
  // Then no zombies to wait() for. Although, Linux-specific. >:)

  int c;

  for (;;) {
    c = accept(s, NULL, NULL);
    if (c == -1) continue;
    pid_t p = fork();
    if (p == -1)
      perror_die("fork()");
    else if (p != 0) {
      close(c);
    }
    else {
      close(s);
      do_client(c);
      return 0;
    }
  }
}
