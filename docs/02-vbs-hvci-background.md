# Background: VBS, HVCI, VTLs, and why read/write is the interesting question

## Virtualization-Based Security (VBS)

VBS uses the hypervisor to create an isolated, higher-privilege execution environment. On a
modern Win11 box the boot chain is: UEFI (Secure Boot) → Hyper-V loads first as the root
hypervisor → it creates **Virtual Trust Levels (VTLs)**. VTL0 is the normal world (ntoskrnl,
drivers, apps). VTL1 is the secure world (securekernel.exe, isolated LSA, etc.). VTL1 is
*more* privileged than VTL0 even though both are “guest” relative to the hypervisor.

## HVCI (Memory Integrity)

Hypervisor-Enforced Code Integrity uses SLAT (NPT on AMD) controlled by the root hypervisor
to enforce **W^X** on VTL0 kernel memory: a physical page can be writable or executable, but
not both, and executable kernel pages must correspond to signed, verified code. The crucial
architectural point for this project:

- The page-table permissions that decide “can this physical page be written / executed” live
  in the **root hypervisor's SLAT**, managed by VTL1 (SKCI).
- VTL0 cannot edit those entries. Not the VTL0 kernel, and not a hypervisor that the VTL0
  kernel launches (which is nested *below* VTL1).

## Why “read/write” is the right lens

Most offensive kernel tooling ultimately wants arbitrary **kernel read/write**. The research
question is precisely: *with VBS/HVCI on, what does a VTL0-launched hypervisor actually gain?*
Expected, documentable findings:

1. **Reads** of ordinary VTL0 memory: still possible (the hypervisor sees guest physical
   memory). Nothing magic — ring-0 could already read most of this.
2. **Writes to executable kernel code pages**: *blocked at the real SLAT by HVCI*. Even if
   our nested NPT marks a page writable, Hyper-V's SLAT still enforces W^X for VTL0, so the
   write either faults or is simply not reflected as executable code. This is the headline.
3. **Hooking via NPT split (exec/read separation)**: our nested NPT can trap our *own*
   guest, but cannot relax HVCI for the real VTL0 kernel. We can demonstrate the technique
   mechanically while showing it does not translate into a real HVCI bypass.
4. **VTL1 / secure memory**: entirely invisible/untouchable from our position.

## What this does NOT provide

This project deliberately stops at *measurement*. It does not attempt a real HVCI bypass,
does not target any product, and does not weaponize NPT hooking against protected code. The
value is the documented boundary and the explanation of the privilege model.

## References to cite in the writeup

- AMD64 Architecture Programmer's Manual, Vol. 2, Ch. 15 (Secure Virtual Machine).
- Microsoft: “Virtualization-based Security (VBS)” and “Hypervisor-protected Code Integrity”
  documentation; “Hypervisor Top-Level Functional Specification (TLFS).”
- Satoshi Tanda, SimpleSvm / SimpleSvmHook writeups.
- Windows Internals, 7th ed., Part 2 (VTLs, Secure Kernel).
