/*
 * svm.h - AMD SVM (AMD-V) data structures and driver-wide state.
 *
 * The VMCB layout follows the AMD64 Architecture Programmer's Manual, Vol. 2,
 * Appendix B. The structure definitions are the standard ones also used by
 * public research hypervisors (e.g. SimpleSvm, MIT). Offsets are asserted at
 * compile time in svm.c.
 */
#pragma once
#include <ntddk.h>
#include "ioctl.h"

/* ---- MSRs and CPUID leaves we touch ------------------------------------ */
#define IA32_MSR_EFER            0xC0000080
#define SVM_MSR_VM_CR            0xC0010114
#define SVM_MSR_VM_HSAVE_PA      0xC0010117
#define EFER_SVME                (1ULL << 12)
#define VM_CR_SVMDIS             (1ULL << 4)

#define CPUID_FN_VENDOR          0x00000000
#define CPUID_FN_FEATURES        0x00000001
#define CPUID_FN_EXT_FEATURES    0x80000001
#define CPUID_FN_SVM_FEATURES    0x8000000A
#define CPUID_FN_HV_VENDOR       0x40000000
#define CPUID_EXT_ECX_SVM        (1UL << 2)   /* 8000_0001.ECX[2]  */
#define CPUID_SVM_EDX_NP         (1UL << 0)   /* 8000_000A.EDX[0]  */
#define CPUID_SVM_EDX_NRIPS      (1UL << 3)   /* 8000_000A.EDX[3]  */
#define CPUID_FEAT_ECX_HV        (1UL << 31)  /* 0000_0001.ECX[31] */

/* A private CPUID leaf our hypervisor answers, used to detect ourselves and
 * to request an orderly unload from inside the guest. */
#define HVR_CPUID_SIGNATURE      0x40000100   /* returns 'Hvsr' in EBX       */
#define HVR_CPUID_UNLOAD         0x40000101
#define HVR_SIGNATURE_EBX        0x72767348   /* 'Hvsr' little-endian        */

/* ---- VMCB control area intercept bits (subset we use) ------------------ */
#define SVM_INTERCEPT_MISC1_CPUID   (1UL << 18)
#define SVM_INTERCEPT_MISC1_MSR     (1UL << 28)
#define SVM_INTERCEPT_MISC2_VMRUN   (1UL << 0)
#define SVM_INTERCEPT_MISC2_VMMCALL (1UL << 1)

/* ---- #VMEXIT codes (subset) -------------------------------------------- */
#define SVM_EXIT_CPUID           0x072
#define SVM_EXIT_MSR             0x07C
#define SVM_EXIT_VMRUN           0x080
#define SVM_EXIT_VMMCALL         0x081
#define SVM_EXIT_NPF             0x400
#define SVM_EXIT_INVALID         (-1)

#pragma warning(push)
#pragma warning(disable : 4201) /* nameless struct/union */

typedef union _SEGMENT_ATTRIBUTE
{
    UINT16 AsUInt16;
    struct
    {
        UINT16 Type : 4;
        UINT16 System : 1;
        UINT16 Dpl : 2;
        UINT16 Present : 1;
        UINT16 Avl : 1;
        UINT16 LongMode : 1;
        UINT16 DefaultBit : 1;
        UINT16 Granularity : 1;
        UINT16 Reserved : 4;
    } Fields;
} SEGMENT_ATTRIBUTE, *PSEGMENT_ATTRIBUTE;

typedef struct _VMCB_CONTROL_AREA
{
    UINT16 InterceptCrRead;             // +0x000
    UINT16 InterceptCrWrite;            // +0x002
    UINT16 InterceptDrRead;             // +0x004
    UINT16 InterceptDrWrite;            // +0x006
    UINT32 InterceptException;          // +0x008
    UINT32 InterceptMisc1;              // +0x00c
    UINT32 InterceptMisc2;              // +0x010
    UINT8  Reserved1[0x03c - 0x014];    // +0x014
    UINT16 PauseFilterThreshold;        // +0x03c
    UINT16 PauseFilterCount;            // +0x03e
    UINT64 IopmBasePa;                  // +0x040
    UINT64 MsrpmBasePa;                 // +0x048
    UINT64 TscOffset;                   // +0x050
    UINT32 GuestAsid;                   // +0x058
    UINT32 TlbControl;                  // +0x05c
    UINT64 VIntr;                       // +0x060
    UINT64 InterruptShadow;             // +0x068
    UINT64 ExitCode;                    // +0x070
    UINT64 ExitInfo1;                   // +0x078
    UINT64 ExitInfo2;                   // +0x080
    UINT64 ExitIntInfo;                 // +0x088
    UINT64 NpEnable;                    // +0x090
    UINT64 AvicApicBar;                 // +0x098
    UINT64 GuestPaOfGhcb;               // +0x0a0
    UINT64 EventInj;                    // +0x0a8
    UINT64 NCr3;                        // +0x0b0
    UINT64 LbrVirtualizationEnable;     // +0x0b8
    UINT64 VmcbClean;                   // +0x0c0
    UINT64 NRip;                        // +0x0c8
    UINT8  NumOfBytesFetched;           // +0x0d0
    UINT8  GuestInstructionBytes[15];   // +0x0d1
    UINT64 AvicApicBackingPagePointer;  // +0x0e0
    UINT64 Reserved2;                   // +0x0e8
    UINT64 AvicLogicalTablePointer;     // +0x0f0
    UINT64 AvicPhysicalTablePointer;    // +0x0f8
    UINT64 Reserved3;                   // +0x100
    UINT64 VmcbSaveStatePointer;        // +0x108
    UINT8  Reserved4[0x400 - 0x110];    // +0x110
} VMCB_CONTROL_AREA, *PVMCB_CONTROL_AREA;
static_assert(sizeof(VMCB_CONTROL_AREA) == 0x400, "VMCB_CONTROL_AREA size");

