; $Id: startup.inc,v 1.8 2002/05/03 15:42:46 sandervl Exp $ 
;*
;* Assembly macro and constant definitions
;*
;* (C) 2000-2002 InnoTek Systemberatung GmbH
;* (C) 2000-2001 Sander van Leeuwen (sandervl@xs4all.nl)
;*
;* This program is free software; you can redistribute it and/or
;* modify it under the terms of the GNU General Public License as
;* published by the Free Software Foundation; either version 2 of
;* the License, or (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public
;* License along with this program; if not, write to the Free
;* Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
;* USA.
;*


; MACRO'S
;*******************************************************************************
;enter a 16:32 far function
;*******************************************************************************
enter32         MACRO
		push	bp
		mov	bp, sp
		push	ds
		mov	ax, DATA16
		mov	ds, ax
		ENDM
;*******************************************************************************
;return to 16:32 far caller
;*******************************************************************************
ret32           MACRO   
		pop	ds
		pop	bp
	        db      66h                             ; Force next instruction 32-bit
        	ret
                ENDM

enter16         MACRO
		push	bp
		mov	bp, sp
		ENDM
ret16           MACRO
		pop	bp
		ret
		ENDM


IFDEF KEE

;;******************************************************************************
;;Need to preserve fs:ebx!! (all other registers must be saved by the caller)
;;******************************************************************************
DevThunkStackTo32 MACRO
	push	stacksel
	push	stackbase

	push	edx
	mov	edx, ss
	mov	stacksel, edx

        call	KernThunkStackTo32
	mov	stackbase, edx
	pop	edx	;trashed by KernThunkStackTo32
        xor     eax, eax ; mark success
        ENDM
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo32_Int MACRO
	push	eax
	push	edx
        call	KernThunkStackTo32
	pop	edx	;trashed by KernThunkStackTo32
	pop	eax	;trashed by KernThunkStackTo32
        ENDM
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo16 MACRO
        push    edx
	push	eax
        call	KernThunkStackTo16
	pop	eax	;trashed by KernThunkStackTo16
        pop     edx     ;trashed by KernThunkStackTo16 when called in interrupt context

	pop	stackbase
	pop	stacksel
        ENDM
        
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo16_Int MACRO
        push    edx
	push	eax
        call	KernThunkStackTo16
	pop	eax	;trashed by KernThunkStackTo16
        pop     edx     ;trashed by KernThunkStackTo16 when called in interrupt context
        ENDM
;;******************************************************************************
;;******************************************************************************

ELSE

;;******************************************************************************
;;Need to preserve fs:ebx!! (all other registers must be saved by the caller)
;;******************************************************************************
DevThunkStackTo32 MACRO
        LOCAL @@stackok, @@stackchangeend

;;;;        int     3
	
	pushfd
	cli
	pop	dword ptr [cpuflags]

        ;check if kernel DS selector DPL == 3; if so, change to 0        
	call    FixSelDPL

        ;allocate private stack
        push    fs
	call	StackAlloc
        pop     fs                             ;trashed by StackAlloc
        mov     dword ptr [fInitStack], 1

        cmp     eax, 0
        jne     @@stackok
	
IFDEF DEBUG
        int     3   ;this is very fatal
ENDIF
        call    RestoreSelDPL
        mov     eax, -1				;mark failure

	push	dword ptr [cpuflags]
	popfd
        jmp     short @@stackchangeend
        
@@stackok:
	;stack ptr in eax
	call    dword ptr [intSwitchStack]

	push	dword ptr [cpuflags]
	popfd

        mov     eax, 0                          ;mark success
@@stackchangeend:
        ENDM
        
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo32_Int MACRO
        LOCAL @@cont32

        cmp     dword ptr [fInitStack], 0
        je      @@cont32

;;;;        int     3
	pushfd
	cli
	pop	dword ptr [cpuflags]

	mov	dword ptr [tempeax], eax	;save eax

	pop	eax		;pop saved flat stack pointer

        ;and switch back to our flat stack again
	call    dword ptr [intSwitchStack]

	mov	eax, dword ptr [tempeax]	;restore eax
	push	dword ptr [cpuflags]
	popfd

@@cont32:
        ENDM
        
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo16 MACRO
;;;;        int     3

	pushfd
	cli
	pop	dword ptr [cpuflags]

	mov	dword ptr [tempeax], eax	;save eax
	mov	dword ptr [tempesi], esi	;save esi

	mov	esi, esp
	xor	eax, eax	;switch to 16 bits stack
	call    dword ptr [intSwitchStack]

	mov	eax, esi	
	call	StackFree

	call    RestoreSelDPL

	mov	eax, dword ptr [tempeax]
	mov	esi, dword ptr [tempesi]
               
	push	dword ptr [cpuflags]
	popfd
        
        ENDM
        
;;******************************************************************************
;;******************************************************************************
DevThunkStackTo16_Int MACRO
        LOCAL @@cont16

        cmp     dword ptr [fInitStack], 0
        je      @@cont16

;;;;        int     3
	pushfd
	cli
	pop	dword ptr [cpuflags]

	mov 	dword ptr [tempeax], eax	;save eax
	mov	dword ptr [tempesi], esi	;save esi
	mov	esi, esp
	xor	eax, eax	;switch to 16 bits stack
	call    dword ptr [intSwitchStack]
	push	esi		;flat stack ptr (popped before switching back to flat)

	mov	eax, dword ptr [tempeax]
	mov	esi, dword ptr [tempesi]
	push	dword ptr [cpuflags]
	popfd

@@cont16:
        ENDM

ENDIF ;KEE

IFDEF FLATSTACK        
;*******************************************************************************
;enter a function that needs to switch to a 16 bits stack
;*******************************************************************************
enterKEERM      MACRO
		push	ebp
		mov	ebp, esp
                push    ds
                push    es
                push    ebx
                push    ecx
                push    edx
                push    esi
                push    edi

                ;to access the parameters on the 32 bits stack, once we've switched
                mov     edi, ebp

	        DevThunkStackTo16_Int
                ENDM
;*******************************************************************************
;switch stack from 16 bits to 32 bits and return
;*******************************************************************************
retKEERM        MACRO
                DevThunkStackTo32_Int
                pop     edi
                pop     esi
                pop     edx
                pop     ecx
                pop     ebx
                pop     es
                pop     ds
		pop	ebp
		ret
                ENDM

;;******************************************************************************
;;******************************************************************************

ENDIF

;Constants and structure definitions

DevHlp_VirtToLin	EQU	5Bh
DevHlp_VMLock		EQU	55h

; Status word masks
STERR	EQU	8000H		; Bit 15 - Error
STINTER EQU	0400H		; Bit 10 - Interim character
STBUI	EQU	0200H		; Bit  9 - Busy
STDON	EQU	0100H		; Bit  8 - Done
STECODE EQU	00FFH		; Error code

; Definition of the request packet header.

reqPacket       struc
reqLenght       db ?
reqUnit         db ?
reqCommand      db ?
reqStatus       dw ?
reqFlags        db ?
                db 3 dup (?)    ; Reserved field
reqLink         dd ?
reqPacket       ends

rpInitIn        struc
i_rph           db size reqPacket dup (?)
i_unit          db ?
i_devHelp       dd ?
i_initArgs      dd ?
i_driveNum      db ?
rpInitIn        ends

rpInitOut       struc
o_rph           db size reqPacket dup (?)
o_unit          db ?
o_codeend	dw ?
o_dataend	dw ?
o_bpbarray	dd ?
o_status	dw ?
rpInitOut       ends


; definitions for 16 bits resource manager buffers
; (also in rm.hpp!!)
MAXSIZE_RMNodeData     EQU 1024
MAXSIZE_RMResources    EQU 128
MAXSIZE_RMHandleList   EQU 128
MAXSIZE_RMResourceList EQU 256
