# Experiment protocol (certificate evidence)

Each experiment is run twice: **Config A (VBS/HVCI ON)** and **Config B (VBS/HVCI OFF)**.
Record the machine configuration block for every run.

## Configuration block (fill in per run)

```
Date/time:
CPU (model, Zen gen):
Windows build (winver):
Secure Boot:            on/off
VBS running:            (msinfo32 -> "Virtualization-based security")
HVCI / Memory Integrity: on/off
Hyper-V feature:        on/off
Nested virt exposed:    yes/no/unknown
Driver signing policy:  test-signing / debug / signed  (record exactly how loaded)
Kernel debugger:        attached/not
```

## E0 — Environment probe (no virtualization)
Run IOCTL PROBE. Expect: SVM supported, NPT supported, `SVMDIS` state, Hyper-V present bit
(`CPUID 0x40000000` vendor = "Microsoft Hv"), VBS indicators. Save `probe.json`.

**Hypothesis:** With VBS on, `CPUID 0x40000000` reports a Hyper-V, and attempting
`EFER.SVME=1` / `VMRUN` from VTL0 behaves differently than with VBS off.

## E1 — SVM bring-up
Run IOCTL VIRTUALIZE. Expect first CPUID `#VMEXIT` logged. Record success/failure per CPU.

**Config A expectation:** either (a) nested SVM is exposed and bring-up succeeds under
Hyper-V, or (b) `VMRUN` is denied — both are valid, documented outcomes.
**Config B expectation:** bring-up succeeds on bare metal.

## E2 — Read a known kernel symbol via the hypervisor
Ask the hypervisor to read N bytes at the guest-physical address backing a known kernel VA
(e.g. start of `ntoskrnl` .text). Compare to what a normal ring-0 read returns.

**Expected:** reads match in both configs (reading is not what HVCI restricts).

## E3 — Attempt a write to an executable kernel page
Ask the hypervisor to write a 1-byte patch to a read-only executable kernel page, then read
it back both via the hypervisor and via normal ring-0, and check `MiReadWriteVirtualMemory`
result / bugcheck behavior.

**Config A expectation (headline result):** the write to executable code does not take effect
as live code; HVCI's real SLAT enforces W^X regardless of our nested NPT. Expect no durable
code modification, and/or a CI bugcheck (e.g. `0x3B`/`KERNEL_SECURITY_CHECK` family) — record
exactly what happens.
**Config B expectation:** write succeeds (no HVCI), demonstrating the delta.

## E4 — NPT split hook demonstration (mechanics only)
Split the nested NPT so the hooked page returns different bytes for exec vs read. Show the
mechanism works on *our own* guest page, then show it does **not** yield a bypass of HVCI on
real VTL0 kernel code (the real SLAT is not ours to split).

## E5 — Boundary report
Generate the comparison table (A vs B) for E2–E4 and write the explanation. This is the
core deliverable.

## Safety / hygiene
- Only run on the dedicated research VM. Expect bugchecks; keep a snapshot to roll back.
- Attach a kernel debugger for E3/E4 so you can capture the exact failure.
- Never run against third-party protected software; the target is Windows itself.
