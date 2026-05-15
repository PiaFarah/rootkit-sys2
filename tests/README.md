# Tests

Run the local automated tests from the repository root:

```sh
python3 tests/run_tests.py
```

These tests cover the implemented features that can be checked without loading a kernel module:

- compile: the rootkit Makefile declares `wlkom.o` as an LKM and delegates `modules`/`clean` to the kernel build system;
- connection: `wlkom.c` contains the kthread, TCP socket, connect, receive, shutdown and retry logic expected for the reverse C2 connection;
- persistence: `rootkit/install_persistence.sh` installs the module under `/lib/modules`, writes `modprobe` parameters, creates a systemd service and enables it at boot;
- C2 build: the user-space C2 program builds;
- password: the C2 requires a password, computes FNV-1a and sends `AUTH <fnv1a_hash>\n`, and the module validates the `AUTH` line against its `module_param` password hash.

The actual `wlkom.ko` compilation and `insmod`/retry behavior still have to be tested inside the victim VM because they depend on the running kernel and its matching headers.
