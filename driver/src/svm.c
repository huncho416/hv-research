/*
 * svm.c - AMD SVM virtualization bring-up and teardown.
 *
 * Flow (per logical CPU, at DISPATCH_LEVEL via KeGenericCallDpc):
 *   1. RtlCaptureContext snapshots the current register state.
 *   2. If our hypervisor is already installed (CPUID signature), this is the
 *      "second pass" after VMRUN resumed the guest -> mark virtualized, return.
 *   3. Otherwise build the VMCB from the captured context + current system
 *      descriptors, enable EFER.SVME, seed host/guest extra state via VMSAVE,
 *      and jump into the VMRUN loop (HvrLaunchVm). The guest resumes exactly
 *      at step 2, now running under our hypervisor.
 *
 * Nested-under-Hyper-V note: on a VBS system EFER.SVME / VMRUN only succeed if
 * the root hypervisor exposes AMD nested virtualization to this partition. If
 * it does not, HvrLaunchVm will fault/triple-fault the guest; that outcome is a
 * documented experimental result (see docs/00-roadmap.md).
 */
#include "svm.h"
#include "npt.h"
#include "log.h"
#include <intrin.h>

#define MSR_FS_BASE        0xC0000100
#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102
#define MSR_PAT            0x00000277
#define MSRPM_SIZE         (2 * PAGE_SIZE)

#pragma pack(push, 1)
typedef struct _SEG_DESCRIPTOR
{
    UINT16 LimitLow;
    UINT16 BaseLow;
    UINT8  BaseMid;
    UINT8  Attr0;              /* type:4, s:1, dpl:2, p:1              */
    UINT8  LimitHighAttr1;     /* limit[19:16]:4, avl:1, l:1, db:1, g:1 */
    UINT8  BaseHigh;
    UINT32 BaseUpper;          /* only valid for 16-byte system descriptors */
    UINT32 MustBeZero;
} SEG_DESCRIPTOR, *PSEG_DESCRIPTOR;

typedef struct _PSEUDO_DESCRIPTOR
{
    UINT16 Limit;
    UINT64 Base;
} PSEUDO_DESCRIPTOR, *PPSEUDO_DESCRIPTOR;
#pragma pack(pop)

static UINT16 HvrSegAttr(UINT16 selector, ULONG_PTR gdtBase)
{
    PSEG_DESCRIPTOR d;
    SEGMENT_ATTRIBUTE a;

    if ((selector & ~0x7u) == 0) {
        return 0; /* null selector */
    }
    d = (PSEG_DESCRIPTOR)(gdtBase + (selector & ~0x7u));
    a.AsUInt16 = 0;
    a.Fields.Type = d->Attr0 & 0x0F;
    a.Fields.System = (d->Attr0 >> 4) & 0x01;
    a.Fields.Dpl = (d->Attr0 >> 5) & 0x03;
    a.Fields.Present = (d->Attr0 >> 7) & 0x01;
    a.Fields.Avl = (d->LimitHighAttr1 >> 4) & 0x01;
    a.Fields.LongMode = (d->LimitHighAttr1 >> 5) & 0x01;
    a.Fields.DefaultBit = (d->LimitHighAttr1 >> 6) & 0x01;
    a.Fields.Granularity = (d->LimitHighAttr1 >> 7) & 0x01;
    return a.AsUInt16;
}

static UINT64 HvrSegBase(UINT16 selector, ULONG_PTR gdtBase)
{
    PSEG_DESCRIPTOR d;
    UINT64 base;

    if ((selector & ~0x7u) == 0) {
        return 0;
    }
    d = (PSEG_DESCRIPTOR)(gdtBase + (selector & ~0x7u));
    base = (UINT64)d->BaseLow |
           ((UINT64)d->BaseMid << 16) |
           ((UINT64)d->BaseHigh << 24);
    /* System segments (TSS/LDT) use a 16-byte descriptor with an upper base. */
    if (((d->Attr0 >> 4) & 0x01) == 0) {
        base |= ((UINT64)d->BaseUpper << 32);
    }
    return base;
}

