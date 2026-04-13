# OS-Jackfruit — Multi-Container Runtime in C

> **Platform**: Ubuntu 22.04 / 24.04 LTS VM (VirtualBox / VMware / KVM — NOT WSL)  
> **Team Size**: 2 Students

---

## 1. Team Information

| Role | Name | SRN |
|------|------|-----|
| Member 1 | _Your Name_ | _Your SRN_ |
| Member 2 | _Partner Name_ | _Partner SRN_ |

---

## 2. Build, Load, and Run Instructions

### Phase 0 — Environment Setup

#### Step 0.1 — VM Requirements

- **Hypervisor**: VirtualBox, VMware, or KVM — **NOT WSL**
- **OS**: Ubuntu 22.04 LTS or 24.04 LTS
- **RAM**: ≥ 2 GB
- **Disk**: ≥ 20 GB
- **CRITICAL**: Disable Secure Boot in VM BIOS/UEFI settings before booting

> Secure Boot must be OFF because the kernel module (`monitor.ko`) is unsigned.
> In VirtualBox: Settings → System → Motherboard → uncheck "Enable EFI (special OSes only)" or go to Settings → System → Motherboard → Secure Boot → Disabled.

#### Step 0.2 — Install Dependencies

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

#### Step 0.3 — Fork and Clone

```bash
# Fork https://github.com/shivangjhalani/OS-Jackfruit on GitHub first
git clone https://github.com/<your-username>/OS-Jackfruit.git
cd OS-Jackfruit
```

#### Step 0.4 — Run Preflight Check

```bash
cd boilerplate
chmod +x environment-check.sh
sudo ./environment-check.sh
```

Fix every `[FAIL]` before continuing.

#### Step 0.5 — Prepare Alpine Root Filesystem

```bash
mkdir rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base

# Make per-container copies
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
```

#### Step 0.6 — Build the Boilerplate

```bash
cd boilerplate && make
```

A clean build produces `engine`, `monitor.ko`, `cpu_hog`, `io_pulse`, and `memory_hog`.

### Load and Run (Phase 4+)

```bash
# Load kernel module
sudo insmod monitor.ko
ls -l /dev/container_monitor   # verify device exists
dmesg | tail                   # check init messages

# Start supervisor
sudo ./engine supervisor ./rootfs-base

# In another terminal — CLI commands
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine ps
sudo ./engine logs alpha
sudo ./engine stop alpha

# Unload module when done
sudo rmmod monitor
```

---

## 3. Demo with Screenshots

> _Add all 8 required screenshots here with captions after completing Phase 6._

| # | Screenshot | Caption |
|---|-----------|---------|
| 1 | _(attach)_ | Multi-container supervision — supervisor + 2 container children |
| 2 | _(attach)_ | `engine ps` metadata table |
| 3 | _(attach)_ | Bounded-buffer log file contents |
| 4 | _(attach)_ | `engine stop alpha` — CLI + IPC |
| 5 | _(attach)_ | `dmesg` soft-limit warning |
| 6 | _(attach)_ | `dmesg` hard-limit kill + `engine ps` state='killed' |
| 7 | _(attach)_ | Scheduling experiment timing output |
| 8 | _(attach)_ | Clean teardown — no zombies |

---

## 4. Engineering Analysis

### 4.1 Isolation Mechanisms

Linux namespaces provide the kernel-level isolation used by each container:

- **CLONE_NEWPID**: Container sees itself as PID 1; host PIDs are invisible inside.
- **CLONE_NEWUTS**: Container has its own hostname, isolated from the host.
- **CLONE_NEWNS**: Container has its own mount namespace; mounts inside don't affect the host.

`chroot(path)` changes the root directory for the container process, restricting filesystem visibility to `rootfs-alpha/`. The host kernel is still shared — the container uses the same kernel, same system calls, same scheduler. Only the namespace views are isolated.

### 4.2 Supervisor & Process Lifecycle

A long-running supervisor parent is essential for:
- Reaping child processes via `waitpid()` in a `SIGCHLD` handler (prevents zombies)
- Maintaining container metadata (state, PIDs, log paths)
- Accepting CLI commands over a UNIX socket

Process lifecycle: `fork()` → child calls `unshare()` → `chroot()` → `mount()` → `execv()`. Parent stores child PID, registers with kernel module, spawns logging threads.

