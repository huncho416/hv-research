/*
 * npt.c - Build a 1:1 identity nested page table (512 GiB, 2 MiB leaves).
 *
 * The identity map means the guest sees physical memory unchanged; the point
 * is simply that every access now passes through tables WE own, so we can
 * instrument/split entries. On a VBS system this is a *nested* NPT and does
 * not override Hyper-V's real SLAT - see docs/02.
 */
#include "npt.h"
#include "log.h"

void HvrFreeNpt(_In_opt_ PNPT_TABLES Tables)
{
    if (Tables) {
        MmFreeContiguousMemory(Tables);
    }
}

NTSTATUS HvrBuildIdentityNpt(_Out_ PNPT_TABLES* OutTables,
                             _Out_ PHYSICAL_ADDRESS* OutPml4Pa)
{
    PHYSICAL_ADDRESS low, high, boundary;
    PNPT_TABLES t;

    *OutTables = NULL;
    OutPml4Pa->QuadPart = 0;

    low.QuadPart = 0;
    high.QuadPart = MAXULONG64;
    boundary.QuadPart = 0;

    /* NPT_TABLES is large (~2 MiB). Allocate contiguous, non-paged. */
    t = (PNPT_TABLES)MmAllocateContiguousMemorySpecifyCache(
            sizeof(NPT_TABLES), low, high, boundary, MmCached);
    if (t == NULL) {
        HVR_ERR("failed to allocate NPT_TABLES (%llu bytes)", (UINT64)sizeof(NPT_TABLES));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(t, sizeof(*t));

    /* PML4: a single entry covering 512 GiB points at the PDPT. */
    {
        PHYSICAL_ADDRESS pdptPa = MmGetPhysicalAddress(&t->Pdpt[0]);
        t->Pml4[0].AsUInt64 = 0;
        t->Pml4[0].Fields.Present = 1;
        t->Pml4[0].Fields.Write = 1;
        t->Pml4[0].Fields.User = 1;
        t->Pml4[0].Fields.PageFrameNumber = (UINT64)(pdptPa.QuadPart >> PAGE_SHIFT);
    }

    /* PDPT: 512 entries, each covering 1 GiB, each pointing at a PD page. */
    for (ULONG i = 0; i < NPT_PDPTE_COUNT; i++) {
        PHYSICAL_ADDRESS pdPa = MmGetPhysicalAddress(&t->Pd[i][0]);
        t->Pdpt[i].AsUInt64 = 0;
        t->Pdpt[i].Fields.Present = 1;
        t->Pdpt[i].Fields.Write = 1;
        t->Pdpt[i].Fields.User = 1;
        t->Pdpt[i].Fields.PageFrameNumber = (UINT64)(pdPa.QuadPart >> PAGE_SHIFT);

        /* PD: 512 x 2 MiB large pages, identity mapped. */
        for (ULONG j = 0; j < NPT_PDE_COUNT; j++) {
            UINT64 pfn2mb = ((UINT64)i * NPT_PDE_COUNT) + j; /* index of 2 MiB page */
            t->Pd[i][j].AsUInt64 = 0;
            t->Pd[i][j].Fields.Present = 1;
            t->Pd[i][j].Fields.Write = 1;
            t->Pd[i][j].Fields.User = 1;
            t->Pd[i][j].Fields.PageSize = 1;
            /* PFN field here is the 4 KiB frame number of the 2 MiB page base. */
            t->Pd[i][j].Fields.PageFrameNumber = pfn2mb * (0x200000 / PAGE_SIZE);
        }
    }

    *OutTables = t;
    *OutPml4Pa = MmGetPhysicalAddress(&t->Pml4[0]);
    HVR_INFO("NPT identity map built: PML4 PA=0x%llx", (UINT64)OutPml4Pa->QuadPart);
    return STATUS_SUCCESS;
}