static void HvrSetupVmcb(PVIRTUAL_PROCESSOR_DATA vp, PCONTEXT ctx)
{
    PVMCB_CONTROL_AREA control = &vp->GuestVmcb.ControlArea;
    PVMCB_STATE_SAVE_AREA state = &vp->GuestVmcb.StateSaveArea;
    PSEUDO_DESCRIPTOR gdtr, idtr;
    ULONG_PTR gdtBase;
    PHYSICAL_ADDRESS msrpmPa;
    PSEUDO_DESCRIPTOR gdtrLocal = {0};
    PSEUDO_DESCRIPTOR idtrLocal = {0};

    HvrGetGdtBase(&gdtrLocal);
    __sidt(&idtrLocal);
    gdtr = &gdtrLocal;
    idtr = &idtrLocal;
    gdtBase = (ULONG_PTR)gdtr->Base;

    /* ---- Control area: intercept CPUID, enable nested paging ---- */
    control->InterceptMisc1 = SVM_INTERCEPT_MISC1_CPUID;
    control->GuestAsid = 1;
    control->NpEnable = 1;  /* bit 0 = NP_ENABLE */
    control->NCr3 = (UINT64)vp->SharedVpData->Pml4Pa.QuadPart;

    msrpmPa = MmGetPhysicalAddress(vp->SharedVpData->MsrPermissionMap);
    control->MsrpmBasePa = (UINT64)msrpmPa.QuadPart;
    control->IopmBasePa = 0; /* no I/O intercepts */

    /* ---- State-save area: clone the current (host) state ---- */
    state->Gdtr.Base = gdtr->Base;  state->GdtrLimit = gdtr->Limit;
    state->Idtr.Base = idtr->Base;  state->IdtrLimit = idtr->Limit;

    state->CsSelector = HvrGetCs();  state->CsAttrib = HvrSegAttr(state->CsSelector, gdtBase);
    state->SsSelector = HvrGetSs();  state->SsAttrib = HvrSegAttr(state->SsSelector, gdtBase);
    state->DsSelector = HvrGetDs();  state->DsAttrib = HvrSegAttr(state->DsSelector, gdtBase);
    state->EsSelector = HvrGetEs();  state->EsAttrib = HvrSegAttr(state->EsSelector, gdtBase);
    state->FsSelector = HvrGetFs();  state->FsAttrib = HvrSegAttr(state->FsSelector, gdtBase);
    state->GsSelector = HvrGetGs();  state->GsAttrib = HvrSegAttr(state->GsSelector, gdtBase);
    state->TrSelector = HvrGetTr();  state->TrAttrib = HvrSegAttr(state->TrSelector, gdtBase);
    state->LdtrSelector = HvrGetLdtr(); state->LdtrAttrib = HvrSegAttr(state->LdtrSelector, gdtBase);

    state->CsBase = 0; state->SsBase = 0; state->DsBase = 0; state->EsBase = 0;
    state->FsBase = __readmsr(MSR_FS_BASE);
    state->GsBase = __readmsr(MSR_GS_BASE);
    state->KernelGsBase = __readmsr(MSR_KERNEL_GS_BASE);
    state->TrBase = HvrSegBase(state->TrSelector, gdtBase);
    state->LdtrBase = HvrSegBase(state->LdtrSelector, gdtBase);

    state->Efer = __readmsr(IA32_MSR_EFER) | EFER_SVME;
    state->Cr0 = __readcr0();
    state->Cr2 = __readcr2();
    state->Cr3 = __readcr3();
    state->Cr4 = __readcr4();
    state->GPat = __readmsr(MSR_PAT);
    state->Cpl = 0;

    /* Resume point: exactly where RtlCaptureContext returned in the caller. */
    state->Rip = ctx->Rip;
    state->Rsp = ctx->Rsp;
    state->Rflags = ctx->EFlags;
    state->Rax = ctx->Rax;
}

