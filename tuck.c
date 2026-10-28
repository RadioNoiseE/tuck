#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static bool verbose;
#define print(...)                                                             \
  do {                                                                         \
    if (verbose) fprintf (stderr, __VA_ARGS__);                                \
  } while (false)

typedef uint64_t cpuset;
#define CPU_MAX   64
#define cpubit(n) ((cpuset) 1 << (n))

static int number (const char **str, unsigned long *res) {
  if (!isdigit ((unsigned char) **str)) return -1;

  char *end;
  *res = strtoul (*str, &end, 10);
  if (*res >= CPU_MAX) return -1;

  *str = end;
  return 0;
}

static int parse (const char *str, cpuset *set) {
  unsigned long a, b;
  cpuset        c = 0;

  for (;;) {
    if (number (&str, &a)) return -1;
    b = a;
    if (*str == '-') {
      str++;
      if (number (&str, &b) || b < a) return -1;
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
  for (a = 0; a < CPU_MAX; a++) {
    if (!(set & cpubit (a))) continue;
    for (b = a; b < CPU_MAX - 1 && (set & cpubit (b + 1)); b++)
      ;

    if (a == b) c += snprintf (buf + c, len - c, "%s%d", c ? "," : "", a);
    else c += snprintf (buf + c, len - c, "%s%d-%d", c ? "," : "", a, b);

    if (c >= len) return -1;
    a = b;
  }

  return 0;
}

static int slurp (const char *path, char *buf, int len) {
  int fd = open (path, O_RDONLY);
  if (fd < 0) return -1;

  ssize_t n = read (fd, buf, len - 1);
  close (fd);

  if (n < 0) return -1;
  return buf[n] = '\0';
}

static int spew (const char *path, const char *str) {
  int fd = open (path, O_WRONLY);
  if (fd < 0) return -1;

  ssize_t n = write (fd, str, strlen (str));
  close (fd);

  return n < 0 ? -1 : 0;
}

enum { CG_INIT, CG_ISOLATE, CG_RELEASE, CG_REMOVE };

static int cgroup (int op, const char *str) {
  char buf[64];

  switch (op) {
  case CG_INIT:
    if (spew ("/sys/fs/cgroup/cgroup.subtree_control", "+cpuset")) {
      fprintf (stderr, "error enabling cpuset controller\n");
      return -1;
    }
    if (mkdir ("/sys/fs/cgroup/tuck", 0755) && errno != EEXIST) {
      fprintf (stderr, "error creating control group\n");
    }
    if (spew ("/sys/fs/cgroup/tuck/cpuset.cpus.partition", "member") ||
        spew ("/sys/fs/cgroup/tuck/cpuset.cpus", str)) {
      fprintf (stderr, "error initializing control group\n");
      return -1;
    }
    print ("initialized control group\n");
    return 0;

  case CG_ISOLATE:
    if (spew ("/sys/fs/cgroup/tuck/cpuset.cpus.partition", "isolated")) {
      fprintf (stderr, "error isolating parition root\n");
      return -1;
    }
    if (slurp ("/sys/fs/cgroup/tuck/cpuset.cpus.partition", buf, sizeof buf) ||
        strcmp (buf, "isolated\n")) {
      fprintf (stderr, "invalid isolated partition root\n");
      spew ("/sys/fs/cgroup/tuck/cpuset.cpus.partition", "member");
      return -1;
    }
    print ("isolated partition root\n");
    return 0;

  case CG_RELEASE:
    if (spew ("/sys/fs/cgroup/tuck/cpuset.cpus.partition", "member")) {
      fprintf (stderr, "error releasing parition root\n");
      return -1;
    }
    print ("released partition root\n");
    return 0;

  case CG_REMOVE:
    if (rmdir ("/sys/fs/cgroup/tuck") && errno != ENOENT) {
      fprintf (stderr, "error removing control group\n");
      return -1;
    }
    print ("removed control group\n");
    return 0;
  }

  return -1;
}

int main (int argc, char *argv[]) {
  char   buf[256];
  char  *isostr = NULL;
  cpuset isoset, allset;
  int    opt;

  while (~(opt = getopt (argc, argv, "hi:v"))) {
    switch (opt) {
    case 'h':
      usage (stdout, *argv);
      return 0;
    case 'i':
      isostr = optarg;
      break;
    case 'v':
      verbose = true;
      break;
    default:
      goto fail;
    }
  }

  if (optind != argc || !isostr) {
  fail:
    usage (stderr, *argv);
    return 1;
  }

  if (parse (isostr, &isoset)) {
    fprintf (stderr, "error parsing isolate cpuset: %s\n", isostr);
    return 1;
  } else print ("parsed isolate cpuset: %ld\n", isoset);

  if (slurp ("/sys/devices/system/cpu/online", buf, 256)) {
    fprintf (stderr, "error reading online cpuset\n");
    return 1;
  }

  if (parse (buf, &allset)) {
    fprintf (stderr, "error parsing online cpuset: %s\n", buf);
    return 1;
  } else print ("parsed online cpuset: %ld\n", allset);

  if ((isoset & ~allset) || !(allset & ~isoset)) {
    fprintf (stderr, "invalid isolate cpuset\n");
    return 1;
  }

  if (geteuid ()) {
    fprintf (stderr, "must hold root privileges\n");
    return 1;
  }
}
