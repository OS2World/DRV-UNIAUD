/* $Id: rm.cpp,v 1.22 2002/06/13 10:07:50 sandervl Exp $ */
/*
 * OS/2 Resource Manager C++ interface
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
#include <string.h>              
#include <dbgos2.h>
#include <version.h>
#include <osspci.h>
#include <linux\pci.h>
#include "pciids.h"

/**@external LDev_Resources::isEmpty
 *  Returns TRUE iff the LDev_Resources structure has no information.
 * @param None.
 * @return BOOL
 */
BOOL LDev_Resources::isEmpty()
{
   BOOL bIsEmpty = TRUE;

   for ( int i=0; i<MAX_ISA_Dev_IO; ++i) {
      if (uIOBase[i] != NoIOValue)
         bIsEmpty = FALSE;
      if ((i < MAX_ISA_Dev_IRQ) && (uIRQLevel[i] != NoIOValue))
         bIsEmpty = FALSE;
      if ((i < MAX_ISA_Dev_DMA) && (uDMAChannel[i] != NoIOValue))
         bIsEmpty = FALSE;
      if ((i < MAX_ISA_Dev_MEM) && (uMemBase[i] != 0xffffffff))
         bIsEmpty = FALSE;
   }
   return bIsEmpty;
}


/**@external LDev_Resources::vClear
 *  Set an LDev_Resources structure to Empty.
 * @param None.
 * @return VOID
 */
void LDev_Resources::vClear()
{
   memset( (PVOID) this, NoIOValue, sizeof(LDev_Resources) );
}


/*
 * --- Linkages required by system Resource Manager (rm.lib).
 */

extern "C" PFN    RM_Help;
extern "C" PFN    RM_Help0;
extern "C" PFN    RM_Help3;
extern "C" ULONG  RMFlags;

/*
 * --- Public member functions.
 */

/**@external ResourceManager
 *  Constructor for RM object.
 * @notes Creates a "driver" node for this device driver, but does not
 *  allocate resources.
 */
ResourceManager::ResourceManager() :
           busnr(0), devnr(0), funcnr(0), devfn(0), DevID(0), idxRes(0)
{
   APIRET rc;
   DRIVERSTRUCT DriverStruct;
   char DriverName[sizeof(RM_DRIVER_NAME)];
   char VendorName[sizeof(RM_DRIVER_VENDORNAME)];
   char DriverDesc[sizeof(RM_DRIVER_DESCRIPTION)];
   FARPTR16 p;
   GINFO FAR *pGIS = 0;
   HDRIVER hDriver;

   /* Warp version level, bus type, machine ID, and much other information is
    * readily available.  Reference the RM ADD sample in the DDK for code.
    *
    * Create a "driver" struct for this driver.  This must ALWAYS be the
    * first true RM call executed, as it attaches the Resource Manager
    * library and performs setup for the other RM calls.
    */
   memset( (PVOID) &DriverStruct, 0, sizeof(DriverStruct) );

   //copy strings to stack, because we need to give RM 16:16 pointers
   //(which can only be (easily) generated from 32 bits stack addresses)
   strcpy(DriverName, RM_DRIVER_NAME);
   strcpy(VendorName, RM_DRIVER_VENDORNAME);
   strcpy(DriverDesc, RM_DRIVER_DESCRIPTION);

   DriverStruct.DrvrName     = FlatToSel((ULONG)DriverName);        /* ### IHV */
   DriverStruct.DrvrDescript = FlatToSel((ULONG)DriverDesc);        /* ### IHV */
   DriverStruct.VendorName   = FlatToSel((ULONG)VendorName);        /* ### IHV */
   DriverStruct.MajorVer     = CMVERSION_MAJOR;          //rmbase.h /* ### IHV */
   DriverStruct.MinorVer     = CMVERSION_MINOR;          //rmbase.h /* ### IHV */
   DriverStruct.Date.Year    = RM_DRIVER_BUILDYEAR;                    /* ### IHV */
   DriverStruct.Date.Month   = RM_DRIVER_BUILDMONTH;                   /* ### IHV */
   DriverStruct.Date.Day     = RM_DRIVER_BUILDDAY;                     /* ### IHV */
   DriverStruct.DrvrType     = DRT_AUDIO;
   DriverStruct.DrvrSubType  = 0;
   DriverStruct.DrvrCallback = NULL;
   //hDriver must be used as FlatToSel only works for stack variables
   rc = RMCreateDriver( FlatToSel((ULONG)&DriverStruct), FlatToSel((ULONG)&hDriver) );
   if( rc == RMRC_SUCCESS ) {
      _state = rmDriverCreated;
      _hDriver = hDriver;
   }
   else {
      _state = rmDriverFailed;
      _hDriver = 0;
   }

   // Build a pointer to the Global Information Segment.
   rc = DevGetDOSVar( DHGETDOSV_SYSINFOSEG, 0, (VOID NEAR *)&p );
   if (rc) {
      _rmDetection = FALSE;
   }
   else {
      SEL FAR48 *pSel = (SEL FAR48 *)MAKE_FARPTR32(p);
      pGIS = (GINFO FAR *)MAKE_FARPTR32((ULONG)(*pSel << 16));
      _rmDetection =
         ( (pGIS->MajorVersion > 20) ||
           ((pGIS->MajorVersion == 20) && (pGIS->MinorVersion > 30)) );
   }
   detectedResources.vClear();
   resources.vClear();
}

