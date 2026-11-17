/*
 * exit.c - #VMEXIT handler (C side).
 *
 * Called from svm.asm after each VMRUN returns. We only intercept CPUID, which
 * is enough to (a) expose our hypervisor signature so bring-up can detect the
 * second pass, (b) service an orderly unload request, and (c) keep the guest
 * OS working by forwarding all other CPUID leaves to the real instruction.
 *
 * Returns TRUE to request teardown on this CPU (unload path); FALSE to resume
 * the guest.
 */
#include "svm.h"
#include "log.h"
#include <intrin.h>

static void HvrAdvanceRip(PVIRTUAL_PROCESSOR_DATA vp, UINT64 fallbackLen)
{
    if (g_Hvr.NripsSupported && vp->GuestVmcb.ControlArea.NRip != 0) {
        vp->GuestVmcb.StateSaveArea.Rip = vp->GuestVmcb.ControlArea.NRip;
    } else {
        vp->GuestVmcb.StateSaveArea.Rip += fallbackLen;
    }
}

static BOOLEAN HvrHandleCpuid(PVIRTUAL_PROCESSOR_DATA vp, PGUEST_REGISTERS regs)
{
    int out[4];
    UINT32 leaf = (UINT32)vp->GuestVmcb.StateSaveArea.Rax;
    UINT32 sub = (UINT32)regs->Rcx;

    if (leaf == HVR_CPUID_SIGNATURE) {
        /* Identify ourselves so the bring-up second-pass check succeeds. */
        vp->GuestVmcb.StateSaveArea.Rax = HVR_CPUID_SIGNATURE;
        regs->Rbx = HVR_SIGNATURE_EBX; /* 'Hvsr' */
        regs->Rcx = 0;
        regs->Rdx = 0;
        HvrAdvanceRip(vp, 2);
        return FALSE;
    }

    if (leaf == HVR_CPUID_UNLOAD) {
        /* Orderly teardown on this CPU. Stash the resume context into the
         * pushed register image for the assembly teardown path:
         *   Rbx <- resume RIP (next instruction after this CPUID)
         *   Rdx <- guest RFLAGS
         *   Rcx <- guest RSP
         * (See svm.asm @ExitVm.) */
        UINT64 resumeRip;
        if (g_Hvr.NripsSupported && vp->GuestVmcb.ControlArea.NRip != 0) {
            resumeRip = vp->GuestVmcb.ControlArea.NRip;
        } else {
            resumeRip = vp->GuestVmcb.StateSaveArea.Rip + 2;
        }
        regs->Rbx = resumeRip;
        regs->Rdx = vp->GuestVmcb.StateSaveArea.Rflags;
        regs->Rcx = vp->GuestVmcb.StateSaveArea.Rsp;
        return TRUE;
    }

    /* Forward everything else to the real CPUID so the guest OS is unaffected. */
    __cpuidex(out, (int)leaf, (int)sub);
    vp->GuestVmcb.StateSaveArea.Rax = (UINT32)out[0];
    regs->Rbx = (UINT32)out[1];
    regs->Rcx = (UINT32)out[2];
    regs->Rdx = (UINT32)out[3];
    HvrAdvanceRip(vp, 2);
    return FALSE;
}

BOOLEAN HvrHandleVmExit(PVIRTUAL_PROCESSOR_DATA vp, PGUEST_REGISTERS regs)
{
    UINT64 code = vp->GuestVmcb.ControlArea.ExitCode;

    InterlockedExchange64(&vp->LastExitCode, (LONG64)code);

    switch (code) {
    case SVM_EXIT_CPUID:
        InterlockedIncrement64(&vp->ExitCpuid);
        return HvrHandleCpuid(vp, regs);

    case SVM_EXIT_MSR:
        InterlockedIncrement64(&vp->ExitMsr);
        /* We do not arm MSR intercepts in this build; treat as a no-op skip. */
        HvrAdvanceRip(vp, 2);
        return FALSE;

    case SVM_EXIT_NPF:
        InterlockedIncrement64(&vp->ExitNpf);
        /* Identity NPT should not fault. If it does, log ExitInfo for study. */
        HVR_WARN("cpu %u NPF: info1=0x%llx gpa=0x%llx", vp->CpuIndex,
                 vp->GuestVmcb.ControlArea.ExitInfo1,
                 vp->GuestVmcb.ControlArea.ExitInfo2);
        return FALSE;

    case SVM_EXIT_VMMCALL:
        InterlockedIncrement64(&vp->ExitVmmcall);
        HvrAdvanceRip(vp, 3);
        return FALSE;

    case (UINT64)SVM_EXIT_INVALID:
        HVR_ERR("cpu %u: VMEXIT_INVALID - malformed VMCB", vp->CpuIndex);
        InterlockedIncrement64(&vp->ExitOther);
        return TRUE; /* bail out */

    default:
        InterlockedIncrement64(&vp->ExitOther);
        HVR_WARN("cpu %u: unhandled exit 0x%llx", vp->CpuIndex, code);
        return FALSE;
    }
}
