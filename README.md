# hv-research

An **AMD SVM (AMD-V) research hypervisor** and instrumentation harness for studying how
Windows 11 (25H2, AMD) virtualization-based security reacts to a guest-level hypervisor
performing memory **read/write** and code-hooking from VTL0.

This is an **educational / defensive security research** project. The goal is to *observe
and document the security boundary*, not to defeat it. The single most important finding
this project is designed to demonstrate is:

> A hypervisor you load from inside Windows (VTL0) runs **below** VBS/HVCI in privilege.
> With VBS on, Microsoft Hyper-V is already the Type-1 root hypervisor and owns the real
> AMD nested page tables (NPT/SLAT) in the root partition (VTL1). Any hypervisor you start
> from the OS runs **nested** and controls only a *second-level* translation. It therefore
> cannot subvert the code-integrity and memory protections that live above it. VBS works by
> moving security policy to a higher privilege than any VTL0 code — including a VTL0
> hypervisor — can reach.

The project lets you reproduce and measure that boundary on real hardware.

## Scope and intent

- **In scope:** AMD SVM virtualization internals, nested virtualization under Hyper-V,
  NPT/SLAT mechanics, `#VMEXIT` instrumentation, VBS/HVCI detection, and *measuring* what
  happens when a VTL0 hypervisor tries to read/write/hook protected kernel memory.
- **Out of scope / explicitly not built here:** anti-cheat evasion, BYOVD exploit chains,
  ready-to-deploy bypasses of production protections, or any method to load unsigned code
  on a locked clean machine. Those are the *attacker* side; this repo studies the
  *defender* side.

## Target platform

- Windows 11 25H2, **AMD** CPU with SVM + NPT (Zen family).
- Two supported research configurations:
  1. **VBS/HVCI ON** (the interesting case) — the hypervisor runs *nested* under Hyper-V.
     Requires AMD nested virtualization exposed to the research VM/partition.
  2. **VBS/HVCI OFF** (baseline) — the hypervisor runs directly on bare metal. Used only to
     establish a control/baseline for comparison; not the research target.

## Loading the driver (read this first)

A hypervisor needs a kernel driver to initialize SVM. On a clean Secure Boot + HVCI system
you cannot load unsigned kernel code — that restriction *is* one of the protections under
study. Legitimate ways to run this on your **dedicated research VM**:

- Test-signing mode / debug policy (note: this relaxes Secure Boot — document it as part of
  the experiment configuration), or
- A properly signed driver (attestation/EV), or
- A kernel-debugger-attached research box.

This repo does **not** contain any technique to bypass driver signing. How you provision the
research VM's code-signing policy is left to you and should be recorded in each experiment's
configuration log (`docs/03-experiments.md`).

## Repository layout

```
docs/      Research notes, architecture, and the experiment protocol (the cert material)
driver/    The WDK kernel driver + AMD SVM hypervisor source
tools/     Host-side control/log-collection utilities
scripts/   Build and research-VM provisioning helpers
```

## Credits / prior art

The SVM core is modeled on well-known public MIT-licensed research hypervisors, chiefly
**SimpleSvm** and **SimpleSvmHook** by Satoshi Tanda, and concepts from **hvpp** (Petr
Beneš) and **Gbhv**. See `LICENSE` and inline attribution.

## Status

Early scaffold. See `docs/00-roadmap.md` for the build order and what is implemented.
