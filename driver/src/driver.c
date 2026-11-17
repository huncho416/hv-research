/*
 * driver.c - DriverEntry, control device, and IOCTL dispatch.
 *
 * Research/educational driver. Creates \\Device\\hvresearch and services the
 * IOCTLs defined in ioctl.h. It virtualizes the machine with AMD SVM for the
 * purpose of MEASURING how Windows/VBS/HVCI react to memory operations from a
 * VTL0 hypervisor. It contains no signing bypass and targets only Windows
 * itself on a dedicated research VM.
 */
#include "svm.h"
#include "log.h"

HVR_CONTEXT g_Hvr = {0};

static PDEVICE_OBJECT g_DeviceObject = NULL;
static UNICODE_STRING g_SymLink;

/* Implemented in experiment_rw.c */
NTSTATUS HvrRunExperiment(_In_ PHVR_EXPERIMENT_REQUEST Req,
                          _Out_ PHVR_EXPERIMENT_RESULT Res);

static NTSTATUS HvrFillStats(PHVR_STATS_RESULT out, ULONG outLen, PULONG written)
{
    ULONG count;

    *written = 0;
    if (outLen < sizeof(HVR_STATS_RESULT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlZeroMemory(out, sizeof(*out));
    out->StructSize = sizeof(*out);

    count = g_Hvr.VpCount;
    if (count > RTL_NUMBER_OF(out->Cpu)) {
        count = RTL_NUMBER_OF(out->Cpu);
    }
    out->CpuCount = count;

    for (ULONG i = 0; i < count; i++) {
        PVIRTUAL_PROCESSOR_DATA vp = g_Hvr.VpArray ? g_Hvr.VpArray[i] : NULL;
        out->Cpu[i].CpuIndex = i;
        if (vp) {
            out->Cpu[i].Virtualized = (UINT32)vp->Virtualized;
            out->Cpu[i].ExitCpuid   = (UINT64)vp->ExitCpuid;
            out->Cpu[i].ExitMsr     = (UINT64)vp->ExitMsr;
            out->Cpu[i].ExitNpf     = (UINT64)vp->ExitNpf;
            out->Cpu[i].ExitVmmcall = (UINT64)vp->ExitVmmcall;
            out->Cpu[i].ExitOther   = (UINT64)vp->ExitOther;
            out->Cpu[i].LastExitCode= (UINT64)vp->LastExitCode;
        }
    }
    *written = sizeof(*out);
    return STATUS_SUCCESS;
}

static NTSTATUS HvrDispatchDeviceControl(PDEVICE_OBJECT dev, PIRP irp)
{
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info = 0;
    PVOID buf = irp->AssociatedIrp.SystemBuffer;
    ULONG inLen = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;

    UNREFERENCED_PARAMETER(dev);

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_HVR_PROBE:
        if (outLen >= sizeof(HVR_PROBE_RESULT)) {
            HvrProbe((PHVR_PROBE_RESULT)buf);
            info = sizeof(HVR_PROBE_RESULT);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    case IOCTL_HVR_VIRTUALIZE:
        status = HvrVirtualizeAllProcessors();
        break;

    case IOCTL_HVR_DEVIRTUALIZE:
        status = HvrDevirtualizeAllProcessors();
        break;

    case IOCTL_HVR_GET_STATS:
        status = HvrFillStats((PHVR_STATS_RESULT)buf, outLen, &info);
        break;

    case IOCTL_HVR_RUN_EXPERIMENT:
        if (inLen >= sizeof(HVR_EXPERIMENT_REQUEST) &&
            outLen >= sizeof(HVR_EXPERIMENT_RESULT)) {
            status = HvrRunExperiment((PHVR_EXPERIMENT_REQUEST)buf,
                                      (PHVR_EXPERIMENT_RESULT)buf);
            info = sizeof(HVR_EXPERIMENT_RESULT);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    default:
        break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS HvrDispatchCreateClose(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
static void HvrUnload(PDRIVER_OBJECT drv)
{
    UNREFERENCED_PARAMETER(drv);
    HVR_INFO("unload: devirtualizing");

    if (g_Hvr.Virtualized) {
        HvrDevirtualizeAllProcessors();
    }
    IoDeleteSymbolicLink(&g_SymLink);
    if (g_DeviceObject) {
        IoDeleteDevice(g_DeviceObject);
    }
    HVR_INFO("unloaded");
}

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg)
{
    NTSTATUS status;
    UNICODE_STRING devName;

    UNREFERENCED_PARAMETER(reg);
    HVR_INFO("DriverEntry (hv-research, educational SVM research driver)");

    RtlInitUnicodeString(&devName, HVR_DEVICE_NAME);
    RtlInitUnicodeString(&g_SymLink, HVR_SYMLINK_NAME);

    status = IoCreateDevice(drv, 0, &devName, HVR_DEVICE_TYPE,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        HVR_ERR("IoCreateDevice failed 0x%08x", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLink, &devName);
    if (!NT_SUCCESS(status)) {
        HVR_ERR("IoCreateSymbolicLink failed 0x%08x", status);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    drv->MajorFunction[IRP_MJ_CREATE] = HvrDispatchCreateClose;
    drv->MajorFunction[IRP_MJ_CLOSE] = HvrDispatchCreateClose;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HvrDispatchDeviceControl;
    drv->DriverUnload = HvrUnload;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;

    HVR_INFO("device \\Device\\hvresearch ready");
    return STATUS_SUCCESS;
}
