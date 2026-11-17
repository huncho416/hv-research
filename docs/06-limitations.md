# Limitations, correctness caveats, and honest scoping

## This code has not been silicon-validated in this repo

The SVM core (VMCB build, the VMRUN loop, the orderly teardown in `svm.asm`) is
written to the AMD64 manual and mirrors the structure of well-known public
research hypervisors, but it has **not** been compiled with a WDK or run on
hardware as part of this repository's authoring. Before relying on it:

- Build with the WDK and fix any environment-specific warnings (the project uses
  `TreatWarningAsError`).
- Bring it up first with **VBS/HVCI OFF** on the research VM, attached to a
  kernel debugger, and confirm the first CPUID `#VMEXIT` and a clean
  devirtualize before trying the nested (VBS-on) configuration.
- Validate the `svm.asm` stack-offset contract (the 6-pointer host-stack block
  and the `+0x60/0x68/0x70/0x80` offsets in the teardown path) against the
  compiled `svm.h` layout. These are the most error-prone lines.

## Nested-under-Hyper-V is not guaranteed

With VBS on, bring-up only works if the root hypervisor exposes AMD nested
virtualization to this partition. If it does not, E1 will fail - that is a
valid, documented result, not a bug to force around.

## Deliberately out of scope

- No driver-signing bypass. No method to load on a fully locked clean machine.
- No real HVCI/VBS bypass, no SLAT subversion, no attack on VTL1/Secure Kernel.
- No targeting of third-party/anti-cheat software. The subject under study is
  Windows' own protection behavior.
- The NPT split-hook (E4) is documented as a mechanism, not weaponized against
  protected code.

## Known simplifications in this build

- MSR intercepts are disabled (MSRPM zeroed); the MSR exit path is a stub. A
  production study would intercept EFER to observe/guard SVME.
- The identity NPT uses 2 MiB leaves and maps a flat 512 GiB; it does not model
  memory holes or MMIO typing beyond PAT defaults.
- RIP advance relies on NRIPS (present on current Zen parts); the fallback uses
  fixed instruction lengths only for the handful of intercepts we arm.
- E2/E3 use kernel `MmCopyMemory`/`MmMapIoSpaceEx` as the physical-memory view.
  This is the safe analogue of what the hypervisor's identity NPT sees; it keeps
  the experiment observational rather than routing every access through a custom
  VMEXIT path.

## Safety

Expect bugchecks, especially E3 under HVCI. Only run on a disposable research VM
with a snapshot to roll back to. Keep a kernel debugger attached to capture the
exact failure for the writeup.
