#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
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
      return -1;
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
      fprintf (stderr, "error isolating partition root\n");
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
      fprintf (stderr, "error releasing partition root\n");
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

static int irq (cpuset set) {
  char           buf[256], *str;
  DIR           *a;
  int            moved, skipped;
  struct dirent *b;

  moved = skipped = 0;

  format (set, buf, sizeof buf);
  str = strdup (buf);
  if (!str) {
    fprintf (stderr, "error allocating memory for cpuset\n");
    return -1;
  }

  snprintf (buf, sizeof buf, "%" PRIx64, set);
  if (spew ("/proc/irq/default_smp_affinity", buf)) {
    fprintf (stderr, "error setting default irq affinity\n");
    return -1;
  }

  if (!(a = opendir ("/proc/irq"))) {
    fprintf (stderr, "error opening /proc/irq\n");
    return -1;
  }

  while ((b = readdir (a))) {
    if (!isdigit ((unsigned char) b->d_name[0])) continue;

    snprintf (buf, sizeof buf, "/proc/irq/%s/smp_affinity_list", b->d_name);
    if (spew (buf, str)) {
      print ("error assigning cpuset %s to irq %s\n", str, b->d_name);
      skipped++;
    } else moved++;
  }
  closedir (a);

  print ("assigned cpuset %s to %d irqs (%d skipped)\n", str, moved, skipped);
  free (str);
  return 0;
}

static volatile sig_atomic_t done;

static void interrupt (int sig) {
  (void) sig;
  done = 1;
}

static int sample (cpuset all, cpuset iso, int *avg, int *max) {
  char                      buf[256];
  FILE                     *f;
  int                       a, b, cpu;
  static unsigned long long pb[CPU_MAX], pt[CPU_MAX];
  unsigned long long        c[8], busy, total, db, dt, sb, st;

  if (!(f = fopen ("/proc/stat", "r"))) return -1;
  *max = 0;
  sb = st = 0;

  while (fgets (buf, sizeof buf, f)) {
    if (strncmp (buf, "cpu", 3)) break;

    if (buf[3] == ' ') continue;
    if (sscanf (buf + 3, "%d %llu %llu %llu %llu %llu %llu %llu %llu", &cpu,
                &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7]) != 9 ||
        cpu < 0 || cpu >= CPU_MAX)
      continue;

    for (total = 0, a = 0; a < 8; a++)
      total += c[a];
    busy = total - c[3] - c[4];

    db = busy - pb[cpu];
    dt = total - pt[cpu];

    pb[cpu] = busy;
    pt[cpu] = total;

    if (!dt || dt == total) continue;
    b = 100 * db / dt;

    if (all & cpubit (cpu)) sb += db, st += dt;
    if (((all & ~iso) & cpubit (cpu)) && b > *max) *max = b;
  }

  *avg = st ? 100 * sb / st : 0;

  fclose (f);
  return 0;
}

static int psi (unsigned stall, unsigned window) {
  char buf[256];
  int  fd, a;

  if ((fd = open ("/proc/pressure/cpu", O_RDWR | O_NONBLOCK)) < 0) return -1;
  a = snprintf (buf, sizeof buf, "some %u %u", stall, window);

  if (write (fd, buf, a + 1) < 0) {
    close (fd);
    return -1;
  }

  return fd;
}

int main (int argc, char *argv[]) {
  bool   flag = false;
  char   buf[256], *isostr = NULL;
  cpuset isoset, allset;
  int    opt, pfd, avg, max, tick = 0;

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
  } else print ("parsed isolate cpuset: %" PRIx64 "\n", isoset);

  if (slurp ("/sys/devices/system/cpu/online", buf, 256)) {
    fprintf (stderr, "error reading online cpuset\n");
    return 1;
  }

  if (parse (buf, &allset)) {
    fprintf (stderr, "error parsing online cpuset: %s\n", buf);
    return 1;
  } else print ("parsed online cpuset: %" PRIx64 "\n", allset);

  if ((isoset & ~allset) || !(allset & ~isoset)) {
    fprintf (stderr, "invalid isolate cpuset\n");
    return 1;
  }

  if (geteuid ()) {
    fprintf (stderr, "must run as root\n");
    return 1;
  }

  struct sigaction sigact = {.sa_handler = interrupt};

  sigaction (SIGHUP, &sigact, NULL);
  sigaction (SIGINT, &sigact, NULL);
  sigaction (SIGQUIT, &sigact, NULL);
  sigaction (SIGTERM, &sigact, NULL);

  if (cgroup (CG_INIT, isostr)) return 1;

  if ((pfd = psi (2e5, 1e6)) < 0) {
    fprintf (stderr, "error creating psi trigger\n");
    goto done;
  }

  struct pollfd fd = {.fd = pfd, .events = POLLPRI};

  sample (allset, isoset, &avg, &max);

  while (!done) {
    fd.revents = 0;
    if (poll (&fd, flag ? 1 : 0, flag ? -1 : 1e3) < 0 || (fd.revents & POLLERR))
      break;

    if (flag) {
      if (!(fd.revents & POLLPRI)) continue;

      fprintf (stdout, "leaving after psi event triggered\n");
      flag = false;
      tick = 0;

      if (irq (allset) || cgroup (CG_RELEASE, NULL)) break;
      sample (allset, isoset, &avg, &max);
      continue;
    }

    sample (allset, isoset, &avg, &max);

    tick = avg < 5 ? tick + 1 : 0;
    print ("sampled load average %d%%\n", avg);
    if (tick < 5) continue;

    fprintf (stdout, "entering after system quiet for 5 ticks\n");
    flag = true;

    if (cgroup (CG_ISOLATE, NULL) || irq (allset & ~isoset)) break;
    poll (&fd, 1, 0);
  }
  close (pfd);

done:
  print ("normal process termination\n");
  irq (allset);
  cgroup (CG_REMOVE, NULL);
  return 0;
}
