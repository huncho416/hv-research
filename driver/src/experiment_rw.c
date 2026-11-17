/*
 * experiment_rw.c - The measurement harness (E2/E3/E4).
 *
 * These experiments quantify what a VTL0-launched hypervisor can actually do
 * to kernel memory with VBS/HVCI on vs off. They are intentionally OBSERVER
 * experiments: they attempt an operation and RECORD the outcome. They do not
 * attempt to defeat HVCI, and they operate only on Windows' own memory on a
 * dedicated research VM.
 *
 * Important honesty note for the writeup: a classic ring-0 write via
 * MmCopyMemory already bypasses nothing special; the interesting part is the
 * DELTA between HVCI-on and HVCI-off, and the fault/outcome you observe when
 * touching an executable kernel page under HVCI.
 */
#include "svm.h"
#include "log.h"
#include <ntstrsafe.h>

#define EXP_MAX_LEN 64

static NTSTATUS HvrReadPhysicalView(UINT64 va, PUINT8 buf, UINT32 len, UINT64* outPa)
{
    PHYSICAL_ADDRESS pa;
    MM_COPY_ADDRESS src;
    SIZE_T done = 0;
    NTSTATUS status;

    pa = MmGetPhysicalAddress((PVOID)va);
    *outPa = (UINT64)pa.QuadPart;
    if (pa.QuadPart == 0) {
        return STATUS_INVALID_ADDRESS;
    }

    /* Read through the physical-memory view (what the hypervisor's identity
     * NPT also sees). MmCopyMemory with PHYSICAL is the safe kernel analogue. */
    src.PhysicalAddress = pa;
    status = MmCopyMemory(buf, src, len, MM_COPY_MEMORY_PHYSICAL, &done);
    return status;
}

static NTSTATUS HvrExpReadKernel(PHVR_EXPERIMENT_REQUEST req, PHVR_EXPERIMENT_RESULT res)
{
    UINT32 len = req->Length;
    UINT64 pa = 0;
    NTSTATUS status;

    if (len == 0 || len > EXP_MAX_LEN) {
        len = EXP_MAX_LEN;
    }
    status = HvrReadPhysicalView(req->TargetVa, res->Before, len, &pa);
    res->TargetPa = pa;
    res->BytesReturned = NT_SUCCESS(status) ? len : 0;
    if (NT_SUCCESS(status)) {
        res->Outcome = HVR_OUTCOME_SUCCEEDED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "read %u bytes @VA 0x%llx (PA 0x%llx) via physical view", len, req->TargetVa, pa);
    } else {
        res->Outcome = HVR_OUTCOME_FAULTED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "read failed 0x%08x @VA 0x%llx", status, req->TargetVa);
    }
    return status;
}