typedef struct _VMCB_STATE_SAVE_AREA
{
    UINT16 EsSelector;                  // +0x000
    UINT16 EsAttrib;
    UINT32 EsLimit;
    UINT64 EsBase;
    UINT16 CsSelector;                  // +0x010
    UINT16 CsAttrib;
    UINT32 CsLimit;
    UINT64 CsBase;
    UINT16 SsSelector;                  // +0x020
    UINT16 SsAttrib;
    UINT32 SsLimit;
    UINT64 SsBase;
    UINT16 DsSelector;                  // +0x030
    UINT16 DsAttrib;
    UINT32 DsLimit;
    UINT64 DsBase;
    UINT16 FsSelector;                  // +0x040
    UINT16 FsAttrib;
    UINT32 FsLimit;
    UINT64 FsBase;
    UINT16 GsSelector;                  // +0x050
    UINT16 GsAttrib;
    UINT32 GsLimit;
    UINT64 GsBase;
    UINT16 GdtrSelector;                // +0x060
    UINT16 GdtrAttrib;
    UINT32 GdtrLimit;
    UINT64 GdtrBase;
    UINT16 LdtrSelector;                // +0x070
    UINT16 LdtrAttrib;
    UINT32 LdtrLimit;
    UINT64 LdtrBase;
    UINT16 IdtrSelector;                // +0x080
    UINT16 IdtrAttrib;
    UINT32 IdtrLimit;
    UINT64 IdtrBase;
    UINT16 TrSelector;                  // +0x090
    UINT16 TrAttrib;
    UINT32 TrLimit;
    UINT64 TrBase;
    UINT8  Reserved1[0x0cb - 0x0a0];    // +0x0a0
    UINT8  Cpl;                         // +0x0cb
    UINT32 Reserved2;                   // +0x0cc
    UINT64 Efer;                        // +0x0d0
    UINT8  Reserved3[0x148 - 0x0d8];    // +0x0d8
    UINT64 Cr4;                         // +0x148
    UINT64 Cr3;                         // +0x150
    UINT64 Cr0;                         // +0x158
    UINT64 Dr7;                         // +0x160
    UINT64 Dr6;                         // +0x168
    UINT64 Rflags;                      // +0x170
    UINT64 Rip;                         // +0x178
    UINT8  Reserved4[0x1d8 - 0x180];    // +0x180
    UINT64 Rsp;                         // +0x1d8
    UINT8  Reserved5[0x1f8 - 0x1e0];    // +0x1e0
    UINT64 Rax;                         // +0x1f8
    UINT64 Star;                        // +0x200
    UINT64 LStar;                       // +0x208
    UINT64 CStar;                       // +0x210
    UINT64 SfMask;                      // +0x218
    UINT64 KernelGsBase;                // +0x220
    UINT64 SysenterCs;                  // +0x228
    UINT64 SysenterEsp;                 // +0x230
    UINT64 SysenterEip;                 // +0x238
    UINT64 Cr2;                         // +0x240
    UINT8  Reserved6[0x268 - 0x248];    // +0x248
    UINT64 GPat;                        // +0x268
    UINT64 DbgCtl;                      // +0x270
    UINT64 BrFrom;                      // +0x278
    UINT64 BrTo;                        // +0x280
    UINT64 LastExcepFrom;               // +0x288
    UINT64 LastExcepTo;                 // +0x290
} VMCB_STATE_SAVE_AREA, *PVMCB_STATE_SAVE_AREA;

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _VMCB
{
    VMCB_CONTROL_AREA ControlArea;
    VMCB_STATE_SAVE_AREA StateSaveArea;
    UINT8 Reserved[PAGE_SIZE - sizeof(VMCB_CONTROL_AREA) - sizeof(VMCB_STATE_SAVE_AREA)];
} VMCB, *PVMCB;
static_assert(sizeof(VMCB) == PAGE_SIZE, "VMCB must be one page");

/*
 * GUEST_REGISTERS - general-purpose registers pushed by the assembly VMEXIT
 * stub, in the order it pushes them (see svm.asm, PUSHAQ macro). The lowest
 * offset (R15) is at the lowest address, i.e. pushed LAST. RAX here is the
 * HOST rax at the moment of #VMEXIT (it holds GuestVmcbPa), NOT the guest rax;
 * the guest's rax lives in VMCB.StateSaveArea.Rax and is handled separately.
 */
