#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool verbose;
#define print(...)                                                             \
  do {                                                                         \
    if (verbose) fprintf (stderr, __VA_ARGS__);                                \
  } while (false)

typedef uint64_t cpuset;
#define MAXCPU    64
#define cpubit(n) ((cpuset) 1 << (n))

static void usage (FILE *out, const char *name) {
  fprintf (out,
           "Usage: %s [options...]\n"
           "\n"
           " -h            Display this help message and quit.\n"
           " -i <cpulist>  A list of power demanding cpu to isolate.\n"
           " -v            Output more verbose information.\n"
           "\n"
           "Copyright (C) 2026 Jing Huang.\n",
           name);
}

static int num (const char **str, unsigned long *res) {
  if (!isdigit ((unsigned char) **str)) return -1;

  char *end;
  *res = strtoul (*str, &end, 10);
  if (*res >= MAXCPU) return -1;

  *str = end;
  return 0;
}

static int parse (const char *str, cpuset *set) {
  unsigned long a, b;
  cpuset        c = 0;

  for (;;) {
    if (num (&str, &a)) return -1;
    b = a;
    if (*str == '-') {
      str++;
      if (num (&str, &b) || b < a) return -1;
    }
    for (; a <= b; a++)
      c |= cpubit (a);

    if (*str != ',') break;
    str++;
  }

  if (*str == '\n') str++;
  if (*str) return -1;

  *set = c;
  return 0;
}

static int format (const cpuset set, char *buf, size_t len) {
  int    a, b;
  size_t c = 0;

  buf[0] = '\0';
  for (a = 0; a < MAXCPU; a++) {
    if (!(set & cpubit (a))) continue;
    for (b = a; b < MAXCPU - 1 && (set & cpubit (b + 1)); b++)
      ;

    if (a == b) c += snprintf (buf + c, len - c, "%s%d", c ? "," : "", a);
    else c += snprintf (buf + c, len - c, "%s%d-%d", c ? "," : "", a, b);
    if (c >= len) return -1;

    a = b;
  }

  return 0;
}

int main (int argc, char *argv[]) {
  char *isolist = NULL;
  int   opt;

  while (~(opt = getopt (argc, argv, "hi:v"))) {
    switch (opt) {
    case 'h':
      usage (stdout, *argv);
      return 0;
    case 'i':
      isolist = optarg;
      break;
    case 'v':
      verbose = true;
      break;
    default:
      goto fail;
    }
  }

  if (optind != argc || !isolist) {
  fail:
    usage (stderr, *argv);
    return 1;
  }
}
