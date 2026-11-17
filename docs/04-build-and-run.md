# Build and run

## Prerequisites (build machine)

- Visual Studio 2022 (Desktop C++ workload).
- Windows Driver Kit (WDK) matching your SDK, plus the corresponding Windows SDK.
- Spectre-mitigated libs (the WDK driver config expects them).

## Build the driver

Open `driver/hvresearch.vcxproj` in VS 2022 (or use `msbuild`):

```
msbuild driver\hvresearch.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `driver\x64\Release\hvresearch.sys` (+ `.inf`). The MASM file `svm.asm`
is assembled automatically by the `<MASM>` item.

## Build the control tool

```
msbuild tools\hvctl\hvctl.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `tools\hvctl\x64\Release\hvctl.exe`.

## Provision the research VM

Run inside the dedicated research VM, elevated:

```
powershell -ExecutionPolicy Bypass -File scripts\provision-research-vm.ps1
```

This records the VBS/HVCI/Secure Boot/Hyper-V state to `captures\vm-config.txt`.
For Config A (the research target) leave VBS/HVCI ON. To load an unsigned build
you must choose a signing path yourself; if you use test-signing, pass
`-EnableTestSigning` and note that it relaxes Secure Boot (record it).

> Reminder: loading the driver on a fully locked clean machine is not something
> this project enables. Use a research VM whose signing policy you control.

## Run the protocol

```
powershell -ExecutionPolicy Bypass -File scripts\run-experiments.ps1 -TargetVa fffff80312345678 -ConfigLabel A
```

Pick `-TargetVa` from a kernel debugger (e.g. `x nt!NtCreateFile`) on the VM.
Run once with VBS/HVCI ON (`-ConfigLabel A`) and once OFF (`-ConfigLabel B`),
then compare the two transcripts in `captures\`.

## Debugging / capturing evidence

Attach a kernel debugger (WinDbg/kd) to the VM. The driver logs via
`DbgPrintEx(DPFLTR_IHVDRIVER_ID, ...)`. To see INFO logs:

```
ed nt!Kd_IHVDRIVER_Mask 0xF
```

Expect bugchecks during E3 under HVCI - that is a result. Keep a VM snapshot so
you can roll back between runs.
