# Operating Systems Labs

C code from the labs of Operating Systems 1 and 2 at MiNI, Warsaw University of Technology. The programs use the POSIX API, from file handling and processes up to threads, shared memory and network sockets.

This is coursework written for learning, much of it under time pressure, not a finished product. Course materials are available on the [course website](https://sop.mini.pw.edu.pl/).

## Repository structure

Folders are named after the lab they belong to, and the prefix says what kind of code is inside:

- `l*t/` (**tutorials**) – code written while working through the tutorials on the course website, as preparation for the graded labs,
- `w*/` (**workshops**) – code from the workshops held at the faculty a week before each graded lab, also as preparation. Most folders include the task sheet as a PDF in Polish,
- `l*p/` (**graded labs**) – code from the graded labs, written in the lab room against the clock.

### Operating Systems 1

| Lab | Topic                                                                 | Tutorial | Workshop           | Graded lab               |
| --- | --------------------------------------------------------------------- | -------- | ------------------ | ------------------------ |
| 0   | C basics: arguments, input/output, environment variables              | `l0t`    | –                  | –                        |
| 1   | Files and directories, `nftw`, `getopt`, symlinks                     | `l1t`    | `w1` (`sop-grep`)  | `l1p` (`sop-bib`)        |
| 2   | Processes and signals, low-level file I/O                             | `l2t`    | `w2` (`sop-admin`) | `l2p` (`sop-collection`) |
| 3   | Threads, mutexes, thread cancellation, signal handling with `sigwait` | `l3t`    | `w3` (`sop-salis`) | `l3p` (`sop-risk`)       |
| 4   | Thread synchronization: semaphores, condition variables, barriers     | `l4t`    | –                  | `l4p` (`sop-husaria`)    |

### Operating Systems 2

| Lab | Topic                                                                      | Tutorial          | Workshop                              | Graded lab               |
| --- | -------------------------------------------------------------------------- | ----------------- | ------------------------------------- | ------------------------ |
| 5   | Pipes and FIFOs                                                            | `l5t`, `l5t/ants` | `w5` (`sop-roncevaux`)                | `l5p` (`sop-expedition`) |
| 6   | Shared memory, `mmap`, process-shared and robust mutexes, named semaphores | –                 | `w6` (`sop-shop`)                     | `l6p` (`ops-booking`)    |
| 7   | Stream sockets and `epoll`                                                 | –                 | `w7` (`sop-hre`, `werona/sop-werona`) | `l7p` (`sop-mcast`)      |
| 8   | UDP sockets combined with worker threads                                   | –                 | `w8` (`sop-mag`)                      | `l8p` (`sop-eden`)       |

Notes:

- `w2` and `w3` contain only the starter code, without a solution.
- Some folders include sample input data, e.g. `l1p/library`, `l3p/maps`, `l5p/exp*.txt`, `l5t/ants/colony*.txt` and `w5/*.txt`.
- `l8p/ops/test.sh` sends test UDP messages to a running server, and the shell scripts in `w7/werona` are test clients.

## Requirements

- Linux: the code relies on Linux and glibc features such as `epoll` and `TEMP_FAILURE_RETRY`, so it will not build on macOS or Windows,
- `gcc` and `make`, with AddressSanitizer and UndefinedBehaviorSanitizer available, as most Makefiles enable them. `w6` uses `-std=c23`, so it needs a recent compiler.

## Building and running

Each folder is built separately. In most of them, `make` builds everything:

```bash
cd l4p
make
./sop-husaria 10 2
```

The Makefiles in `l3t`, `l4t`, `l5t` and `w5` only set the compiler flags, so name the program you want to build:

```bash
cd l3t
make zad1
```

Most programs print their expected arguments when started without them.

---

**Author:** Krzysztof Wójtowicz<br>
**Course:** Operating Systems 1 & 2 (Systemy Operacyjne) @ MiNI WUT
