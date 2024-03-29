/* $Id: util.cpp,v 1.7 2002/04/25 17:27:17 sandervl Exp $ */
/*
 * Replacement runtime routines for Watcom C++
 *
 * (C) 2000-2002 InnoTek Systemberatung GmbH
 * (C) 2000-2001 Sander van Leeuwen (sandervl@xs4all.nl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */


#include "rm.hpp"                      // Will include os2.h, etc.
#include <devhelp.h>
#include <devinfo.h>

#include <malloc.h>
#include <include.h>
#include <irqos2.h>

//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_2_undefed_cdtor_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_2_undefined_member_function_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_2_pure_error_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_2_undef_vfun_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_4_undefed_cdtor_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_4_undefined_member_function_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_4_pure_error_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
extern "C" void __wcpp_4_undef_vfun_(void)
{
   int3();
}
//*****************************************************************************
//*****************************************************************************
void* operator new(unsigned u)
{
#ifdef DEBUGHEAP
   return malloc(u, __FILE__, __LINE__);
#else
   return malloc(u);
#endif
}
//*****************************************************************************
//*****************************************************************************
void operator delete(void *p)
{
#ifdef DEBUGHEAP
   free(p, __FILE__, __LINE__);
#else
   free(p);
#endif
}
//*****************************************************************************
//*****************************************************************************
void *operator new(unsigned u, void near *p)
{
   return u ? p : NULL;
}
//*****************************************************************************
//*****************************************************************************
static  GINFO FAR48 *pGIS = 0;
//*****************************************************************************
//*****************************************************************************
ULONG os2gettimesec()
{
    APIRET rc;
    FARPTR16 p;

    if(pGIS == NULL) {
        // Build a pointer to the Global Information Segment.
        rc = DevGetDOSVar( DHGETDOSV_SYSINFOSEG, 0, (VOID NEAR *)&p );
        if (rc) {
            return 0;
        }
        SEL FAR48 *pSel = (SEL FAR48 *)MAKE_FARPTR32(p);
        pGIS = (GINFO FAR48 *)MAKE_FARPTR32((ULONG)(*pSel << 16));
    }
    return pGIS->Time;
}
//*****************************************************************************
//*****************************************************************************
ULONG os2gettimemsec()
{
    APIRET rc;
    FARPTR16 p;

    if(pGIS == NULL) {
        // Build a pointer to the Global Information Segment.
        rc = DevGetDOSVar( DHGETDOSV_SYSINFOSEG, 0, (VOID NEAR *)&p );
        if (rc) {
            return 0;
        }
        SEL FAR48 *pSel = (SEL FAR48 *)MAKE_FARPTR32(p);
        pGIS = (GINFO FAR48 *)MAKE_FARPTR32((ULONG)(*pSel << 16));
    }
    return pGIS->MilliSeconds;
}
//*****************************************************************************
//*****************************************************************************

