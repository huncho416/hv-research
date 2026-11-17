; svm.asm - AMD SVM assembly glue for hv-research.
;
; Contains:
;   HvrLaunchVm             - the VMRUN loop and the orderly teardown path
;   HvrIsHypervisorInstalled- CPUID signature probe used during bring-up
;   HvrGetGdtBase           - sgdt wrapper
;   HvrGetCs/Ss/Ds/Es/Fs/Gs/Tr/Ldtr - segment selector readers
;
; The VMRUN loop reads a 6-pointer block at the host stack top (see svm.h,
; VIRTUAL_PROCESSOR_DATA.HostStack.Layout). RSP is set to &GuestVmcbPa so:
;   [rsp+00h] = GuestVmcbPa
;   [rsp+08h] = HostVmcbPa

.code

extern HvrHandleVmExit:proc

; ---- register save/restore macros (order matches GUEST_REGISTERS) --------
PUSHAQ macro
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    -1          ; placeholder for the RSP slot (+0x58)
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
endm

POPAQ macro
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    add     rsp, 8      ; skip the RSP placeholder slot
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
endm

; void HvrLaunchVm(void* HostRsp);   rcx = &GuestVmcbPa (host stack top)
HvrLaunchVm proc frame
    .endprolog
    mov     rsp, rcx                ; switch to the per-CPU host stack top

@LaunchLoop:
    mov     rax, qword ptr [rsp]    ; rax = GuestVmcbPa
    vmload  rax                     ; load guest extra state (FS/GS/TR/LDTR/...)
    vmrun   rax                     ; run guest until #VMEXIT (returns here)
    vmsave  rax                     ; save guest extra state back to VMCB

    mov     rax, qword ptr [rsp+8]  ; rax = HostVmcbPa
    vmload  rax                     ; restore host extra state for the handler

    PUSHAQ                          ; build GUEST_REGISTERS image on host stack

    mov     rdx, rsp                ; arg2 = PGUEST_REGISTERS
    ; arg1 = PVIRTUAL_PROCESSOR_DATA (Self) located at [stacktop + 0x10].
    ; stacktop = rsp + 80h (size of PUSHAQ image); Self is at +10h past it.
    mov     rcx, qword ptr [rsp+80h+10h]
    sub     rsp, 20h                ; home space for x64 call
    call    HvrHandleVmExit
    add     rsp, 20h

    test    al, al
    jnz     @ExitVm

    POPAQ
    jmp     @LaunchLoop

@ExitVm:
    ; Orderly teardown on this CPU. The handler has placed into the pushed
    ; image: Rbx(+60h)=resumeRIP, Rdx(+68h)=RFLAGS, Rcx(+70h)=guestRSP.
    ; GuestVmcbPa is at [rsp+80h] (just past the PUSHAQ image).
    mov     rax, qword ptr [rsp+80h]
    vmload  rax                     ; restore GUEST extra state for resume
    stgi                            ; re-enable global interrupts

    ; disable EFER.SVME
    mov     ecx, 0C0000080h         ; IA32_EFER
    rdmsr
    and     eax, 0FFFFEFFFh         ; clear bit 12 (SVME)
    wrmsr

    ; stage a return frame on the guest stack: [gsp-8]=RIP, [gsp-16]=RFLAGS
    mov     rax, qword ptr [rsp+70h] ; guest RSP
    mov     rbx, qword ptr [rsp+60h] ; resume RIP
    mov     rcx, qword ptr [rsp+68h] ; RFLAGS
    sub     rax, 8
    mov     qword ptr [rax], rbx
    sub     rax, 8
    mov     qword ptr [rax], rcx
    mov     qword ptr [rsp+70h], rax ; stash adjusted guest RSP into Rcx slot

    POPAQ                           ; restore guest GPRs; rcx <- adjusted RSP
    mov     rsp, rcx
    popfq                           ; restore guest RFLAGS
    ret                             ; resume guest at RIP (now de-virtualized)
HvrLaunchVm endp

; BOOLEAN HvrIsHypervisorInstalled(void);
HvrIsHypervisorInstalled proc
    push    rbx
    mov     eax, 40000100h          ; HVR_CPUID_SIGNATURE
    xor     ecx, ecx
    cpuid
    xor     eax, eax
    cmp     ebx, 72767348h          ; 'Hvsr'
    jne     @done
    mov     eax, 1
@done:
    pop     rbx
    ret
HvrIsHypervisorInstalled endp

; void HvrGetGdtBase(void* Descriptor);  rcx -> PSEUDO_DESCRIPTOR (limit+base)
HvrGetGdtBase proc
    sgdt    fword ptr [rcx]
    ret
HvrGetGdtBase endp

HvrGetCs proc
    mov     ax, cs
    ret
HvrGetCs endp

HvrGetSs proc
    mov     ax, ss
    ret
HvrGetSs endp

HvrGetDs proc
    mov     ax, ds
    ret
HvrGetDs endp

HvrGetEs proc
    mov     ax, es
    ret
HvrGetEs endp

HvrGetFs proc
    mov     ax, fs
    ret
HvrGetFs endp

HvrGetGs proc
    mov     ax, gs
    ret
HvrGetGs endp

HvrGetTr proc
    str     ax
    ret
HvrGetTr endp

HvrGetLdtr proc
    sldt    ax
    ret
HvrGetLdtr endp

    end