static void HvrVirtualizeProcessor(PVIRTUAL_PROCESSOR_DATA vp)
{
    PCONTEXT ctx;
    UINT64 guestPa, hostPa, hostStatePa;

    ctx = (PCONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CONTEXT), HVR_TAG);
    if (ctx == NULL) {
        HVR_ERR("cpu %u: no memory for CONTEXT", vp->CpuIndex);
        return;
    }

    RtlCaptureContext(ctx);

    if (HvrIsHypervisorInstalled()) {
        /* Second pass: we are now the guest, running virtualized. */
        InterlockedOr(&vp->Virtualized, 1);
        InterlockedOr(&g_Hvr.Virtualized, 1);
        ExFreePoolWithTag(ctx, HVR_TAG);
        HVR_INFO("cpu %u: virtualized", vp->CpuIndex);
        return;
    }

    HvrSetupVmcb(vp, ctx);
    ExFreePoolWithTag(ctx, HVR_TAG);

    guestPa = (UINT64)MmGetPhysicalAddress(&vp->GuestVmcb).QuadPart;
    hostPa = (UINT64)MmGetPhysicalAddress(&vp->HostVmcb).QuadPart;
    hostStatePa = (UINT64)MmGetPhysicalAddress(&vp->HostStateArea[0]).QuadPart;

    vp->HostStack.Layout.GuestVmcbPa = guestPa;
    vp->HostStack.Layout.HostVmcbPa = hostPa;
    vp->HostStack.Layout.Self = vp;
    vp->HostStack.Layout.SharedVpData = vp->SharedVpData;

    __writemsr(IA32_MSR_EFER, __readmsr(IA32_MSR_EFER) | EFER_SVME);
    __writemsr(SVM_MSR_VM_HSAVE_PA, hostStatePa);

    /* Seed guest and host extra state (FS/GS/TR/LDTR/SYSENTER) from current. */
    __svm_vmsave(guestPa);
    __svm_vmsave(hostPa);

    HVR_INFO("cpu %u: entering VMRUN loop (guestVmcbPa=0x%llx)", vp->CpuIndex, guestPa);
    HvrLaunchVm(&vp->HostStack.Layout.GuestVmcbPa);

    /* Only reached if VMRUN never virtualized us (e.g. nested SVM withheld). */
    HVR_ERR("cpu %u: HvrLaunchVm returned - virtualization failed", vp->CpuIndex);
}

static void HvrDevirtualizeProcessor(PVIRTUAL_PROCESSOR_DATA vp)
{
    int regs[4] = {0};

    if (vp->Virtualized == 0) {
        return;
    }
    /* Ask our own hypervisor to tear down on this CPU. The CPUID exit handler
     * performs the orderly VMRUN-loop exit (see svm.asm / exit.c). */
    __cpuidex(regs, (int)HVR_CPUID_UNLOAD, 0);
    InterlockedAnd(&vp->Virtualized, 0);
    HVR_INFO("cpu %u: devirtualized (ack=0x%08x)", vp->CpuIndex, regs[0]);
}

_Function_class_(KDEFERRED_ROUTINE)
static void HvrVirtualizeDpc(PKDPC Dpc, PVOID Context, PVOID Sys1, PVOID Sys2)
{
    ULONG index = KeGetCurrentProcessorNumberEx(NULL);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    if (index < g_Hvr.VpCount && g_Hvr.VpArray[index]) {
        HvrVirtualizeProcessor(g_Hvr.VpArray[index]);
    }
    KeSignalCallDpcSynchronize(Sys2);
    KeSignalCallDpcDone(Sys1);
}

_Function_class_(KDEFERRED_ROUTINE)
static void HvrDevirtualizeDpc(PKDPC Dpc, PVOID Context, PVOID Sys1, PVOID Sys2)
{
    ULONG index = KeGetCurrentProcessorNumberEx(NULL);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    if (index < g_Hvr.VpCount && g_Hvr.VpArray[index]) {
        HvrDevirtualizeProcessor(g_Hvr.VpArray[index]);
    }
    KeSignalCallDpcSynchronize(Sys2);
    KeSignalCallDpcDone(Sys1);
}

static PVIRTUAL_PROCESSOR_DATA HvrAllocVp(ULONG index)
{
    PHYSICAL_ADDRESS low = {0}, high, boundary = {0};
    PVIRTUAL_PROCESSOR_DATA vp;

    high.QuadPart = MAXULONG64;
    vp = (PVIRTUAL_PROCESSOR_DATA)MmAllocateContiguousMemorySpecifyCache(
            sizeof(VIRTUAL_PROCESSOR_DATA), low, high, boundary, MmCached);
    if (vp == NULL) {
        return NULL;
    }
    RtlZeroMemory(vp, sizeof(*vp));
    vp->CpuIndex = index;
    vp->SharedVpData = &g_Hvr.Shared;
    return vp;
}

static void HvrFreeShared(void)
{
    if (g_Hvr.Shared.MsrPermissionMap) {
        MmFreeContiguousMemory(g_Hvr.Shared.MsrPermissionMap);
        g_Hvr.Shared.MsrPermissionMap = NULL;
    }
    if (g_Hvr.Shared.Pml4) {
        HvrFreeNpt((PNPT_TABLES)g_Hvr.Shared.Pml4);
        g_Hvr.Shared.Pml4 = NULL;
    }
}

