#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef uint64_t cpuset;
#define cpubit(n) ((cpuset) 1 << (n))

static bool verbose;



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
