#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "record.h"

#define MAX_QUERY (NAME_LEN_MAX+1)
#define MAX_REPLY 11

void perror_die(const char *msg)
{
  perror(msg);
  exit(1);
}

// Parse dot-address and port number.
// If parser error, will exit(1).
void parse_addr(struct sockaddr_in *a, const char *dot, const char *port)
{
  memset(a, 0, sizeof(struct sockaddr_in));
  if (inet_pton(AF_INET, dot, &a->sin_addr) != 1) {
    fprintf(stderr, "%s is ot an IPv4 dot address.\n", dot);
    exit(1);
  }
  int p;
  if (sscanf(port, "%d", &p) != 1 || p < 1 || p > 65535) {
    fprintf(stderr, "%s is not a port number.\n", port);
    exit(1);
  }
  a->sin_port = htons((uint16_t)p);
  a->sin_family = AF_INET;
}

// socket(), connect(), fdopen("r"). If error, will exit(1).
void client_socket(const struct sockaddr_in *a, int *ps, FILE **pf)
{
  int s;
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1)
    perror_die("socket()");
  if (connect(s, (const struct sockaddr *)a, sizeof(struct sockaddr_in)) == -1)
    perror_die("connect()");
  FILE *f;
  f = fdopen(s, "r");
  if (f == NULL)
    perror_die("fdopen()");
  *ps = s;
  *pf = f;
}

// returns false for error, true for OK
int my_send(int s, const char *inp) {
  int n = strlen(inp);
  if (write(s, inp, n) == -1) {
    printf("Server bug: Server disconnected when I send\n");
    return 0;
  }
  return 1;
}

// returns false for error, true for OK
int my_recv(FILE *f) {
  char reply_buf[MAX_REPLY+1];
  if (fgets(reply_buf, MAX_REPLY+1, f) == NULL) {
    printf("Server bug: Server disconnected before replying\n");
    return 0;
  }
  int reply_len = strlen(reply_buf);
  if (reply_buf[reply_len-1] != '\n') {
    printf("Server bug: no newline in %d bytes.\n", MAX_REPLY);
    return 0;
  }
  printf("%s", reply_buf);
  return 1;
}

// returns false for error, true for OK
int dialogue(int s, FILE *f, const char *inp) {
  return my_send(s, inp) && my_recv(f);
}

// Test case: normal queries.
void normal(const struct sockaddr_in *addr) {
  int s;
  FILE *f;
  client_socket(addr, &s, &f);
  const char *queries[] = {
    "Ada Lovelace\n",
    "Hermione Granger\n",
    "Frodo Baggins\n",
    "Alan Turing\n",
    "Frodo Baggins\n"
  };
  for (int i = 0; i < sizeof(queries)/sizeof(const char *); i++) {
    if (! dialogue(s, f, queries[i]))
      break;
  }
  fclose(f);
}

// Test case: interleaving clients.
void duo(const struct sockaddr_in *addr) {
  int s[2];
  FILE *f[2];

  for (int j = 0; j < 2; j++) {
    client_socket(addr, s+j, f+j);
  }
  const char *queries[2][5] = {
    {"Victor Frankenstein\n",
     "Frodo Baggins\n",
     "Hermione Granger\n",
     "Alan Turing\n",
     "Frodo Baggins\n"
    },
    {"Frodo Baggins\n",
     "Victor Frankenstein\n",
     "Ada Lovelace\n",
     "Hermione Granger\n",
     "Dennis Ritchie\n"
    }
  };
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 2; j++) {
      if (! dialogue(s[j], f[j], queries[j][i]))
        break;
    }
  }
  for (int j = 0; j < 2; j++) {
    fclose(f[j]);
  }
}

// Test case: split message.
void split(const struct sockaddr_in *addr) {
  int s;
  FILE *f;
  client_socket(addr, &s, &f);
  const char *query[2] = {"Prof. Shriram K", "rishnamurthi I\n"};
  if (my_send(s, query[0])) {
    struct timespec timeout = {0, 5000000}; // 5 ms
    nanosleep(&timeout, NULL);
    if (my_send(s, query[1])) my_recv(f);
  }
  fclose(f);
}

// Test case: merged messages.
void merged(const struct sockaddr_in *addr) {
  int s;
  FILE *f;
  client_socket(addr, &s, &f);
  const char *queries = "Ada Lovelace\nFrodo Baggins\n";
  if (! my_send(s, queries)) return;
  if (! my_recv(f)) return;
  if (! my_recv(f)) return;
  fclose(f);
}

// Test case: 2nd message too long.
void too_long(const struct sockaddr_in *addr) {
  int s;
  FILE *f;
  client_socket(addr, &s, &f);
  const char *queries[2] = {
    "Prof. Shriram Krishnamurthi I\n",
    "Prof. Shriram Krishnamurthi II",
  };
  if (! dialogue(s, f, queries[0])) return;
  if (! my_send(s, queries[1])) return;
  char buf[MAX_REPLY+1];
  if (fgets(buf, MAX_REPLY+1, f) != NULL) {
    printf("Server bug: Server did not disconnect when it should. It sent %s\n",
           buf);
  } else {
    printf("Good news: server unlikes.\n");
  }
  fclose(f);
}

// Test case: connect-query-close loop.
void loyalty(const struct sockaddr_in *addr) {
  const char *query = "Victor Frankenstein\n";
  struct timespec timeout = {0, 100000000};  // 0.1 seconds
  int s;
  FILE *f;
  for (int i = 0; i < 15; i++) {
    client_socket(addr, &s, &f);
    nanosleep(&timeout, NULL);
    if (! dialogue(s, f, query)) break;
    fclose(f);
  }
}

struct {
  char *name;
  void (*func)(const struct sockaddr_in *);
} cases[] = {
  {"1", normal},
  {"2", duo},
  {"3", split},
  {"4", merged},
  {"5", too_long},
  {"6", loyalty}
};

int main(int argc, char **argv)
{
  if (argc < 4) {
    fprintf(stderr, "Need 3 arguments: dotaddress port case\n");
    exit(1);
  }

  struct sockaddr_in addr;
  parse_addr(&addr, argv[1], argv[2]);

  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 1024);

  const int n = sizeof(cases)/sizeof(cases[0]);
  int i;
  for (i = 0; i < n; i++) {
    if (strcmp(cases[i].name, argv[3]) == 0) {
      cases[i].func(&addr);
      break;
    }
  }
  if (i == n) {
    fprintf(stderr, "Invalid case\n");
    exit(1);
  }

  return 0;
}