typedef struct _GUEST_REGISTERS
{
    UINT64 R15;   // +0x00
    UINT64 R14;   // +0x08
    UINT64 R13;   // +0x10
    UINT64 R12;   // +0x18
    UINT64 R11;   // +0x20
    UINT64 R10;   // +0x28
    UINT64 R9;    // +0x30
    UINT64 R8;    // +0x38
    UINT64 Rdi;   // +0x40
    UINT64 Rsi;   // +0x48
    UINT64 Rbp;   // +0x50
    UINT64 Rsp;   // +0x58 placeholder; true guest RSP is in VMCB
    UINT64 Rbx;   // +0x60
    UINT64 Rdx;   // +0x68
    UINT64 Rcx;   // +0x70
    UINT64 Rax;   // +0x78 (host rax at exit = GuestVmcbPa)
} GUEST_REGISTERS, *PGUEST_REGISTERS;

/* Shared, read-mostly data referenced by every virtual processor. */
typedef struct _SHARED_VP_DATA
{
    PVOID  MsrPermissionMap;       /* 8 KiB MSRPM, non-paged, page-aligned    */
    PVOID  Pml4;                   /* top-level nested page table (NPT)        */
    PHYSICAL_ADDRESS Pml4Pa;
} SHARED_VP_DATA, *PSHARED_VP_DATA;

/*
 * Per-CPU data. The assembly launch routine (HvrLaunchVm) is given a pointer
 * to HostStack.Layout.GuestVmcbPa and sets RSP there. The six trailing fields
 * MUST stay in this exact order because the assembly reads them as:
 *     [rsp+0x00] GuestVmcbPa
 *     [rsp+0x08] HostVmcbPa
 *     [rsp+0x10] Self
 *     [rsp+0x18] SharedVpData
 *     [rsp+0x20] Padding1
 *     [rsp+0x28] Reserved1
 */
typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _VIRTUAL_PROCESSOR_DATA
{
    union
    {
        struct
        {
            UINT8 StackContents[KERNEL_STACK_SIZE - sizeof(PVOID) * 6];
            UINT64 GuestVmcbPa;
            UINT64 HostVmcbPa;
            struct _VIRTUAL_PROCESSOR_DATA* Self;
            PSHARED_VP_DATA SharedVpData;
            UINT64 Padding1;
            UINT64 Reserved1;
        } Layout;
        UINT8 Limit[KERNEL_STACK_SIZE];
    } HostStack;

    VMCB GuestVmcb;                     /* page aligned */
    VMCB HostVmcb;                      /* page aligned */
    UINT8 HostStateArea[PAGE_SIZE];     /* for VMSAVE of host (VM_HSAVE_PA)     */

    PSHARED_VP_DATA SharedVpData;
    UINT32 CpuIndex;
    volatile LONG Virtualized;

    /* exit counters (read by IOCTL_HVR_GET_STATS) */
    volatile LONG64 ExitCpuid;
    volatile LONG64 ExitMsr;
    volatile LONG64 ExitNpf;
    volatile LONG64 ExitVmmcall;
    volatile LONG64 ExitOther;
    volatile LONG64 LastExitCode;
} VIRTUAL_PROCESSOR_DATA, *PVIRTUAL_PROCESSOR_DATA;

#pragma warning(pop)

/* ---- driver-wide context ----------------------------------------------- */
typedef struct _HVR_CONTEXT
{
    SHARED_VP_DATA Shared;
    PVIRTUAL_PROCESSOR_DATA* VpArray;   /* one pointer per logical processor   */
    ULONG VpCount;
    BOOLEAN NripsSupported;
    volatile LONG Virtualized;          /* non-zero if any CPU is virtualized  */
} HVR_CONTEXT, *PHVR_CONTEXT;

extern HVR_CONTEXT g_Hvr;

/* ---- svm.c ------------------------------------------------------------- */
NTSTATUS HvrVirtualizeAllProcessors(void);
NTSTATUS HvrDevirtualizeAllProcessors(void);

/* ---- detect.c --------------------------------------------------------- */
void HvrProbe(PHVR_PROBE_RESULT Result);
BOOLEAN HvrIsSvmUsable(void);

/* ---- exit.c ----------------------------------------------------------- */
/* Returns TRUE if the hypervisor should tear down and return to the host on
 * this CPU (used by the orderly unload path). */
BOOLEAN HvrHandleVmExit(PVIRTUAL_PROCESSOR_DATA Vp, PGUEST_REGISTERS Regs);

/* ---- assembly (svm.asm) ----------------------------------------------- */
void HvrLaunchVm(void* HostRsp);
BOOLEAN HvrIsHypervisorInstalled(void);  /* CPUID HVR_CPUID_SIGNATURE probe   */
void HvrGetGdtBase(void* Descriptor);    /* sgdt wrapper                      */
UINT16 HvrGetCs(void);
UINT16 HvrGetSs(void);
UINT16 HvrGetDs(void);
UINT16 HvrGetEs(void);
UINT16 HvrGetFs(void);
UINT16 HvrGetGs(void);
UINT16 HvrGetTr(void);
UINT16 HvrGetLdtr(void);
