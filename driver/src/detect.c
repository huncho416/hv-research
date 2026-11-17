/*
 * detect.c - Hardware / hypervisor / VBS capability probing.
 *
 * Everything here is read-only CPUID/MSR inspection. It answers the questions
 * the certificate cares about before we touch SVM at all:
 *   - Is this an AMD CPU with SVM + NPT?
 *   - Is SVM disabled by firmware (SVMDIS)?
 *   - Is a hypervisor already present (i.e. are we a guest)?
 *   - Does that hypervisor look like Microsoft Hyper-V (VBS likely on)?
 */
#include "svm.h"
#include "log.h"
#include <intrin.h>

static void HvrCpuidVendor(_Out_writes_(13) char* vendor)
{
    int regs[4] = {0};
    __cpuid(regs, CPUID_FN_VENDOR);
    *(int*)&vendor[0] = regs[1]; /* EBX */
    *(int*)&vendor[4] = regs[3]; /* EDX */
    *(int*)&vendor[8] = regs[2]; /* ECX */
    vendor[12] = '\0';
}

BOOLEAN HvrIsSvmUsable(void)
{
    int regs[4] = {0};
    char vendor[13];

    HvrCpuidVendor(vendor);
    if (strcmp(vendor, "AuthenticAMD") != 0 && strcmp(vendor, "HygonGenuine") != 0) {
        HVR_ERR("not an AMD/Hygon CPU (vendor=%s)", vendor);
        return FALSE;
    }

    __cpuid(regs, CPUID_FN_EXT_FEATURES);
    if ((regs[2] & CPUID_EXT_ECX_SVM) == 0) {
        HVR_ERR("SVM not supported (CPUID 8000_0001.ECX[2]=0)");
        return FALSE;
    }

    if ((__readmsr(SVM_MSR_VM_CR) & VM_CR_SVMDIS) != 0) {
        /* SVMDIS can mean BIOS-locked. CPUID 8000_000A.EDX[2] (SVML) tells us
         * whether it is lockable; either way we cannot enable EFER.SVME. */
        HVR_ERR("SVM disabled by firmware (VM_CR.SVMDIS=1) - enable SVM/\"SVM Mode\" in BIOS");
        return FALSE;
    }

    __cpuid(regs, CPUID_FN_SVM_FEATURES);
    if ((regs[3] & CPUID_SVM_EDX_NP) == 0) {
        HVR_ERR("Nested paging (NPT) not supported (CPUID 8000_000A.EDX[0]=0)");
        return FALSE;
    }

    return TRUE;
}

void HvrProbe(PHVR_PROBE_RESULT r)
{
    int regs[4] = {0};
    char vendor[13];

    RtlZeroMemory(r, sizeof(*r));
    r->StructSize = sizeof(*r);
    r->LogicalProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    HvrCpuidVendor(vendor);
    r->IsAmd = (UINT8)(strcmp(vendor, "AuthenticAMD") == 0 ||
                       strcmp(vendor, "HygonGenuine") == 0);

    __cpuid(regs, CPUID_FN_EXT_FEATURES);
    r->SvmSupported = (UINT8)((regs[2] & CPUID_EXT_ECX_SVM) != 0);

    __cpuid(regs, CPUID_FN_SVM_FEATURES);
    r->NptSupported = (UINT8)((regs[3] & CPUID_SVM_EDX_NP) != 0);
    r->SvmRevisionAndFeatures = (UINT32)regs[0];
    r->NumAsids = (UINT32)regs[1];

    r->SvmDisabledByBios = (UINT8)((__readmsr(SVM_MSR_VM_CR) & VM_CR_SVMDIS) != 0);

    __cpuid(regs, CPUID_FN_FEATURES);
    r->HypervisorPresent = (UINT8)((regs[2] & CPUID_FEAT_ECX_HV) != 0);

    if (r->HypervisorPresent) {
        char hv[13] = {0};
        __cpuid(regs, CPUID_FN_HV_VENDOR);
        r->HypervisorMaxLeaf = (UINT32)regs[0];
        *(int*)&hv[0] = regs[1];
        *(int*)&hv[4] = regs[2];
        *(int*)&hv[8] = regs[3];
        hv[12] = '\0';
        RtlCopyMemory(r->HypervisorVendor, hv, sizeof(hv));

        /* "Microsoft Hv" strongly implies we are a guest of Hyper-V, which on
         * a modern client build is the VBS root hypervisor. This is a
         * heuristic, not proof HVCI is on - correlate with msinfo32. */
        r->VbsLikelyOn = (UINT8)(strcmp(hv, "Microsoft Hv") == 0);
        r->AlreadyVirtualized = 1;
    }

    HVR_INFO("probe: amd=%u svm=%u npt=%u svmdis=%u hv=%u vendor='%s' vbsLikely=%u cpus=%u",
             r->IsAmd, r->SvmSupported, r->NptSupported, r->SvmDisabledByBios,
             r->HypervisorPresent, r->HypervisorVendor, r->VbsLikelyOn,
             r->LogicalProcessorCount);
}