### 4.3 IPC, Threads & Synchronization

Two IPC mechanisms:
1. **Pipe** (per container): captures container stdout/stderr into the supervisor's ring buffer
2. **UNIX domain socket** (`/tmp/engine.sock`): CLI clients send commands to the supervisor

The ring buffer uses `pthread_mutex_t` to protect `head`/`tail`/`count` and `pthread_cond_t` (`not_full`, `not_empty`) for producer-consumer coordination. Without the mutex, two threads could simultaneously read `count < RING_SIZE`, both write, and one would overwrite the other (TOCTOU race).

### 4.4 Memory Management & Enforcement

RSS (Resident Set Size) measures physical pages currently in RAM — not virtual memory (`total_vm`). Soft limits trigger a warning log; hard limits send `SIGKILL` from kernel space. Enforcement belongs in the kernel because user-space cannot reliably observe and act on another process's memory before it allocates more.

### 4.5 Scheduling Behaviour

> _Fill in with experiment results from Phase 5._

CFS (Completely Fair Scheduler) tracks `vruntime` per task. A `nice 19` task accumulates vruntime faster relative to CPU time received, so it gets ~5–10× less CPU than a `nice 0` task under equal contention. I/O-bound tasks benefit from shorter sleep periods resetting their vruntime deficit, giving them CPU quickly when they wake.

---

## 5. Design Decisions and Tradeoffs

| Subsystem | Decision | Tradeoff |
|-----------|----------|----------|
| Container isolation | `unshare()` in child vs `clone()` with flags | `unshare()` is simpler; `clone()` allows more control but more complex |
| IPC control | UNIX domain socket | Simpler than TCP; no network stack overhead; local only |
| Logging | Ring buffer + producer/consumer threads | Bounded memory use; slight latency vs direct write |
| Memory enforcement | Kernel module kthread polling every 1s | Simple; 1s granularity may miss brief spikes |
| Container metadata | Fixed-size array | Predictable memory; limits max containers to `MAX_CONTAINERS` |

---

## 6. Scheduler Experiment Results

> _Fill in after completing Phase 5._

### Experiment A: CPU Priority (nice 0 vs nice 19)

| Container | Nice Value | Workload | Wall-Clock Time |
|-----------|-----------|----------|----------------|
| alpha | 0 | cpu_hog | ___ s |
| beta | 19 | cpu_hog | ___ s |

**Analysis**: _Explain CFS vruntime difference here._

### Experiment B: CPU-bound vs I/O-bound

| Container | Workload | Avg CPU% | Completion Time |
|-----------|----------|----------|----------------|
| alpha | cpu_hog | ___% | ___ s |
| beta | io_pulse | ___% | ___ s |

**Analysis**: _Explain I/O interactivity bonus here._

---

## Verification Checklist

- [ ] `make` builds with zero errors and zero warnings
- [ ] `sudo insmod monitor.ko` succeeds, `/dev/container_monitor` exists
- [ ] Supervisor starts and accepts CLI commands
- [ ] Two containers run simultaneously
- [ ] `engine ps` shows correct metadata
- [ ] Log files exist at `/tmp/engine-*.log`
- [ ] `memory_hog` triggers soft-limit warning in `dmesg`
- [ ] `memory_hog` triggers hard-limit kill, state shows `killed`
- [ ] Scheduling experiment tables recorded
- [ ] Clean shutdown: no zombies, module unloads cleanly
- [ ] All 8 screenshots taken
- [ ] `rootfs-base/` and `rootfs-*/` in `.gitignore`
- [ ] Repository pushed to fork

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `[FAIL] WSL detected` | Use VirtualBox/VMware/KVM — not WSL |
| `[FAIL] Secure Boot enabled` | Disable Secure Boot in VM BIOS settings, reboot |
| `[FAIL] Kernel headers missing` | `sudo apt install linux-headers-$(uname -r)` |
| `wget` fails on Alpine CDN | Check network; verify URL is current at dl-cdn.alpinelinux.org |
| `make` fails: `No rule to make target` | Run `make clean && make`; check `uname -r` matches headers |
| `Invalid module format` on `insmod` | `make clean && make` after any kernel header change |
| Zombie processes | Ensure `waitpid(-1, &st, WNOHANG)` loops in `SIGCHLD` handler |
| `/tmp/engine.sock` not found | Start supervisor before running CLI commands |
