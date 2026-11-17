# Roadmap / build order

This project is built in layers. Each layer is independently testable so you can capture
evidence for the certificate at every stage.

| Stage | Component | File(s) | What it proves |
|-------|-----------|---------|----------------|
| 0 | Environment probe | `driver/src/detect.c` | Are we on AMD SVM? Is Hyper-V present? Is VBS/HVCI on? Are we already virtualized? |
| 1 | SVM bring-up | `driver/src/svm.c`, `driver/src/svm.asm` | We can enter SVM guest mode and take a `#VMEXIT` (CPUID) and resume |
| 2 | NPT identity map | `driver/src/npt.c` | We build a 1:1 nested page table and run the guest through it |
| 3 | VMEXIT instrumentation | `driver/src/exit.c` | We log CPUID/MSR/NPF exits with counts and timings |
| 4 | Memory R/W experiment | `driver/src/experiment_rw.c` | We attempt NPT-based read/write/hook of a target page and record the outcome |
| 5 | VBS boundary report | `tools/` + `driver/src/detect.c` | We correlate (ON vs OFF) what the hypervisor can and cannot observe/modify |

## Nested-virtualization caveat (AMD + Hyper-V)

When VBS is on, `VMRUN` from VTL0 only works if Hyper-V exposes AMD nested virtualization to
the partition your research VM runs in. If nested SVM is not exposed, stage 1 will fail at
`EFER.SVME` or `VMRUN` with `#GP`/`#VMEXIT(INVALID)` — **that failure is itself a result**:
it demonstrates that the root hypervisor is withholding the virtualization extension from
guests. Record it rather than trying to force it.

## Definition of done for the certificate

1. A reproducible log showing SVM bring-up succeeding (VBS off) and the exact failure/behavior
   with VBS on.
2. A side-by-side table of what memory the VTL0 hypervisor could read/write in each config.
3. A written explanation (docs/02) of *why* the results differ, grounded in the VTL/SLAT model.
