/*
 * hvctl.c - user-mode control / evidence-collection tool for hv-research.
 *
 * Usage:
 *   hvctl probe                         - print HW/VBS capability probe
 *   hvctl on                            - virtualize all processors
 *   hvctl off                           - devirtualize
 *   hvctl stats                         - dump per-CPU #VMEXIT counters
 *   hvctl read   <hexVA> [len]          - E2: read kernel bytes via hv view
 *   hvctl write  <hexVA> <hexByte>      - E3: attempt write to exec page
 *   hvctl hook   <hexVA>                - E4: NPT split-hook demo (documented)
 *
 * Build with the Windows SDK (user-mode). Run elevated on the research VM.
 * This tool only talks to the research driver; it performs no bypass itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../driver/inc/ioctl.h"

static HANDLE OpenDev(void)
{
    HANDLE h = CreateFileW(HVR_WIN32_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "open %ws failed: %lu (is the driver loaded?)\n",
                HVR_WIN32_NAME, GetLastError());
    }
    return h;
}

static int DoProbe(HANDLE h)
{
    HVR_PROBE_RESULT r = {0};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_HVR_PROBE, NULL, 0, &r, sizeof(r), &got, NULL)) {
        fprintf(stderr, "PROBE failed: %lu\n", GetLastError());
        return 1;
    }
    printf("== hv-research probe ==\n");
    printf("logical CPUs        : %u\n", r.LogicalProcessorCount);
    printf("AMD vendor          : %u\n", r.IsAmd);
    printf("SVM supported       : %u\n", r.SvmSupported);
    printf("NPT supported       : %u\n", r.NptSupported);
    printf("SVM disabled (BIOS) : %u\n", r.SvmDisabledByBios);
    printf("ASIDs               : %u\n", r.NumAsids);
    printf("hypervisor present  : %u\n", r.HypervisorPresent);
    printf("hypervisor vendor   : '%s'\n", r.HypervisorVendor);
    printf("VBS likely ON       : %u\n", r.VbsLikelyOn);
    printf("already virtualized : %u\n", r.AlreadyVirtualized);
    printf("\nInterpretation: if 'hypervisor present'=1 and vendor='Microsoft Hv',\n"
           "you are a guest of Hyper-V (the VBS root hypervisor). Any SVM you bring\n"
           "up here is NESTED and sits below VBS/HVCI. Correlate VBS/HVCI state with\n"
           "msinfo32 and record it in the experiment config block.\n");
    return 0;
}

static int DoSimple(HANDLE h, DWORD code, const char* name)
{
    DWORD got = 0;
    if (!DeviceIoControl(h, code, NULL, 0, NULL, 0, &got, NULL)) {
        fprintf(stderr, "%s failed: %lu\n", name, GetLastError());
        return 1;
    }
    printf("%s: OK\n", name);
    return 0;
}

static int DoStats(HANDLE h)
{
    HVR_STATS_RESULT s = {0};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_HVR_GET_STATS, NULL, 0, &s, sizeof(s), &got, NULL)) {
        fprintf(stderr, "GET_STATS failed: %lu\n", GetLastError());
        return 1;
    }
    printf("cpu  virt   cpuid        msr          npf          vmmcall      other        lastexit\n");
    for (UINT32 i = 0; i < s.CpuCount; i++) {
        HVR_CPU_STATS* c = &s.Cpu[i];
        printf("%-4u %-6u %-12llu %-12llu %-12llu %-12llu %-12llu 0x%llx\n",
               c->CpuIndex, c->Virtualized, c->ExitCpuid, c->ExitMsr, c->ExitNpf,
               c->ExitVmmcall, c->ExitOther, c->LastExitCode);
    }
    return 0;
}

static void PrintBytes(const char* label, const UINT8* b, UINT32 n)
{
    printf("%s", label);
    for (UINT32 i = 0; i < n; i++) printf("%02x ", b[i]);
    printf("\n");
}

static int DoExperiment(HANDLE h, UINT32 id, UINT64 va, UINT32 len, UINT8 patch)
{
    HVR_EXPERIMENT_REQUEST req = {0};
    HVR_EXPERIMENT_RESULT res = {0};
    DWORD got = 0;
    req.StructSize = sizeof(req);
    req.ExperimentId = id;
    req.TargetVa = va;
    req.Length = len;
    req.PatchByte = patch;
    if (!DeviceIoControl(h, IOCTL_HVR_RUN_EXPERIMENT, &req, sizeof(req),
                         &res, sizeof(res), &got, NULL)) {
        fprintf(stderr, "RUN_EXPERIMENT failed: %lu\n", GetLastError());
        return 1;
    }
    printf("== experiment %u ==\n", res.ExperimentId);
    printf("NTSTATUS : 0x%08x\n", res.NtStatus);
    printf("outcome  : %u (1=succeeded 2=blocked 3=faulted 4=not-virt 5=bad-target)\n", res.Outcome);
    printf("targetPA : 0x%llx\n", res.TargetPa);
    if (res.BytesReturned) PrintBytes("before   : ", res.Before, res.BytesReturned > 16 ? 16 : res.BytesReturned);
    PrintBytes("after    : ", res.After, 16);
    printf("note     : %s\n", res.Note);
    return 0;
}

int main(int argc, char** argv)
{
    HANDLE h;
    int rc;
    if (argc < 2) {
        printf("usage: hvctl probe|on|off|stats|read <hexVA> [len]|write <hexVA> <hexByte>|hook <hexVA>\n");
        return 2;
    }
    h = OpenDev();
    if (h == INVALID_HANDLE_VALUE) return 1;

    if (!strcmp(argv[1], "probe"))      rc = DoProbe(h);
    else if (!strcmp(argv[1], "on"))    rc = DoSimple(h, IOCTL_HVR_VIRTUALIZE, "VIRTUALIZE");
    else if (!strcmp(argv[1], "off"))   rc = DoSimple(h, IOCTL_HVR_DEVIRTUALIZE, "DEVIRTUALIZE");
    else if (!strcmp(argv[1], "stats")) rc = DoStats(h);
    else if (!strcmp(argv[1], "read") && argc >= 3)
        rc = DoExperiment(h, HVR_EXP_READ_KERNEL, _strtoui64(argv[2], NULL, 16),
                          argc >= 4 ? (UINT32)strtoul(argv[3], NULL, 10) : 64, 0);
    else if (!strcmp(argv[1], "write") && argc >= 4)
        rc = DoExperiment(h, HVR_EXP_WRITE_EXEC_PAGE, _strtoui64(argv[2], NULL, 16),
                          1, (UINT8)strtoul(argv[3], NULL, 16));
    else if (!strcmp(argv[1], "hook") && argc >= 3)
        rc = DoExperiment(h, HVR_EXP_NPT_HOOK_DEMO, _strtoui64(argv[2], NULL, 16), 0, 0);
    else { printf("bad arguments\n"); rc = 2; }

    CloseHandle(h);
    return rc;
}
