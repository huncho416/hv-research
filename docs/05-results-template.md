# Results template (fill in per run, then compare A vs B)

Paste the `captures/vm-config.txt` block for each run above its table.

## Config A - VBS/HVCI ON

| Exp | Operation | Outcome | NTSTATUS | Notes / observed behavior |
|-----|-----------|---------|----------|---------------------------|
| E0  | probe | | | hypervisor vendor, VBS likely, already-virtualized |
| E1  | virtualize all CPUs | | | succeeded nested? or VMRUN withheld? |
| E2  | read kernel .text via hv view | | | bytes match normal ring-0 read? |
| E3  | write 0x90 to exec page | | | persisted? faulted? bugcheck code? |
| E4  | NPT split-hook demo | | | documented boundary |

## Config B - VBS/HVCI OFF (baseline/control)

| Exp | Operation | Outcome | NTSTATUS | Notes / observed behavior |
|-----|-----------|---------|----------|---------------------------|
| E0  | probe | | | |
| E1  | virtualize all CPUs | | | bare-metal bring-up |
| E2  | read kernel .text via hv view | | | |
| E3  | write 0x90 to exec page | | | expected: write persists |
| E4  | NPT split-hook demo | | | |

## The delta (this is the certificate's conclusion)

Summarize, in your own words, grounded in docs/02:

1. **Reads** behaved identically in A and B because reading guest-physical
   memory is not what HVCI restricts; ring-0 already had that.
2. **Writes to executable kernel code** differed: in B the write persisted; in A
   it was blocked/faulted because the ROOT hypervisor's SLAT (not our nested
   NPT) enforces W^X for VTL0, and VTL1/SKCI owns code-integrity policy.
3. Therefore a VTL0-launched hypervisor gains no new power over HVCI-protected
   code: VBS moved the security boundary above where our hypervisor can reach.

Attach: the two transcripts, the two config blocks, and (if any) the E3 bugcheck
minidump analysis (`!analyze -v`).
