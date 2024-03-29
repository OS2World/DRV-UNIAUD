; $Id: begin.asm,v 1.3 2002/03/18 19:19:13 sandervl Exp $ 
;*
;* Labels to mark start of code & data segments
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

	.386p

	include segments.inc

;Label to mark start of 32 bits code section

FIRSTCODE32 segment
        public __OffBeginCS32
__OffBeginCS32 label byte
FIRSTCODE32 ends

;Label to mark start of 32 bits data section

BSS32 segment
    public  __OffBeginDS32
    __OffBeginDS32   dd 0
BSS32 ends

	end