#pragma off (unreferenced)
bool ResourceManager::bIsDevDetected( DEVID DevID , ULONG ulSearchFlags, bool fPciDevice)
#pragma on (unreferenced)
/*
;  PURPOSE: Search the Resource Manager's "current detected" tree for
;           the matching PnP ID.
;
;  IN:    - DevID - PnP Device ID being sought (Compressed Ascii).
;         - ulSearchFlags - Search flags, ref rmbase.h SEARCH_ID_*;  also
;           documented as SearchFlags parm in PDD RM API RMDevIDToHandleList().
;           Defines whether DevID is a Device ID, Logical device ID, Compatible
;           ID, or Vendor ID.
;
;  OUT:     Boolean indicator, TRUE when number of matching detected devices > 0.
;
*/
{
#ifdef MANUAL_PCI_DETECTION
   if(getPCIConfiguration(DevID) == FALSE) {
      return FALSE;
   }

   this->DevID = DevID;

   //Manual detection in ResourceManager class constructor; 
   return (_state == rmDriverCreated || _state == rmAdapterCreated);
#else
   BOOL bReturn = FALSE;
   LPHANDLELIST pHandleList = 0;

   if ( ! _rmDetection )
      bReturn = TRUE;
   else {
      pHandleList = _DevIDToHandleList( DevID, ulSearchFlags, fPciDevice);
                                       // Array of RM handles for the detected
                                       // devices that match the PnP device ID(s).
      bReturn = (pHandleList->cHandles != 0);
                                       // If the size of the array != 0, we found the device.
   }

   return bReturn ;
#endif
}



/**@internal GetRMDetectedResources
 *  Return the set of IO ports, IRQ levels, DMA channels, & memory ranges
 *  required by the specified device, as detected by the OS/2 resource
 *  manager.
 * @param Refer to _bIsDevDetected() for parameters.
 * @notes It's expectded that the spec'd DevID & flags will select a single
 *  device in the system.  If multiples are found, the first matching device
 *  is referenced.
 * @return LDev_Resources* object, filled in with required resources.  Object
 *  is allocated from heap, is responsibility of caller to free object.
 *  Ordering on the resources is preserved.   Unused fields are set to NoIOValue.
 * @return NULL on error situations.
 */
