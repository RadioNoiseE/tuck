## About

tuck is an attempt to utilize the 4 Darkmont LP E-cores on Intel
Panther Lake (Core Ultra Series 3) processors to save power and extend
battery life.  It is inspired by intel-lpmd(8) which frustrated the
author with its unnecessarily complexity.

tuck works by confining all tasks and movable interrupts to a few LP
E-cores while the system is in idle, while stepping back when the
system is more performance demanding.  For detailed information of how
tuck works, please consult the manpage tuck(8).

tuck is designed to work on Intel Panther Lake processors, although it
is believed to also be useful to all mobile hybrid processors,
including Apple Silicon M-series processors.

## Installation

tuck only depends on the C POSIX library, and is implemented in
approximately 300 lines of C that compiles cleanly.

```sh
clang -Wall -Wextra -pedantic -O2 -flto=thin tuck.c -o tuck
```

Then you can install the binary alongside its manpage to, i.e.,
`/usr/local`.

## Requirements

There are a few runtime requirements, namely:

* the unified cgroup v2 hierarchy mounted at `/sys/fs/cgroup` with the
  cpuset controller available,
* a kernel with isolated cpuset partitions and pressure stall
  information (`CONFIG_PSI`),
* no other program managing CPU or interrupt affinity, such as
  irqbalance(1) or intel_lpmd(8).

Most (GNU/)Linux distributions should satisfy all of the above.

## Usage

For tuck to work, you only need to specify the list of CPUs that you
want to disable when the system is in idle.  On a laptop whose CPUs 12
to 15 are the LP E-cores, switch off everything else while idle:

```sh
sudo tuck -i 0-11
```

On Intel Panther Lake processors the LP E-cores are the CPUs without a
level 3 cache; these are the CPUs missing from

```sh
ls -d /sys/devices/system/cpu/cpu*/cache/index3
```

To see what tuck is doing, use the verbose `-v` flag.  Before
reporting any issues, please make sure you've read the manpage.

## Contributions

Before opening a pull request, please open an issue first.  Feel free
to open issues for discussions, share benchmarks, etc.  Hybrid cores
are effective, while being hard to use right.