static NTSTATUS HvrAllocShared(void)
{
    PHYSICAL_ADDRESS low = {0}, high, boundary = {0};
    PNPT_TABLES npt = NULL;
    PHYSICAL_ADDRESS pml4Pa = {0};
    NTSTATUS status;

    high.QuadPart = MAXULONG64;

    g_Hvr.Shared.MsrPermissionMap =
        MmAllocateContiguousMemorySpecifyCache(MSRPM_SIZE, low, high, boundary, MmCached);
    if (g_Hvr.Shared.MsrPermissionMap == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_Hvr.Shared.MsrPermissionMap, MSRPM_SIZE); /* intercept nothing */

    status = HvrBuildIdentityNpt(&npt, &pml4Pa);
    if (!NT_SUCCESS(status)) {
        HvrFreeShared();
        return status;
    }
    g_Hvr.Shared.Pml4 = npt;
    g_Hvr.Shared.Pml4Pa = pml4Pa;
    return STATUS_SUCCESS;
}

NTSTATUS HvrVirtualizeAllProcessors(void)
{
    NTSTATUS status;
    ULONG count, i;
    int regs[4] = {0};

    if (g_Hvr.Virtualized) {
        HVR_WARN("already virtualized");
        return STATUS_SUCCESS;
    }
    if (!HvrIsSvmUsable()) {
        return STATUS_NOT_SUPPORTED;
    }

    __cpuid(regs, (int)CPUID_FN_SVM_FEATURES);
    g_Hvr.NripsSupported = (BOOLEAN)((regs[3] & CPUID_SVM_EDX_NRIPS) != 0);
    if (!g_Hvr.NripsSupported) {
        HVR_WARN("NRIPS not reported; RIP advance will use fixed instruction lengths");
    }

    count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    g_Hvr.VpArray = (PVIRTUAL_PROCESSOR_DATA*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, count * sizeof(PVIRTUAL_PROCESSOR_DATA), HVR_TAG);
    if (g_Hvr.VpArray == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    g_Hvr.VpCount = count;

    status = HvrAllocShared();
    if (!NT_SUCCESS(status)) {
        goto fail;
    }

    for (i = 0; i < count; i++) {
        g_Hvr.VpArray[i] = HvrAllocVp(i);
        if (g_Hvr.VpArray[i] == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto fail;
        }
    }

    HVR_INFO("virtualizing %u logical processors", count);
    KeGenericCallDpc(HvrVirtualizeDpc, NULL);

    /* Verify every CPU reported success. */
    for (i = 0; i < count; i++) {
        if (g_Hvr.VpArray[i]->Virtualized == 0) {
            HVR_ERR("cpu %u failed to virtualize; rolling back", i);
            HvrDevirtualizeAllProcessors();
            return STATUS_UNSUCCESSFUL;
        }
    }
    HVR_INFO("all processors virtualized");
    return STATUS_SUCCESS;

fail:
    if (g_Hvr.VpArray) {
        for (i = 0; i < count; i++) {
            if (g_Hvr.VpArray[i]) {
                MmFreeContiguousMemory(g_Hvr.VpArray[i]);
            }
        }
        ExFreePoolWithTag(g_Hvr.VpArray, HVR_TAG);
        g_Hvr.VpArray = NULL;
    }
    HvrFreeShared();
    g_Hvr.VpCount = 0;
    return status;
}

NTSTATUS HvrDevirtualizeAllProcessors(void)
{
    ULONG i;

    if (g_Hvr.VpArray == NULL) {
        return STATUS_SUCCESS;
    }

    if (g_Hvr.Virtualized) {
        HVR_INFO("devirtualizing");
        KeGenericCallDpc(HvrDevirtualizeDpc, NULL);
        InterlockedAnd(&g_Hvr.Virtualized, 0);
    }

    for (i = 0; i < g_Hvr.VpCount; i++) {
        if (g_Hvr.VpArray[i]) {
            MmFreeContiguousMemory(g_Hvr.VpArray[i]);
            g_Hvr.VpArray[i] = NULL;
        }
    }
    ExFreePoolWithTag(g_Hvr.VpArray, HVR_TAG);
    g_Hvr.VpArray = NULL;
    g_Hvr.VpCount = 0;
    HvrFreeShared();
    HVR_INFO("devirtualized and freed");
    return STATUS_SUCCESS;
}