#pragma off (unreferenced)
BOOL ResourceManager::GetRMDetectedResources ( DEVID DevID , ULONG ulSearchFlags, bool fPciDevice)
#pragma on (unreferenced)
{
#ifdef MANUAL_PCI_DETECTION
   //Fill in resources read from PCI Configuration space
   detectedResources.uIRQLevel[0]  = pciConfigData->InterruptLine;
   if(detectedResources.uIRQLevel[0] == 0 || detectedResources.uIRQLevel[0] > 15)  {
	    dprintf(("Invalid PCI irq %x", (int)detectedResources.uIRQLevel[0]));
	    DebugInt3();
       	return FALSE;
   }

   int idxmem = 0, idxio = 0;
   for(int i=0;i<MAX_PCI_BASE_ADDRESS;i++) 
   {
       if(pciConfigData->Bar[i] != -1) 
       {
           ULONG l, barsize;
           int reg = PCI_BASE_ADDRESS_0 + (i << 2);
           
           pci_read_config_dword(reg, &l);
           if (l == 0xffffffff)
               return 0;

           pci_write_config_dword(reg, ~0);
           pci_read_config_dword(reg, &barsize);
           pci_write_config_dword(reg, l);

           if (!barsize || barsize == 0xffffffff)
               continue;

           if ((l & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY) {
               barsize = ~(barsize & PCI_BASE_ADDRESS_MEM_MASK);
               detectedResources.uMemBase[idxmem]     = (pciConfigData->Bar[i] & 0xFFFFFFF0);
               detectedResources.uMemLength[idxmem++] = barsize + 1;
           }
           else {
               barsize = ~(barsize & PCI_BASE_ADDRESS_IO_MASK) & 0xffff;
               detectedResources.uIOBase[idxio]     = (USHORT)(pciConfigData->Bar[i] & 0xFFFFFFF0);
               detectedResources.uIOLength[idxio++] = (USHORT)barsize + 1;
           }
       }
   }
   return TRUE;
#else
   LDev_Resources* pResources = 0;     // Used to return result.
   LPRM_GETNODE_DATA pNode = 0;        // Node resource data for spec'd DEVID's.
   LPRESOURCELIST pResourceList = 0;   // Resource list contained within Node data.
   int indexIO = 0;                    // Number of IO, IRQ, etc. requested.
   int indexIRQ = 0;
   int indexDMA = 0;
   int indexMEM = 0;
   int i;

   pResources = new LDev_Resources();
   if (!pResources) goto error_cleanup;
   pResources->vClear();

   // Get resources list from system RM.  Returned pNode should have
   // pNode->NodeType equal to RMTYPE_DETECTED (=5).
   pNode = _DevIDToNodeData( DevID, ulSearchFlags, fPciDevice );
   if ( !pNode ) goto error_cleanup;

   pResourceList = (LPRESOURCELIST) MAKE_FARPTR32(pNode->RMNode.pResourceList);
   if (! pResourceList) {
      goto error_cleanup;
   }
   if (pResourceList->Count > MAX_ResourceCount) {
      goto error_cleanup;
   }

   //--- Format resources into an LDev_Resource format.
   for (i=0; i < pResourceList->Count; ++i) {
      LPRESOURCESTRUCT pRes = &pResourceList->Resource[i];
                                       // Pointer to next resource in list.
      switch( pRes->ResourceType )  {
      case RS_TYPE_IO:
         pResources->uIOBase[ indexIO ] = pRes->IOResource.BaseIOPort;
         pResources->uIOLength[ indexIO ] = pRes->IOResource.NumIOPorts;
         ++indexIO;
         break;

      case RS_TYPE_IRQ:
         pResources->uIRQLevel[ indexIRQ ] = pRes->IRQResource.IRQLevel;
         ++indexIRQ;
         break;

      case RS_TYPE_DMA:
         pResources->uDMAChannel[ indexDMA ] = pRes->DMAResource.DMAChannel;
         ++indexDMA;
         break;

      case RS_TYPE_MEM:
         pResources->uMemBase[ indexMEM ] = pRes->MEMResource.MemBase;
         pResources->uMemLength[ indexMEM ] = pRes->MEMResource.MemSize;
         ++indexMEM;
         break;
      }
   }  /* end for loop through resource list. */

   return pResources;

error_cleanup:
   delete pResources;
   return NULL;
#endif
}


/**@external pGetDevResources
 *
 *  Allocate set of IO ports, IRQ levels, DMA channels requested by the
 *  specified device.
 *
 * @param Refer to bIsDevDetected()
 *
 * @return LDev_Resources object, filled in with required resources.  Object
 *  is returned on stack. Ordering on the resources is preserved.   Unused
 *  fields are set to 0xFF.
 *
 * @notes The allocation from OS/2's RM is required; if not performed, the
 *  resources could be allocated by a driver which loads after this one.
 *  Also perform other bookeepping by registering driver, adapter, and
 *  device with the system RM.
 *
 * @notes Additional comments from DevCon 'ADD' sample for RM: "Create all
 *  Resource Manager nodes required by this driver.  The Resource Manager
 *  structures created in this module may be thought of as being allocated
 *  in a seperate tree called the 'driver' tree.  The snooper resource nodes
 *  were created in the 'current detected' tree.  Therefore, these
 *  allocations will not compete with previous snooper resource allocations.
 *
 * @notes
 *
 *  - Fetch defaults for named device
 *     - Warp3:  GetSpecDefaults( pnpID )
 *     - (not implemented:)  snoop on Warp3 -> GetSnoopedResources()
 *     - Warp4:  GetLDev_Resources( pnpID ) (rename to GetRMDetectedResources())
 *     - @return LDev_Resources structure
 *  - Fill in any user overrides
 *     - object Override( pnpID ) subclasses an LDev_Resources
 *     - has an added "exists" flag, set true if any overrides exist
 *     - on creation, interacts with parsed information for overrides
 *     - bool Override.exist( pnpID )
 *     - LDev_Resources Override.apply( pnpID, LDev_Resources )
 *  - Format LDev_Resources into RESOURCELIST
 *     - pResourceList = MakeResourceList( LDev_Resources )
 *  - Allocate adapter if this is the 1st time through
 *     - RMCreateAdapter()
 *  - Allocate resources to device
 *     - Call GetDescriptiveName( pnpID ) if Warp3
 *     - Call RMCreateDevice() to allocate the resources
 *  - Add as last step:
 *     - if (Warp3 or any command line overrides) and
 *     - if resources are successfully allocated
 *     - then SLAM the chip with the allocated resources
 */

LDev_Resources* ResourceManager::pGetDevResources ( DEVID DevID , ULONG ulSearchFlags, bool fPciDevice)
{
    // Initialize resource object.  Use detected information if available,
    // otherwise use hardcoded defaults.
    if(GetRMDetectedResources( DevID, ulSearchFlags, fPciDevice) == FALSE) {
        return NULL;
    }
   
    if(_pahRMAllocDetectedResources() == FALSE) {
        _state = rmAllocFailed;
        return NULL;
    }
    return &detectedResources;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::registerResources()
{
    APIRET rc;

    //--- Here, we got all the resources we wanted.  Register adapter if not yet done.
    //### Problem here with mult adpaters, would need to cross ref PnP ID's to adapters created.
    if (_state != rmAdapterCreated) {
        rc = _rmCreateAdapter();
    }
    
    char *lpszDeviceName;

    switch(DevID) {
    case PCIID_CREATIVELABS_SBLIVE:
        lpszDeviceName = "Creative Labs SBLive!";
        break;
    case PCIID_VIA_686A:
        lpszDeviceName = "VIA VT82C686A";
        break;
    case PCIID_VIA_8233:
        lpszDeviceName = "VIA VT8233";
        break;
    case PCIID_ALS4000:
        lpszDeviceName = "Avance Logic ALS4000";
        break;
    case PCIID_CMEDIA_CM8338A:
        lpszDeviceName = "C-Media CMI8338A";
        break;
    case PCIID_CMEDIA_CM8338B:
        lpszDeviceName = "C-Media CMI8338B";
        break;
    case PCIID_CMEDIA_CM8738:
        lpszDeviceName = "C-Media CMI8738";
        break;
    case PCIID_CMEDIA_CM8738B:
        lpszDeviceName = "C-Media CMI8738B";
        break;
    case PCIID_INTEL_82801:
        lpszDeviceName = "Intel ICH 82801";
        break;
    case PCIID_INTEL_82901:
        lpszDeviceName = "Intel ICH 82901";
        break;
    case PCIID_INTEL_92801BA:
        lpszDeviceName = "Intel ICH 92801BA";
        break;
    case PCIID_INTEL_440MX:
        lpszDeviceName = "Intel ICH 440MX";
        break;
    case PCIID_INTEL_ICH3:
        lpszDeviceName = "Intel ICH 3";
        break;
    case PCIID_CIRRUS_4281:
        lpszDeviceName = "Cirrus Logic CS4281";
        break;
    case PCIID_ESS_ALLEGRO_1:
        lpszDeviceName = "ESS Allegro 1";
        break;
    case PCIID_ESS_ALLEGRO:
        lpszDeviceName = "ESS Allegro";
        break;
    case PCIID_ESS_MAESTRO3:
        lpszDeviceName = "ESS Maestro 3";
        break;
    case PCIID_ESS_MAESTRO3_1:
        lpszDeviceName = "ESS Maestro 3 (1)";
        break;
    case PCIID_ESS_MAESTRO3_HW:
        lpszDeviceName = "ESS Maestro 3 (HW)";
        break;
    case PCIID_ESS_MAESTRO3_2:
        lpszDeviceName = "ESS Maestro 3 (2)";
        break;

    default:
        DebugInt3();
        return FALSE;
    }

    _rmCreateDevice(lpszDeviceName);
    return TRUE;
}
//*****************************************************************************
//*****************************************************************************


/*
 * --- Private member functions.
 */


/**@internal _pahRMAllocDetectedResources
 *  Allocate a set of resources from OS/2 by interfacing with the
 *  OS/2 resource manager.
 * @param PRESOURCELIST pResourceList - list of resources to allocate.
 * @return TRUE  - on success
 * @return FALSE - on failure
 * @notes Logs appropriate errors in global error log if any allocation
 *  problem.
 * @notes Either all resources are allocated, or none.  If there is a
 *  failure within this function when some (but not all) resources are
 *  allocated, then any allocated resources are freed.
 */
BOOL ResourceManager::_pahRMAllocDetectedResources( )
{
    int j;

    for ( j=0; j<MAX_ISA_Dev_IO; ++j) {
        if (detectedResources.uIOBase[j] != NoIOValue) {
            if(requestIORange(detectedResources.uIOBase[j], detectedResources.uIOLength[j]) == FALSE) {
                return FALSE;
            }
        }
    }
    for ( j=0; j<MAX_ISA_Dev_IRQ; ++j) {
        if (detectedResources.uIRQLevel[j] != NoIOValue) {
            //shared irq is not necessarily true, but let's assume that for now
            if(requestIRQ(detectedResources.uIRQLevel[j], TRUE) == FALSE) {
                return FALSE;
            }
        }
    }
    for ( j=0; j<MAX_ISA_Dev_DMA; ++j) {
        if (detectedResources.uDMAChannel[j] != NoIOValue) {
            if(requestDMA(detectedResources.uDMAChannel[j]) == FALSE) {
                return FALSE;
            }
        }
    }

    for ( j=0; j<MAX_ISA_Dev_MEM; ++j) {
        if (detectedResources.uMemBase[j] != 0xffffffff) {
           if(requestMemRange(detectedResources.uMemBase[j], detectedResources.uMemLength[j]) == FALSE) {
               return FALSE;
           }
       }
    }
    return TRUE;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::isPartOfAllocatedResources(int type, ULONG ulBase, ULONG ulLength)
{
    int j;

    switch(type) {
    case RS_TYPE_IO:
        for ( j=0; j<MAX_ISA_Dev_IO; ++j) {
            if (resources.uIOBase[j] == ulBase && resources.uIOLength[j] == ulLength) {
                return TRUE;
            }
        }
        break;
    case RS_TYPE_DMA:
        for ( j=0; j<MAX_ISA_Dev_DMA; ++j) {
            if (resources.uDMAChannel[j] == ulBase) {
                return TRUE;
            }
        }
        break;
    case RS_TYPE_MEM:
        for ( j=0; j<MAX_ISA_Dev_MEM; ++j) {
            if (resources.uMemBase[j] == ulBase && resources.uMemLength[j] == ulLength) {
                return TRUE;
            }
        }
        break;
    case RS_TYPE_IRQ:
        for ( j=0; j<MAX_ISA_Dev_IRQ; ++j) {
            if (resources.uIRQLevel[j] == ulBase) {
                return TRUE;
            }
        }
        break;
    }
    return FALSE;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::requestIORange(ULONG ulBase, ULONG ulLength)
{
    RESOURCESTRUCT Resource;
    HRESOURCE      hres = 0;
    APIRET         rc;
    int            j;

    if(isPartOfAllocatedResources(RS_TYPE_IO, ulBase, ulLength)) {
        return TRUE;
    }

    memset(__Stack32ToFlat(&Resource), 0, sizeof(Resource));
    Resource.ResourceType          = RS_TYPE_IO;
    Resource.IOResource.BaseIOPort = (USHORT)ulBase;
    Resource.IOResource.NumIOPorts = (USHORT)ulLength;
    Resource.IOResource.IOFlags    = RS_IO_EXCLUSIVE;
    if (ulBase > 0x3ff) 
       Resource.IOResource.IOAddressLines = 16;
    else                                                  
       Resource.IOResource.IOAddressLines = 10;

    rc = RMAllocResource( _hDriver,                       // Handle to driver.
                          FlatToSel((ULONG)&hres),           // OUT:  "allocated" resource node handle
                          FlatToSel((ULONG)&Resource) );     // Resource to allocate.

    if (rc == RMRC_SUCCESS) {
        //insert into allocated resources array
        for ( j=0; j<MAX_ISA_Dev_IO; ++j) {
            if (resources.uIOBase[j] == NoIOValue) {
                resources.uIOBase[j]   = (USHORT)ulBase;
                resources.uIOLength[j] = (USHORT)ulLength;
                break;
            }
        }
        if(j != MAX_ISA_Dev_IO) {
            hResource[idxRes] = hres;
            idxRes++;
            return TRUE;
        }
        DebugInt3();
    }
    releaseAllResources();
    return FALSE;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::requestMemRange(ULONG ulBase, ULONG ulLength)
{
    RESOURCESTRUCT Resource;
    HRESOURCE      hres = 0;
    APIRET         rc;
    int            j;

    if(isPartOfAllocatedResources(RS_TYPE_MEM, ulBase, ulLength)) {
        return TRUE;
    }
    memset(__Stack32ToFlat(&Resource), 0, sizeof(Resource));
    Resource.ResourceType        = RS_TYPE_MEM;
    Resource.MEMResource.MemBase = ulBase;
    Resource.MEMResource.MemSize = ulLength;
    Resource.MEMResource.MemFlags= RS_MEM_EXCLUSIVE;

    rc = RMAllocResource( _hDriver,                       // Handle to driver.
                          FlatToSel((ULONG)&hres),           // OUT:  "allocated" resource node handle
                          FlatToSel((ULONG)&Resource) );     // Resource to allocate.

    if (rc == RMRC_SUCCESS) {
        //insert into allocated resources array
        for ( j=0; j<MAX_ISA_Dev_MEM; ++j) {
            if (resources.uMemBase[j] == 0xffffffff) {
                resources.uMemBase[j]   = ulBase;
                resources.uMemLength[j] = ulLength;
                break;
            }
        }
        if(j != MAX_ISA_Dev_MEM) {
            hResource[idxRes] = hres;
            idxRes++;
            return TRUE;
        }
        DebugInt3();
    }
    releaseAllResources();
    return FALSE;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::requestIRQ(USHORT usIRQ, BOOL fShared)
{
    RESOURCESTRUCT Resource;
    HRESOURCE      hres = 0;
    APIRET         rc;
    int            j;

    if(isPartOfAllocatedResources(RS_TYPE_IRQ, usIRQ, 0)) {
        return TRUE;
    }
    memset(__Stack32ToFlat(&Resource), 0, sizeof(Resource));
    Resource.ResourceType          = RS_TYPE_IRQ;
    Resource.IRQResource.IRQLevel  = usIRQ;
    Resource.IRQResource.IRQFlags  = (fShared) ? RS_IRQ_SHARED : RS_IRQ_EXCLUSIVE;
    Resource.IRQResource.PCIIrqPin = RS_PCI_INT_NONE;

    rc = RMAllocResource( _hDriver,                       // Handle to driver.
                          FlatToSel((ULONG)&hres),           // OUT:  "allocated" resource node handle
                          FlatToSel((ULONG)&Resource) );     // Resource to allocate.

    if (rc == RMRC_SUCCESS) {
        //insert into allocated resources array
        for ( j=0; j<MAX_ISA_Dev_IRQ; ++j) {
            if (resources.uIRQLevel[j] == NoIOValue) {
                resources.uIRQLevel[j] = usIRQ;
                break;
            }
        }
        if(j != MAX_ISA_Dev_IRQ) {
            hResource[idxRes] = hres;
            idxRes++;
            return TRUE;
        }
        DebugInt3();
    }
    releaseAllResources();
    return FALSE;
}
//*****************************************************************************
//*****************************************************************************
BOOL ResourceManager::requestDMA(USHORT usDMA)
{
    RESOURCESTRUCT Resource;
    HRESOURCE      hres = 0;
    APIRET         rc;
    int            j;

    if(isPartOfAllocatedResources(RS_TYPE_DMA, usDMA, 0)) {
        return TRUE;
    }
    memset(__Stack32ToFlat(&Resource), 0, sizeof(Resource));
    Resource.ResourceType           = RS_TYPE_DMA;
    Resource.DMAResource.DMAChannel = usDMA;
    Resource.DMAResource.DMAFlags   = RS_DMA_EXCLUSIVE;

    rc = RMAllocResource( _hDriver,                       // Handle to driver.
                          FlatToSel((ULONG)&hres),           // OUT:  "allocated" resource node handle
                          FlatToSel((ULONG)&Resource) );     // Resource to allocate.

    if (rc == RMRC_SUCCESS) {
        //insert into allocated resources array
        for ( j=0; j<MAX_ISA_Dev_DMA; ++j) {
            if (resources.uDMAChannel[j] == NoIOValue) {
                resources.uDMAChannel[j] = usDMA;
                break;
            }
        }
        if(j != MAX_ISA_Dev_DMA) {
            hResource[idxRes] = hres;
            idxRes++;
            return TRUE;
        }
        DebugInt3();
    }
    releaseAllResources();
    return FALSE;
}
//*****************************************************************************
//*****************************************************************************
void ResourceManager::releaseAllResources()
{
    int i;

    for(i=0;i<idxRes;i++) {
        RMDeallocResource(_hDriver, hResource[i]);
    }
    idxRes = 0;
}
//*****************************************************************************
//*****************************************************************************
/**@internal _rmCreateAdapter
 *  Create the "adapter" node.  The "adapter" node belongs to this driver's
 *  "driver" node.  Also as part of this operation, the "resource" nodes
 *  associated with this driver will be moved to the "adapter" node.
 * @param None.
 * @notes Changes state of the RM object to 'rmAdapterCreated'.
 * @return APIRET rc - 0 iff good creation.  Returns non-zero and logs a soft
 *  error on failure.
 */
APIRET ResourceManager::_rmCreateAdapter()
{
   APIRET rc;
   ADAPTERSTRUCT AdapterStruct;
   char AdapterName[sizeof(RM_ADAPTER_NAME)];
   HADAPTER hAdapter;

   if (_state != rmAdapterCreated) 
   {
      //copy string to stack, because we need to give RM 16:16 pointers
      //(which can only be (easily) generated from 32 bits stack addresses)
      strcpy(AdapterName, RM_ADAPTER_NAME);

      memset( (PVOID) &AdapterStruct, 0, sizeof(AdapterStruct) );
      AdapterStruct.AdaptDescriptName = FlatToSel((ULONG)AdapterName);        /* ### IHV */
      AdapterStruct.AdaptFlags        = AS_16MB_ADDRESS_LIMIT;    // AdaptFlags         /* ### IHV */
      AdapterStruct.BaseType          = AS_BASE_MMEDIA;           // BaseType
      AdapterStruct.SubType           = AS_SUB_MM_AUDIO;          // SubType
      AdapterStruct.InterfaceType     = AS_INTF_GENERIC;          // InterfaceType
      AdapterStruct.HostBusType       = AS_HOSTBUS_PCI;           // HostBusType        /* ### IHV */
      AdapterStruct.HostBusWidth      = AS_BUSWIDTH_32BIT;        // HostBusWidth       /* ### IHV */
      AdapterStruct.pAdjunctList      = NULL;                     // pAdjunctList       /* ### IHV */

      //--- Register adapter.  We'll record any error code, but won't fail
      // the driver initialization and won't return resources.
      //NOTE: hAdapter must be used as FlatToSel only works for stack variables
      rc = RMCreateAdapter( _hDriver,          // Handle to driver
                            FlatToSel((ULONG)&hAdapter),        // (OUT) Handle to adapter
                            FlatToSel((ULONG)&AdapterStruct),    // Adapter structure
                            NULL,              // Parent device (defaults OK)
                            NULL );            // Allocated resources.  We assign ownership
                                               // of the IO, IRQ, etc. resources to the
                                               // device (via RMCreateDevice()), not to the
                                               // adapter (as done in disk DD sample).
      if (rc == RMRC_SUCCESS) {
         _state = rmAdapterCreated;
         _hAdapter = hAdapter;
      }
   }
   return rc;
}

/**@internal _rmCreateDevice
 *  Create Device node in the OS/2 RM allocation tree.  Device nodes belong
 *  to the Adapter node, just like the Resource nodes.
 * @param PSZ pszName - Descriptive name of device.
 * @param LPAHRESOURCE pahResource - Handles of allocated resources that are
 *  owned by this device.
 * @return APIRET rc - Value returned by RMCreateDevice() call.
 * @notes Same "soft" error strategy as adapter registration: we'll record
 *  any errors but hold onto the resources and continue to operate the
 *  driver.
 * @notes
 */
APIRET ResourceManager::_rmCreateDevice( char *lpszDeviceName)
{
    DEVICESTRUCT DeviceStruct;
    HDEVICE      hDevice;
    char         szDeviceName[64];
    APIRET       rc;
    typedef struct _myhresource {
        ULONG     NumResource;
        HRESOURCE hResource[MAX_ResourceCount];      /*First Entry in Array of HRESOURCE */
    } MYAHRESOURCE;

    MYAHRESOURCE hres;

    hres.NumResource = idxRes;
    for(int i=0;i<idxRes;i++) {
        hres.hResource[i] = hResource[i];
    }
    //copy string to stack, because we need to give RM 16:16 pointers
    //(which can only be (easily) generated from 32 bits stack addresses)
    strncpy(szDeviceName, lpszDeviceName, sizeof(szDeviceName));

    memset( (PVOID) &DeviceStruct, 0, sizeof(DeviceStruct));
    DeviceStruct.DevDescriptName = FlatToSel((ULONG)szDeviceName);
    DeviceStruct.DevFlags        = DS_FIXED_LOGICALNAME;
    DeviceStruct.DevType         = DS_TYPE_AUDIO;
    DeviceStruct.pAdjunctList    = NULL;

    rc = RMCreateDevice(_hDriver,      // Handle to driver
                        FlatToSel((ULONG)&hDevice),      // (OUT) Handle to device, unused.
                        FlatToSel((ULONG)&DeviceStruct), // Device structure
                        _hAdapter,     // Parent adapter
                        FlatToSel((ULONG)&hres)); // Allocated resources
    return rc;
}
#ifndef MANUAL_PCI_DETECTION
LPHANDLELIST ResourceManager::_DevIDToHandleList ( DEVID DevID , ULONG ulSearchFlags, bool fPciDevice)
/*
;  PURPOSE: Search the Resource Manager's "current detected" tree for
;           the specified PnP ID, and return all matching RM handles.
;
;  IN:      Refer to bIsDevDetected()
;
;  OUT:     List of RM handles matching the search, in HandleList format (rmbase.h)
;           Info returned in heap memory, caller must ensure this is later freed.
*/
{
   APIRET rc;
   DEVID  DeviceID, FunctionID, CompatID, VendorID;

   //--- Stuff the search value into the appropriate vbl.  Need not initialize
   //    or zero out the other vbls, they won't be referenced during the search.
   switch (ulSearchFlags) {
   case SEARCH_ID_DEVICEID:
      DeviceID = DevID;
      break;
   case SEARCH_ID_FUNCTIONID:
      FunctionID = DevID;
      break;
   case SEARCH_ID_COMPATIBLEID:
      CompatID = DevID;
      break;
   case SEARCH_ID_VENDOR:
      VendorID = DevID;
      break;
   default:
      return NULL;
   }

   LPHANDLELIST pDevHandleList = (LPHANDLELIST)MAKE_FARPTR32(RMHandleList);

   // List of RM handles associated w/ Device ID.  Will normally
   // be a single handle for the single adapter or function found.

   pDevHandleList->cHandles = 0;                  // clear handle count
   pDevHandleList->cMaxHandles = MAX_DevID;       // set size dimension

   // Use the PnP ID to get a list of detected devices which used this ID,
   // by searching for all snooped devices in the CURRENT detected tree.
   rc = RMDevIDToHandleList((fPciDevice) ? RM_IDTYPE_PCI : RM_IDTYPE_EISA,           // input device IDs' format
                            DeviceID,                 // device (adapter) ID
                            FunctionID,               // logical device (function) ID
                            CompatID,                 // compatible ID
                            VendorID,                 // vendor ID
                            0,                        // serial number
                            ulSearchFlags,
                            HANDLE_CURRENT_DETECTED,
                            RMHandleList );         // place output here
   if (rc != RMRC_SUCCESS) {
      return NULL;
   }
   return pDevHandleList;
}


LPRM_GETNODE_DATA ResourceManager::_RMHandleToNodeData ( RMHANDLE rmHandle )
/*
;  PURPOSE: Return the list of resources requested by the device
;           represented by the resource manager handle.
;
;  IN:    - rmHandle - rmHandle representing the Device or Logical device of
;           interest.
;
;  OUT:     List of resources (GETNODE_DATA  format, rmbase.h, rmioctl.h), saved
;           in heap memory.  Caller must ensure this memory is later freed.
;
;  ALGORITHM:
;           1.  Call RMHandleToResourceHandleList to get a count of # of resources.
;           2.  Allocate heap memory for array of Resources.
;           3.  Construct resource list by one of the following methods
;               a. Call RMGetNodeInfo on each resource hangle (n calls to RM)
;       used->  b. Call RMGetNodeInfo with device handle (1 call to RM)
*/
{
   APIRET rc;

   //--- Fetch list of resource handles for this device handle.  We use the
   //    handle list only to get a count on the number of resources.
   char work[ sizeof(HANDLELIST) + (sizeof(RMHANDLE) * MAX_ResourceCount) ];
   NPHANDLELIST pResourceHandleList = (NPHANDLELIST) work;
                     // List of handles for IO, IRQ, DMA, etc. resources.

   pResourceHandleList->cHandles = 0;
   pResourceHandleList->cMaxHandles = MAX_ResourceCount;
   rc = RMHandleToResourceHandleList( rmHandle, FlatToSel((ULONG)pResourceHandleList) );
   if (rc != RMRC_SUCCESS) {
      return NULL;
   }

   //--- Allocate heap memory to hold complete list of resources for device.
   USHORT uNodeSize = sizeof(RM_GETNODE_DATA)
                      + sizeof(ADAPTERSTRUCT) + MAX_DescTextLen
                      + sizeof(DRIVERSTRUCT) + MAX_DescTextLen
                      + (sizeof(RESOURCESTRUCT) * pResourceHandleList->cHandles);

   if(uNodeSize > MAXSIZE_RMNodeData) {
       DebugInt3();
       return NULL;
   }

   LPRM_GETNODE_DATA pNodeData = (LPRM_GETNODE_DATA) MAKE_FARPTR32(RMNodeData);

   //--- Get resource info, use single call to GetNodeInfo on device handle.
   rc = RMGetNodeInfo( rmHandle, RMNodeData, uNodeSize );
   if (rc != RMRC_SUCCESS) {
      return NULL;
   }

   return pNodeData;             // Return getnode data.
}


LPRM_GETNODE_DATA ResourceManager::_DevIDToNodeData ( DEVID DevID , ULONG ulSearchFlags, bool fPciDevice )
/*
;  PURPOSE: Compose the functions
;                _DevIDToHandleList
;                _RMHandleToNodeData (applied to 1st RM handle in handle list)
;
;  IN:      Refer to bIsDevDetected()
;  OUT:     Refer to _RMHandleToNodeData.
;
;  REMARKS: Returns pointer to heap memory allocated.  Caller must ensure heap memory
;           freed after use.
*/
{
   LPHANDLELIST pDevHandleList;        // RM handles for spec'd DEVID's
   LPRM_GETNODE_DATA pNode = NULL;     // Node resource data for spec'd DEVID's.

   pDevHandleList = _DevIDToHandleList( DevID, ulSearchFlags, fPciDevice );
                                       // Convert PnP ID -> RM handle.
   if ( pDevHandleList ) {             // If we got a valid handle list
      if ( pDevHandleList->cHandles )  // ... and if we got >0 handles
         pNode = _RMHandleToNodeData( pDevHandleList->Handles[0] );
   }

   return pNode;
}
#endif //#ifndef MANUAL_PCI_DETECTION
//******************************************************************************
//******************************************************************************
BOOL ResourceManager::getPCIConfiguration(ULONG pciId)
{
 ULONG devNr, busNr, funcNr, temp, cfgaddrreg, detectedId;
 BOOL  found = FALSE;

	cfgaddrreg = _inpd(PCI_CONFIG_ADDRESS);
    for(busNr=0;busNr<MAX_PCI_BUSSES;busNr++)     //BusNumber<255
    {
  		for(devNr=0;devNr<32;devNr++)
  		{
			for(funcNr=0;funcNr<8;funcNr++) 
			{
                temp = ((ULONG)((ULONG)devNr<<11UL) + ((ULONG)busNr<<16UL) + ((ULONG)funcNr << 8UL));

                _outpd(PCI_CONFIG_ADDRESS, PCI_CONFIG_ENABLE|temp);
                detectedId = _inpd(PCI_CONFIG_DATA);
                if(detectedId == pciId)
                {
                    found = TRUE;
                    break;
                }
			}
			if(found) break;
		}
		if(found) break;
    }

    if(!found) {
        _outpd(PCI_CONFIG_ADDRESS, cfgaddrreg);
        return FALSE;
    }

    for(int i=0;i<64;i++)
    {
        temp = ((ULONG)((ULONG)devNr<<11UL) + ((ULONG)busNr<<16UL) + ((ULONG)funcNr << 8UL) + (i << 2));
        _outpd(PCI_CONFIG_ADDRESS, PCI_CONFIG_ENABLE|temp);

        PCIConfig[i] = _inpd(PCI_CONFIG_DATA);
    }
    _outpd(PCI_CONFIG_ADDRESS, cfgaddrreg);

	pciConfigData = (PCIConfigData *)&PCIConfig[0];

    if(pciConfigData->Bar[0] == 0 || pciConfigData->Bar[0] == 0xFFFFFFFF)
    {
        //must have at least one io or memory mapped address
		DebugInt3();
        return(FALSE);
    }
    busnr  = busNr  & 0xFF;
    devnr  = devNr  & 0x1F;
    funcnr = funcNr & 0x7;
    devfn  = (devnr << 3) | funcnr;
    return TRUE;
}
//******************************************************************************
//******************************************************************************
void ResourceManager::pci_write_config_dword(ULONG where, ULONG value)
{
    _outpd(PCI_CONFIG_ADDRESS, CONFIG_CMD(busnr,devfn, where));
    _outpd(PCI_CONFIG_DATA, value);
}
//******************************************************************************
//******************************************************************************
void ResourceManager::pci_read_config_dword(ULONG where, ULONG *pValue)
{
    _outpd(PCI_CONFIG_ADDRESS, CONFIG_CMD(busnr,devfn, where));
    *pValue = _inpd(PCI_CONFIG_DATA);
}
//******************************************************************************
//******************************************************************************
HRESMGR RMFindPCIDevice(ULONG vendorid, ULONG deviceid, IDC_RESOURCE *lpResource)
{
    LDev_Resources* pRMResources;
    ResourceManager* pRM = NULL;               // Resource manager object.
    int i;
    ULONG pcidevid;

    pcidevid = (deviceid << 16) | vendorid;
 
    pRM = new ResourceManager();        // Create the RM object.
    if (!pRM) {
        goto fail;
    }
    if(pRM->getState() != rmDriverCreated) {
        goto fail;
    }

    if(!pRM->bIsDevDetected(pcidevid, SEARCH_ID_DEVICEID, TRUE)) {
        goto fail;
    }

    pRMResources = pRM->pGetDevResources(pcidevid, SEARCH_ID_DEVICEID, TRUE);
    if ((!pRMResources) || pRMResources->isEmpty()) {
        goto fail;
    }
    lpResource->busnr  = pRM->getBusNr();
    lpResource->devnr  = pRM->getDeviceNr();
    lpResource->funcnr = pRM->getFunctionNr();
    lpResource->devfn  = pRM->getDevFuncNr();
    dprintf(("Detected device %x%x bus %d dev %d func %d", vendorid, deviceid, lpResource->busnr, lpResource->devnr, lpResource->funcnr));

    // Available device resources identified
    for(i=0;i<MAX_ISA_Dev_IO;i++) {
        lpResource->io[i] = pRMResources->uIOBase[i];
        lpResource->iolength[i] = pRMResources->uIOLength[i];
        if(lpResource->io[i] != 0xffff) 
            dprintf(("IO resource %x length %d", (ULONG)lpResource->io[i], (ULONG)lpResource->iolength[i]));
    }
    for(i=0;i<MAX_ISA_Dev_IRQ;i++) {
        lpResource->irq[i] = pRMResources->uIRQLevel[i];
        if(lpResource->irq[i] != 0xffff) 
            dprintf(("IRQ resource %d ", (ULONG)lpResource->irq[i]));
    }
    for(i=0;i<MAX_ISA_Dev_DMA;i++) {
        lpResource->dma[i] = pRMResources->uDMAChannel[i];
    }
    for(i=0;i<MAX_ISA_Dev_MEM;i++) {
        lpResource->mem[i] = pRMResources->uMemBase[i];
        lpResource->memlength[i] = pRMResources->uMemLength[i];
        if(lpResource->mem[i] != 0xffffffff) 
            dprintf(("Memory resource %x length %d", (ULONG)lpResource->mem[i], (ULONG)lpResource->memlength[i]));
    }

    return (HRESMGR)pRM;

fail:
    if(pRM) delete pRM;
    return 0;
}
//******************************************************************************
//register resources & destroy resource manager object
//******************************************************************************
void RMFinialize(HRESMGR hResMgr)
{
    if(hResMgr) {
        ResourceManager* pRM = (ResourceManager*)hResMgr;
        pRM->registerResources();
        delete pRM;
    }
}
//******************************************************************************
//destroy resource manager object
//******************************************************************************
void RMDestroy(HRESMGR hResMgr)
{
    if(hResMgr) {
        ResourceManager* pRM = (ResourceManager*)hResMgr;
        delete pRM;
    }
}
//******************************************************************************
//******************************************************************************
BOOL RMRequestIO(HRESMGR hResMgr, ULONG ulIOBase, ULONG ulIOLength)
{
    ResourceManager* pRM = (ResourceManager*)hResMgr;

    if(!pRM) {
        DebugInt3();
        return FALSE;
    }
    return pRM->requestIORange(ulIOBase, ulIOLength);
}
//******************************************************************************
//******************************************************************************
BOOL RMRequestMem(HRESMGR hResMgr, ULONG ulMemBase, ULONG ulMemLength)
{
    ResourceManager* pRM = (ResourceManager*)hResMgr;

    if(!pRM) {
        DebugInt3();
        return FALSE;
    }
    return pRM->requestMemRange(ulMemBase, ulMemLength);
}
//******************************************************************************
//******************************************************************************
BOOL RMRequestIRQ(HRESMGR hResMgr, ULONG ulIrq, BOOL fShared)
{
    ResourceManager* pRM = (ResourceManager*)hResMgr;

    if(!pRM) {
        DebugInt3();
        return FALSE;
    }
    return pRM->requestIRQ((USHORT)ulIrq, fShared);
}
//******************************************************************************
//******************************************************************************
BOOL RMRequestDMA(HRESMGR hResMgr, ULONG ulDMA)
{
    ResourceManager* pRM = (ResourceManager*)hResMgr;

    if(!pRM) {
        DebugInt3();
        return FALSE;
    }
    return pRM->requestDMA((USHORT)ulDMA);
}
//******************************************************************************
//******************************************************************************

