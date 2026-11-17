/*
 * npt.h - AMD Nested Page Table (NPT) construction.
 *
 * We build a 1:1 (identity) nested mapping of guest-physical -> system-physical
 * so the guest runs exactly as before, but every guest-physical access is now
 * subject to a second translation we control. Experiment E4 splits individual
 * entries to demonstrate exec/read separation on OUR OWN page.
 *
 * This is the mechanism HVCI uses one privilege level above us. Studying it
 * here is how we explain, in the writeup, why our nested NPT cannot override
 * the root hypervisor's SLAT.
 */
#pragma once
#include <ntddk.h>

/* 2 MiB large-page identity map keeps the table small. The guest physical
 * address space we cover is bounded by the highest RAM run reported by the
 * memory manager, rounded up. */
#define NPT_PML4E_COUNT   512
#define NPT_PDPTE_COUNT   512
#define NPT_PDE_COUNT     512

typedef union _NPT_PML4E
{
    UINT64 AsUInt64;
    struct
    {
        UINT64 Present : 1;
        UINT64 Write : 1;
        UINT64 User : 1;
        UINT64 WriteThrough : 1;
        UINT64 CacheDisable : 1;
        UINT64 Accessed : 1;
        UINT64 Reserved1 : 3;
        UINT64 Avl : 3;
        UINT64 PageFrameNumber : 40;
        UINT64 Reserved2 : 11;
        UINT64 NoExecute : 1;
    } Fields;
} NPT_PML4E, *PNPT_PML4E;

typedef union _NPT_PDPTE
{
    UINT64 AsUInt64;
    struct
    {
        UINT64 Present : 1;
        UINT64 Write : 1;
        UINT64 User : 1;
        UINT64 WriteThrough : 1;
        UINT64 CacheDisable : 1;
        UINT64 Accessed : 1;
        UINT64 Reserved1 : 3;
        UINT64 Avl : 3;
        UINT64 PageFrameNumber : 40;
        UINT64 Reserved2 : 11;
        UINT64 NoExecute : 1;
    } Fields;
} NPT_PDPTE, *PNPT_PDPTE;

/* 2 MiB page directory entry (PDE.PageSize = 1). */
typedef union _NPT_PDE_LARGE
{
    UINT64 AsUInt64;
    struct
    {
        UINT64 Present : 1;
        UINT64 Write : 1;
        UINT64 User : 1;
        UINT64 WriteThrough : 1;
        UINT64 CacheDisable : 1;
        UINT64 Accessed : 1;
        UINT64 Dirty : 1;
        UINT64 PageSize : 1;     /* must be 1 for a 2 MiB page */
        UINT64 Global : 1;
        UINT64 Avl : 3;
        UINT64 Pat : 1;
        UINT64 Reserved1 : 8;
        UINT64 PageFrameNumber : 31;
        UINT64 Reserved2 : 11;
        UINT64 NoExecute : 1;
    } Fields;
} NPT_PDE_LARGE, *PNPT_PDE_LARGE;

/* One PML4 page, one PDPT page, and NPT_PDPTE_COUNT PD pages = a full 512 GiB
 * identity map with 2 MiB leaves. */
typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _NPT_TABLES
{
    NPT_PML4E Pml4[NPT_PML4E_COUNT];
    NPT_PDPTE Pdpt[NPT_PDPTE_COUNT];
    DECLSPEC_ALIGN(PAGE_SIZE) NPT_PDE_LARGE Pd[NPT_PDPTE_COUNT][NPT_PDE_COUNT];
} NPT_TABLES, *PNPT_TABLES;

NTSTATUS HvrBuildIdentityNpt(_Out_ PNPT_TABLES* OutTables,
                             _Out_ PHYSICAL_ADDRESS* OutPml4Pa);
void HvrFreeNpt(_In_opt_ PNPT_TABLES Tables);
