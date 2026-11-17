/*
 * ioctl.h - User/kernel interface for the hv-research control device.
 *
 * This header is shared by the driver and the host-side tool. It defines the
 * device name, the IOCTL codes, and the request/response structures. All of
 * this is observational: PROBE/GET_STATS read state, VIRTUALIZE/DEVIRTUALIZE
 * toggle the hypervisor, and RUN_EXPERIMENT drives the measurement harness.
 */
#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#endif

#define HVR_DEVICE_NAME     L"\\Device\\hvresearch"
#define HVR_SYMLINK_NAME    L"\\DosDevices\\hvresearch"
#define HVR_WIN32_NAME      L"\\\\.\\hvresearch"

/* Custom device type in the 32768-65535 vendor range (per WDK convention). */
#define HVR_DEVICE_TYPE     0x8000

#define HVR_IOCTL(index) \
    CTL_CODE(HVR_DEVICE_TYPE, (index), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_HVR_PROBE          HVR_IOCTL(0x800)  /* query HW/VBS capabilities */
#define IOCTL_HVR_VIRTUALIZE     HVR_IOCTL(0x801)  /* enter SVM on all CPUs      */
#define IOCTL_HVR_DEVIRTUALIZE   HVR_IOCTL(0x802)  /* leave SVM on all CPUs      */
#define IOCTL_HVR_GET_STATS      HVR_IOCTL(0x803)  /* read per-CPU exit counters */
#define IOCTL_HVR_RUN_EXPERIMENT HVR_IOCTL(0x804)  /* run a measurement experiment */

#pragma pack(push, 1)

/* ---- IOCTL_HVR_PROBE ---------------------------------------------------- */
typedef struct _HVR_PROBE_RESULT
{
    UINT32 StructSize;
    UINT32 LogicalProcessorCount;

    /* Hardware capability (CPUID) */
    UINT8  IsAmd;                 /* vendor == AuthenticAMD                   */
    UINT8  SvmSupported;          /* CPUID 8000_0001.ECX[2]                   */
    UINT8  NptSupported;          /* CPUID 8000_000A.EDX[0]                   */
    UINT8  SvmDisabledByBios;     /* VM_CR MSR (0xC0010114) bit 4 (SVMDIS)    */
    UINT32 SvmRevisionAndFeatures;/* CPUID 8000_000A raw EAX / EDX packed     */
    UINT32 NumAsids;              /* CPUID 8000_000A.EBX                      */

    /* Hypervisor presence (we are a guest if this is set) */
    UINT8  HypervisorPresent;     /* CPUID 1.ECX[31]                          */
    CHAR   HypervisorVendor[13];  /* CPUID 4000_0000 vendor string            */
    UINT32 HypervisorMaxLeaf;     /* CPUID 4000_0000.EAX                      */

    /* VBS / HVCI indicators the driver can observe from kernel mode */
    UINT8  VbsLikelyOn;           /* heuristic: Hyper-V present + feature bits */
    UINT8  AlreadyVirtualized;    /* best-effort: are we nested?              */
    UINT32 ReservedFlags;
} HVR_PROBE_RESULT, *PHVR_PROBE_RESULT;

/* ---- IOCTL_HVR_GET_STATS ------------------------------------------------ */
typedef struct _HVR_CPU_STATS
{
    UINT32 CpuIndex;
    UINT32 Virtualized;
    UINT64 ExitCpuid;
    UINT64 ExitMsr;
    UINT64 ExitNpf;
    UINT64 ExitVmmcall;
    UINT64 ExitOther;
    UINT64 LastExitCode;
} HVR_CPU_STATS, *PHVR_CPU_STATS;

typedef struct _HVR_STATS_RESULT
{
    UINT32 StructSize;
    UINT32 CpuCount;
    HVR_CPU_STATS Cpu[64];
} HVR_STATS_RESULT, *PHVR_STATS_RESULT;

/* ---- IOCTL_HVR_RUN_EXPERIMENT ------------------------------------------ */
enum HVR_EXPERIMENT_ID
{
    HVR_EXP_READ_KERNEL     = 2, /* E2: read bytes at a kernel VA via hypervisor view */
    HVR_EXP_WRITE_EXEC_PAGE = 3, /* E3: attempt a write to an exec kernel page         */
    HVR_EXP_NPT_HOOK_DEMO   = 4, /* E4: NPT split-hook mechanics on our own page       */
};

typedef struct _HVR_EXPERIMENT_REQUEST
{
    UINT32 StructSize;
    UINT32 ExperimentId;   /* enum HVR_EXPERIMENT_ID */
    UINT64 TargetVa;       /* kernel VA to operate on (E2/E3)                 */
    UINT32 Length;         /* bytes to read/compare (bounded)                */
    UINT8  PatchByte;      /* byte to attempt to write (E3)                  */
    UINT8  Reserved[3];
} HVR_EXPERIMENT_REQUEST, *PHVR_EXPERIMENT_REQUEST;

typedef struct _HVR_EXPERIMENT_RESULT
{
    UINT32 StructSize;
    UINT32 ExperimentId;
    UINT32 NtStatus;       /* NTSTATUS of the operation                      */
    UINT32 Outcome;        /* enum HVR_OUTCOME                               */
    UINT64 TargetPa;       /* resolved guest-physical address                */
    UINT32 BytesReturned;
    UINT8  Before[64];     /* bytes before the operation                     */
    UINT8  After[64];      /* bytes after the operation                      */
    CHAR   Note[128];      /* human-readable summary of what happened        */
} HVR_EXPERIMENT_RESULT, *PHVR_EXPERIMENT_RESULT;

enum HVR_OUTCOME
{
    HVR_OUTCOME_UNKNOWN          = 0,
    HVR_OUTCOME_SUCCEEDED        = 1, /* operation took effect                 */
    HVR_OUTCOME_BLOCKED          = 2, /* operation was prevented/not reflected */
    HVR_OUTCOME_FAULTED          = 3, /* generated a fault/exception           */
    HVR_OUTCOME_NOT_VIRTUALIZED  = 4, /* hypervisor not active                 */
    HVR_OUTCOME_INVALID_TARGET   = 5,
};

#pragma pack(pop)
