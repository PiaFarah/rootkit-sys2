# Tests

Run the local automated tests from the repository root:

```sh
python3 tests/run_tests.py
```

These tests cover the implemented features that can be checked without loading a kernel module:

- compile: the rootkit Makefile declares `wlkom.o` as an LKM and delegates `modules`/`clean` to the kernel build system;
- connection: `wlkom.c` contains the kthread, TCP socket, connect, receive, shutdown and retry logic expected for the reverse C2 connection;
- persistence: `rootkit/install_persistence.sh` installs the module under `/lib/modules`, prompts for the password, hashes it, writes `modprobe` parameters, creates a systemd service and enables it at boot;
- C2 build: the user-space C2 program builds;
- password/cipher: after each rootkit connection, the C2 prompts for a password, computes FNV-1a, sends an encrypted fixed-size `AUTH <fnv1a_hash>\n` frame, and the module validates it against its `module_param` password hash. The C2 also times out failed authentication attempts so a wrong password can be retried after the rootkit reconnects.

The actual `wlkom.ko` compilation and `insmod`/retry behavior still have to be tested inside the victim VM because they depend on the running kernel and its matching headers.
