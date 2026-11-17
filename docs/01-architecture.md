# Architecture: AMD SVM research hypervisor

## 1. AMD SVM in one page

AMD-V (SVM) virtualizes a CPU through the **VMCB** (Virtual Machine Control Block), a 4 KiB
page split into a *control area* (intercepts, guest ASID, nested paging config) and a
*state-save area* (guest register state). The host uses a small set of instructions:

- `VMRUN rax` — `rax` = physical address of the guest VMCB. Transfers control to the guest
  until an intercepted event causes a `#VMEXIT`, which returns to the instruction after
  `VMRUN`.
- `VMSAVE` / `VMLOAD` — save/load the subset of host or guest state that `VMRUN` does not
  (FS/GS/TR/LDTR bases, SYSENTER/STAR MSRs, etc.).
- `VMMCALL` — guest-to-hypervisor hypercall.
- `STGI` / `CLGI` — set/clear global interrupt flag around the critical VMRUN region.

SVM is enabled by setting `EFER.SVME (bit 12)`, after confirming support via
`CPUID Fn8000_0001.ECX[2]` (SVM) and that it is not disabled by
`VM_CR MSR (0xC0010114)` bit 4 (SVMDIS), with `CPUID Fn8000_000A` reporting NPT support in
`EDX[0]`.

### Nested paging (NPT)

With NPT enabled (`VMCB.NP_ENABLE`), guest physical addresses are translated by a second set
of page tables (nCR3) that the hypervisor controls. This is AMD's SLAT. A translation fault
in this layer is a **Nested Page Fault (`#VMEXIT` code 0x400, NPF)**. NPT is what a memory
hypervisor uses to trap/redirect guest physical memory access — and it is exactly the
mechanism HVCI uses one level up.

## 2. Where we sit relative to VBS

```
            +-------------------------------------------------------+
  VTL1      |  Secure Kernel (securekernel.exe), SKCI/HVCI policy    |   higher privilege
 (root)     |  owns the REAL NPT/SLAT for VTL0, enforces W^X on krnl |
            +-------------------------------------------------------+
  L0 (root) |  Microsoft Hyper-V (hvax64.sys) — Type-1 root hv       |
            +-------------------------------------------------------+
  VTL0      |  Normal Windows: ntoskrnl, drivers, THIS PROJECT      |   lower privilege
            |  If we start SVM here we are NESTED (L2 under Hyper-V) |
            +-------------------------------------------------------+
```

Key consequence: our NPT is a *nested* NPT. The physical addresses we think we are mapping
are themselves guest-physical addresses that Hyper-V re-translates through the real SLAT.
We can police our own guest, but we cannot see or override the protections Hyper-V/SKCI
enforce for VTL0. This is the boundary the project measures.

## 3. Driver object model

- `DriverEntry` → create control device `\\Device\\hvresearch`, symlink `\\DosDevices\\hvresearch`.
- IOCTLs (see `driver/inc/ioctl.h`): PROBE, VIRTUALIZE, DEVIRTUALIZE, GET_STATS, RUN_EXPERIMENT.
- Per-CPU `VIRTUAL_PROCESSOR_DATA` holds the guest VMCB, host VMCB, host state-save area,
  host stack, and MSR permission map. All allocated as contiguous, non-paged, page-aligned.

## 4. Control / data flow for bring-up

1. IOCTL VIRTUALIZE → run `DpcForEachProcessor` so each logical CPU self-virtualizes at
   `IRQL = DISPATCH_LEVEL` on its own core.
2. Per CPU: check support (detect.c), build VMCB + NPT, capture current context,
   `VMSAVE` guest, then `VMRUN` loop in `svm.asm`.
3. On first `VMRUN` the guest resumes exactly after the context capture point, now running
   under the hypervisor. The very next intercepted event (we intercept CPUID) produces the
   first `#VMEXIT`, handled in `exit.c`.