static NTSTATUS HvrExpWriteExecPage(PHVR_EXPERIMENT_REQUEST req, PHVR_EXPERIMENT_RESULT res)
{
    UINT64 pa = 0;
    UINT8 before = 0, after = 0;
    PHYSICAL_ADDRESS phys;
    PVOID mapped;
    NTSTATUS status;

    /* Capture the current byte via the physical view. */
    status = HvrReadPhysicalView(req->TargetVa, &before, 1, &pa);
    res->TargetPa = pa;
    res->Before[0] = before;
    if (!NT_SUCCESS(status)) {
        res->Outcome = HVR_OUTCOME_INVALID_TARGET;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "could not resolve/read target 0x%llx (0x%08x)", req->TargetVa, status);
        return status;
    }

    /*
     * Attempt the write by mapping the physical page and storing a byte. Under
     * HVCI, the physical page backing executable kernel code is enforced
     * read-only/no-write-execute by the ROOT hypervisor's SLAT, which we (a
     * nested VTL0 hypervisor) do not control. Expected outcomes:
     *   - the mapping store faults / bugchecks (CI catches the write), or
     *   - the store appears to succeed on our alias but is not reflected as
     *     live executable code, or
     *   - with HVCI OFF, the write simply succeeds (the control/baseline).
     * We wrap the store so we can record a fault rather than crash silently.
     */
    phys.QuadPart = (LONGLONG)pa;
    mapped = MmMapIoSpaceEx(phys, PAGE_SIZE, PAGE_READWRITE);
    if (mapped == NULL) {
        res->Outcome = HVR_OUTCOME_BLOCKED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "MmMapIoSpaceEx(RW) returned NULL for PA 0x%llx - mapping refused", pa);
        return STATUS_ACCESS_DENIED;
    }

    __try {
        UINT32 off = (UINT32)(pa & (PAGE_SIZE - 1));
        volatile UINT8* p = (volatile UINT8*)mapped + off;
        *p = req->PatchByte;             /* <-- the operation under study */
        KeInvalidateAllCaches();
        after = *p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        res->Outcome = HVR_OUTCOME_FAULTED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "write to exec page PA 0x%llx raised 0x%08x (CI likely blocked it)",
            pa, GetExceptionCode());
        MmUnmapIoSpace(mapped, PAGE_SIZE);
        return STATUS_ACCESS_VIOLATION;
    }
    MmUnmapIoSpace(mapped, PAGE_SIZE);

    /* Read back through the independent physical view to see if the live code
     * actually changed. */
    HvrReadPhysicalView(req->TargetVa, &res->After[0], 1, &pa);

    if (res->After[0] == req->PatchByte) {
        res->Outcome = HVR_OUTCOME_SUCCEEDED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "write TOOK EFFECT (before=0x%02x after=0x%02x) - HVCI not enforcing here",
            before, res->After[0]);
    } else {
        res->Outcome = HVR_OUTCOME_BLOCKED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "write did NOT persist (before=0x%02x alias=0x%02x readback=0x%02x) - protected",
            before, after, res->After[0]);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS HvrExpNptHookDemo(PHVR_EXPERIMENT_REQUEST req, PHVR_EXPERIMENT_RESULT res)
{
    UNREFERENCED_PARAMETER(req);
    /*
     * The NPT split-hook mechanism (separate exec vs read mappings of the same
     * guest-physical page) is implemented at the NPT layer. On this build the
     * demo documents the boundary rather than performing a live split, because
     * on a VBS system our nested NPT cannot relax the root hypervisor's SLAT
     * for real VTL0 kernel code. See docs/02 for the explanation to cite.
     */
    res->Outcome = HVR_OUTCOME_BLOCKED;
    RtlStringCbPrintfA(res->Note, sizeof(res->Note),
        "NPT split-hook is a nested-NPT mechanic; it cannot override the root "
        "SLAT/HVCI for VTL0 code. Documented, not performed against protected code.");
    return STATUS_SUCCESS;
}

NTSTATUS HvrRunExperiment(_In_ PHVR_EXPERIMENT_REQUEST req, _Out_ PHVR_EXPERIMENT_RESULT res)
{
    HVR_EXPERIMENT_REQUEST local = *req; /* buffered IOCTL: in/out share buffer */
    NTSTATUS status;

    RtlZeroMemory(res, sizeof(*res));
    res->StructSize = sizeof(*res);
    res->ExperimentId = local.ExperimentId;

    if (g_Hvr.Virtualized == 0) {
        res->Outcome = HVR_OUTCOME_NOT_VIRTUALIZED;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note),
            "hypervisor not active - run IOCTL_HVR_VIRTUALIZE first");
        res->NtStatus = STATUS_DEVICE_NOT_READY;
        return STATUS_SUCCESS; /* report via struct, not IOCTL failure */
    }

    switch (local.ExperimentId) {
    case HVR_EXP_READ_KERNEL:
        status = HvrExpReadKernel(&local, res);
        break;
    case HVR_EXP_WRITE_EXEC_PAGE:
        status = HvrExpWriteExecPage(&local, res);
        break;
    case HVR_EXP_NPT_HOOK_DEMO:
        status = HvrExpNptHookDemo(&local, res);
        break;
    default:
        res->Outcome = HVR_OUTCOME_UNKNOWN;
        RtlStringCbPrintfA(res->Note, sizeof(res->Note), "unknown experiment %u",
                           local.ExperimentId);
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    res->NtStatus = (UINT32)status;
    HVR_INFO("experiment %u -> outcome=%u status=0x%08x: %s",
             local.ExperimentId, res->Outcome, status, res->Note);
    return STATUS_SUCCESS;
}
