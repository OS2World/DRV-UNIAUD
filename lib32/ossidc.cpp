/* $Id: ossidc.cpp,v 1.35 2002/05/03 14:11:30 sandervl Exp $ */
/*
 * OS/2 IDC services (callback to 16 bits MMPM2 driver)
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

#define INCL_NOPMAPI
#define INCL_DOSERRORS           // for ERROR_INVALID_FUNCTION
#include <os2.h>
#include <ossdefos2.h>
#include <ossidc32.h>
#include <dbgos2.h>
#include <devhelp.h>
#include <unicard.h>
#ifdef KEE
#include <kee.h>
#endif
#include "initcall.h"

//******************************************************************************
//******************************************************************************
BOOL CallOSS16(ULONG cmd, ULONG param1, ULONG param2)
{
    BOOL         rc;

    if(idc16_PddHandler == 0) {
	    return FALSE;
    }

    rc = CallPDD16(idc16_PddHandler, cmd, param1, param2);
    return rc;
}
//******************************************************************************
exitcall_t fnCardExitCall[OSS32_MAX_AUDIOCARDS] = {0};
extern "C" {
int        nrCardsDetected = 0;
int        fStrategyInit = FALSE;
};
//******************************************************************************
OSSRET OSS32_Initialize()
{
    fStrategyInit = TRUE;

    if(call_module_init(alsa_sound_init) != 0)       return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_pcm_init) != 0)         return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_hwdep_init) != 0)       return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_timer_init) != 0)       return OSSERR_INIT_FAILED;

    DebugInt3();
    if(call_module_init(alsa_rawmidi_init) != 0)     return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_seq_init) != 0)         return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_opl3_init) != 0)        return OSSERR_INIT_FAILED;
    if(call_module_init(alsa_opl3_seq_init) != 0)    return OSSERR_INIT_FAILED;
    
    if(call_module_init(alsa_mpu401_uart_init) != 0) return OSSERR_INIT_FAILED;

    //Check for SoundBlaster Live!
    if((ForceCard == CARD_NONE || ForceCard == CARD_SBLIVE) && 
       nrCardsDetected < (OSS32_MAX_AUDIOCARDS-1) && call_module_init(alsa_card_emu10k1_init) == 0) 
    {
        fnCardExitCall[nrCardsDetected++] = name_module_exit(alsa_card_emu10k1_exit);
    }    
    else //Check for C-Media 8738 Audio
    if((ForceCard == CARD_NONE || ForceCard == CARD_CMEDIA) && 
       call_module_init(alsa_card_cmipci_init) == 0) 
    {
        fnCardExitCall[nrCardsDetected++] = name_module_exit(alsa_card_cmipci_exit);
    }
    else //Check for Avance Logic ALS4000 Audio
    if((ForceCard == CARD_NONE || ForceCard == CARD_ALS4000) && 
       nrCardsDetected < (OSS32_MAX_AUDIOCARDS-1) && call_module_init(alsa_card_als4k_init) == 0) 
    {
        fnCardExitCall[nrCardsDetected++] = name_module_exit(alsa_card_als4k_exit);
    }
    else //Check for Crystal Semi 4281 Audio
    if((ForceCard == CARD_NONE || ForceCard == CARD_CS4281) && 
       nrCardsDetected < (OSS32_MAX_AUDIOCARDS-1) && call_module_init(alsa_card_cs4281_init) == 0) 
    {
        fnCardExitCall[nrCardsDetected++] = name_module_exit(alsa_card_cs4281_exit);
    }
    else //Check for Intel ICH Audio
    if((ForceCard == CARD_NONE || ForceCard == CARD_ICH) && 
       nrCardsDetected < (OSS32_MAX_AUDIOCARDS-1) && call_module_init(alsa_card_intel8x0_init) == 0) 
    {
        fnCardExitCall[nrCardsDetected++] = name_module_exit(alsa_card_intel8x0_exit);
    }
    fStrategyInit = FALSE;

    if(nrCardsDetected != 0) {
        return OSSERR_SUCCESS;
    }
    return OSSERR_INIT_FAILED;
}
//******************************************************************************
//Called during OS/2 shutdown
//******************************************************************************
OSSRET OSS32_Shutdown()
{
    CallOSS16(IDC16_EXIT, 0, 0);

    for(int i=0;i<nrCardsDetected;i++) {
        if(fnCardExitCall[i]) fnCardExitCall[i]();
    }

    call_module_exit(alsa_mpu401_uart_exit);
    call_module_exit(alsa_opl3_seq_exit);
    call_module_exit(alsa_opl3_exit);
    call_module_exit(alsa_seq_exit);
    call_module_exit(alsa_rawmidi_exit);
    call_module_exit(alsa_timer_exit);
    call_module_exit(alsa_hwdep_exit);
    call_module_exit(alsa_pcm_exit);
    call_module_exit(alsa_sound_exit);

    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
int OSS32_ProcessIRQ()
{
    return CallOSS16(IDC16_PROCESS, 0, 0);
}
//******************************************************************************
//******************************************************************************
