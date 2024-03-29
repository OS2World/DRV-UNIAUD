/* $Id: sound.c,v 1.44 2002/06/13 12:43:25 sandervl Exp $ */
/*
 * MMPM/2 to OSS interface translation layer
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

#include <sound/driver.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/minors.h>
#include <linux/file.h>
#include <linux/soundcard.h>

#define LINUX
#include <ossidc32.h>
#include <osspci.h>
#include <stacktoflat.h>
#include <stdlib.h>
#include "soundoss.h"

#undef samples_to_bytes
#undef bytes_to_samples
#define samples_to_bytes(a)     ((a*pHandle->doublesamplesize)/2)
#define bytes_to_samples(a)    (pHandle->doublesamplesize ? ((a*2)/pHandle->doublesamplesize) : a)

struct file_operations oss_devices[OSS32_MAX_DEVICES] = {0};
struct file_operations *alsa_fops = NULL;

//OSS32 to ALSA datatype conversion table
static OSSToALSADataType[OSS32_PCM_MAX_FORMATS] = {
/* OSS32_PCM_FORMAT_S8     */ SNDRV_PCM_FORMAT_S8,          //signed 8 bits sample
/* OSS32_PCM_FORMAT_U8     */ SNDRV_PCM_FORMAT_U8,          //unsigned 8 bits sample
/* OSS32_PCM_FORMAT_S16_LE */ SNDRV_PCM_FORMAT_S16_LE,      //signed 16 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_S16_BE */ SNDRV_PCM_FORMAT_S16_BE,      //signed 16 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_U16_LE */ SNDRV_PCM_FORMAT_U16_LE,      //unsigned 16 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_U16_BE */ SNDRV_PCM_FORMAT_U16_BE,      //unsigned 16 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_S24_LE */ SNDRV_PCM_FORMAT_S24_LE,      //signed 24 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_S24_BE */ SNDRV_PCM_FORMAT_S24_BE,      //signed 24 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_U24_LE */ SNDRV_PCM_FORMAT_U24_LE,      //unsigned 24 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_U24_BE */ SNDRV_PCM_FORMAT_U24_BE,      //unsigned 24 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_S32_LE */ SNDRV_PCM_FORMAT_S32_LE,      //signed 32 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_S32_BE */ SNDRV_PCM_FORMAT_S32_BE,      //signed 32 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_U32_LE */ SNDRV_PCM_FORMAT_U32_LE,      //unsigned 32 bits sample (little endian/Intel)
/* OSS32_PCM_FORMAT_U32_BE */ SNDRV_PCM_FORMAT_U32_BE,      //unsigned 32 bits sample (big endian/Motorola)
/* OSS32_PCM_FORMAT_MULAW  */ SNDRV_PCM_FORMAT_MU_LAW,      //8 bps (compressed 16 bits sample)
/* OSS32_PCM_FORMAT_ALAW   */ SNDRV_PCM_FORMAT_A_LAW,       //8 bps (compressed 16 bits sample)
/* OSS32_PCM_FORMAT_ADPCM  */ SNDRV_PCM_FORMAT_IMA_ADPCM,   //4 bps (compressed 16 bits sample)
/* OSS32_PCM_FORMAT_MPEG   */ SNDRV_PCM_FORMAT_MPEG,        //AC3?
};

//******************************************************************************
//******************************************************************************
int register_chrdev(unsigned int version, const char *name, struct file_operations *fsop)
{
   if(!strcmp(name, "alsa")) {
       alsa_fops = fsop;
   }
   return 0;
}
//******************************************************************************
//******************************************************************************
int unregister_chrdev(unsigned int version, const char *name)
{
   if(!strcmp(name, "alsa")) {
       alsa_fops = NULL;
   }
   return 0;
}
//******************************************************************************
//******************************************************************************
int register_sound_special(struct file_operations *fops, int unit)
{
    if(fops == NULL) return -1;

    memcpy(&oss_devices[OSS32_SPECIALID], fops, sizeof(struct file_operations));
    return OSS32_SPECIALID;
}
//******************************************************************************
//******************************************************************************
int register_sound_mixer(struct file_operations *fops, int dev)
{
    if(fops == NULL) return -1;

    memcpy(&oss_devices[OSS32_MIXERID], fops, sizeof(struct file_operations));
    return OSS32_MIXERID;
}
//******************************************************************************
//******************************************************************************
int register_sound_midi(struct file_operations *fops, int dev)
{
    if(fops == NULL) return -1;

    memcpy(&oss_devices[OSS32_MIDIID], fops, sizeof(struct file_operations));
    return OSS32_MIDIID;
}
//******************************************************************************
//******************************************************************************
int register_sound_dsp(struct file_operations *fops, int dev)
{
    if(fops == NULL) return -1;

    memcpy(&oss_devices[OSS32_DSPID], fops, sizeof(struct file_operations));
    return OSS32_DSPID;
}
//******************************************************************************
//******************************************************************************
int register_sound_synth(struct file_operations *fops, int dev)
{
    if(fops == NULL) return -1;

    memcpy(&oss_devices[OSS32_SYNTHID], fops, sizeof(struct file_operations));
    return OSS32_SYNTHID;
}
//******************************************************************************
//******************************************************************************
void unregister_sound_special(int unit)
{
    memset(&oss_devices[OSS32_SPECIALID], 0, sizeof(struct file_operations));
}
//******************************************************************************
//******************************************************************************
void unregister_sound_mixer(int unit)
{
    memset(&oss_devices[OSS32_MIXERID], 0, sizeof(struct file_operations));
}
//******************************************************************************
//******************************************************************************
void unregister_sound_midi(int unit)
{
    memset(&oss_devices[OSS32_MIDIID], 0, sizeof(struct file_operations));
}
//******************************************************************************
//******************************************************************************
void unregister_sound_dsp(int unit)
{
    memset(&oss_devices[OSS32_DSPID], 0, sizeof(struct file_operations));
}
//******************************************************************************
//******************************************************************************
void unregister_sound_synth(int unit)
{
    memset(&oss_devices[OSS32_SYNTHID], 0, sizeof(struct file_operations));
}
//******************************************************************************
//******************************************************************************
OSSRET UNIXToOSSError(int unixerror)
{
    switch(unixerror) {
    case 0:
        return OSSERR_SUCCESS;
    case -ENOMEM:
        return OSSERR_OUT_OF_MEMORY;
    case -ENODEV:
        return OSSERR_NO_DEVICE_AVAILABLE;
    case -ENOTTY:
    case -EINVAL:
        return OSSERR_INVALID_PARAMETER;
    case -EAGAIN:
        return OSSERR_AGAIN;    //????
    case -ENXIO:
        return OSSERR_IO_ERROR;
    case -EBUSY:
        return OSSERR_BUSY;
    case -EPERM:
        return OSSERR_ACCESS_DENIED; //??
    case -EPIPE:
    case -EBADFD:
        return OSSERR_ACCESS_DENIED; //??
    default:
        dprintf(("Unknown error %d", (unixerror > 0) ? unixerror : -unixerror));
        return OSSERR_UNKNOWN;
    }
}
//******************************************************************************
//ALSA to OSS32 datatype conversion
//******************************************************************************
int ALSAToOSSDataType(ULONG ALSADataType)
{
    switch(ALSADataType) 
    {
    case SNDRV_PCM_FORMAT_S8:
        return OSS32_CAPS_PCM_FORMAT_S8;          //signed 8 bits sample
    case SNDRV_PCM_FORMAT_U8:
        return OSS32_CAPS_PCM_FORMAT_U8;          //unsigned 8 bits sample
    case SNDRV_PCM_FORMAT_S16_LE:
        return OSS32_CAPS_PCM_FORMAT_S16_LE;      //signed 16 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_S16_BE:
        return OSS32_CAPS_PCM_FORMAT_S16_BE;      //signed 16 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_U16_LE:
        return OSS32_CAPS_PCM_FORMAT_U16_LE;      //unsigned 16 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_U16_BE:
        return OSS32_CAPS_PCM_FORMAT_U16_BE;      //unsigned 16 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_S24_LE:
        return OSS32_CAPS_PCM_FORMAT_S24_LE;      //signed 24 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_S24_BE:
        return OSS32_CAPS_PCM_FORMAT_S24_BE;      //signed 24 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_U24_LE:
        return OSS32_CAPS_PCM_FORMAT_U24_LE;      //unsigned 24 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_U24_BE:
        return OSS32_CAPS_PCM_FORMAT_U24_BE;      //unsigned 16 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_S32_LE:
        return OSS32_CAPS_PCM_FORMAT_S32_LE;      //signed 32 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_S32_BE:
        return OSS32_CAPS_PCM_FORMAT_S32_BE;      //signed 32 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_U32_LE:
        return OSS32_CAPS_PCM_FORMAT_U32_LE;      //unsigned 32 bits sample (little endian/Intel)
    case SNDRV_PCM_FORMAT_U32_BE:
        return OSS32_CAPS_PCM_FORMAT_U32_BE;      //unsigned 32 bits sample (big endian/Motorola)
    case SNDRV_PCM_FORMAT_MU_LAW:
        return OSS32_CAPS_PCM_FORMAT_MULAW;       //8 bps (compressed 16 bits sample)
    case SNDRV_PCM_FORMAT_A_LAW:
        return OSS32_CAPS_PCM_FORMAT_ALAW;        //8 bps (compressed 16 bits sample)
    case SNDRV_PCM_FORMAT_IMA_ADPCM:
        return OSS32_CAPS_PCM_FORMAT_ADPCM;       //4 bps (compressed 16 bits sample)
    case SNDRV_PCM_FORMAT_MPEG:
        return OSS32_CAPS_PCM_FORMAT_MPEG;        //AC3?
    default:
        DebugInt3();
        return -1;
    }
}
//******************************************************************************
//******************************************************************************
ULONG ALSAToOSSRateFlags(ULONG fuRates)
{
    ULONG fuOSSRates = 0;

    if(fuRates & SNDRV_PCM_RATE_5512) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_5512;
    }
    if(fuRates & SNDRV_PCM_RATE_8000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_8000;
    }
    if(fuRates & SNDRV_PCM_RATE_11025) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_11025;
    }
    if(fuRates & SNDRV_PCM_RATE_16000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_16000;
    }
    if(fuRates & SNDRV_PCM_RATE_22050) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_22050;
    }
    if(fuRates & SNDRV_PCM_RATE_32000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_32000;
    }
    if(fuRates & SNDRV_PCM_RATE_44100) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_44100;
    }
    if(fuRates & SNDRV_PCM_RATE_48000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_48000;
    }
    if(fuRates & SNDRV_PCM_RATE_64000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_64000;
    }
    if(fuRates & SNDRV_PCM_RATE_88200) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_88200;
    }
    if(fuRates & SNDRV_PCM_RATE_96000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_96000;
    }
    if(fuRates & SNDRV_PCM_RATE_176400) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_176400;
    }
    if(fuRates & SNDRV_PCM_RATE_192000) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_192000;
    }
    if(fuRates & SNDRV_PCM_RATE_CONTINUOUS) {
        fuOSSRates |= OSS32_CAPS_PCM_RATE_CONTINUOUS;
    }

    //TODO:
    if(fuRates & SNDRV_PCM_RATE_KNOT) {
        DebugInt3();
    }
//#define OSS32_CAPS_PCM_RATE_KNOT		(1<<31)		/* supports more non-continuos rates */

    return fuOSSRates;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_QueryDevCaps(ULONG deviceid, POSS32_DEVCAPS pDevCaps)
{
    OSSSTREAMID          streamid = 0;
    soundhandle         *pHandle;
    snd_pcm_info_t      *pcminfo = NULL;
    snd_pcm_hw_params_t *params;
    int                  ret, fmt, i;
    ULONG                format_mask;

    //these structures are too big to put on the stack
    pcminfo = (snd_pcm_info_t *)kmalloc(sizeof(snd_pcm_info_t)+sizeof(snd_pcm_hw_params_t), GFP_KERNEL);
    if(pcminfo == NULL) {
        DebugInt3();
        return OSSERR_OUT_OF_MEMORY;
    }
    params = (snd_pcm_hw_params_t *)(pcminfo+1);

    pDevCaps->nrDevices  = nrCardsDetected;
    pDevCaps->ulCaps     = OSS32_CAPS_WAVE_PLAYBACK | OSS32_CAPS_WAVE_CAPTURE;

    //query wave in & out caps
    for(i=0;i<2;i++) 
    {
        PWAVE_CAPS pWaveCaps = (i == 0) ? &pDevCaps->waveOutCaps : &pDevCaps->waveInCaps;

        ret = OSS32_WaveOpen(deviceid, (i == 0) ? OSS32_STREAM_WAVEOUT : OSS32_STREAM_WAVEIN, &streamid);
        if(ret != OSSERR_SUCCESS) 
        {
            DebugInt3();
            goto fail;
        }
        pHandle = (soundhandle *)streamid;
        if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
            ret = OSSERR_INVALID_STREAMID;
            goto fail;
        }
        //set operation to non-blocking
        pHandle->file.f_flags = O_NONBLOCK;
    
        ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_INFO, (ULONG)pcminfo);
        if(ret != 0) {
            ret = UNIXToOSSError(ret);
            goto fail;
        }
        if(i == 0) {//only need to do this once
            if(pcminfo->name[0]) {
                 strncpy(pDevCaps->szDeviceName, pcminfo->name, sizeof(pDevCaps->szDeviceName));
            }
            else strncpy(pDevCaps->szDeviceName, pcminfo->id, sizeof(pDevCaps->szDeviceName));
        }
        pWaveCaps->nrStreams = pcminfo->subdevices_count;
    
        //get all hardware parameters
        _snd_pcm_hw_params_any(params);
        ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_HW_REFINE, (ULONG)params);
        if(ret != 0) {
            ret = UNIXToOSSError(ret);
            goto fail;
        }
        pWaveCaps->ulMinChannels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->min;
        pWaveCaps->ulMaxChannels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->max;
        pWaveCaps->ulChanFlags   = 0;
        if(pWaveCaps->ulMinChannels == 1) {
            pWaveCaps->ulChanFlags |= OSS32_CAPS_PCM_CHAN_MONO;
        }
        if(pWaveCaps->ulMaxChannels >= 2) {
            pWaveCaps->ulChanFlags |= OSS32_CAPS_PCM_CHAN_STEREO;
        }
        if(pWaveCaps->ulMaxChannels >= 4) {
            pWaveCaps->ulChanFlags |= OSS32_CAPS_PCM_CHAN_QUAD;
        }
        if(pWaveCaps->ulMaxChannels >= 6) {
            pWaveCaps->ulChanFlags |= OSS32_CAPS_PCM_CHAN_5_1;
        }

        pWaveCaps->ulMinRate     = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->min;
        pWaveCaps->ulMaxRate     = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->max;
        pWaveCaps->ulRateFlags   = *hw_param_mask(params, SNDRV_PCM_HW_PARAM_RATE_MASK);

        pWaveCaps->ulRateFlags   = ALSAToOSSRateFlags(pWaveCaps->ulRateFlags);

        pWaveCaps->ulDataFormats = 0;

        format_mask = *hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT); 
        for(fmt=0;fmt<32;fmt++) 
        {
            if(format_mask & (1 << fmt)) 
            {
                int f = ALSAToOSSDataType(fmt);
                if (f >= 0)
                    pWaveCaps->ulDataFormats |= f;
            }
        }
        OSS32_WaveClose(streamid);
        streamid = 0;
    }

    //Check support for MPU401, FM & Wavetable MIDI
    if(OSS32_MidiOpen(deviceid, OSS32_STREAM_MPU401_MIDIOUT, &streamid) == OSSERR_SUCCESS) 
    {
        pDevCaps->ulCaps |= OSS32_CAPS_MPU401_PLAYBACK;
        OSS32_MidiClose(streamid);
        streamid = 0;
    }
    if(OSS32_MidiOpen(deviceid, OSS32_STREAM_MPU401_MIDIIN, &streamid) == OSSERR_SUCCESS) 
    {
        pDevCaps->ulCaps |= OSS32_CAPS_MPU401_CAPTURE;
        OSS32_MidiClose(streamid);
        streamid = 0;
    }
    if(OSS32_MidiOpen(deviceid, OSS32_STREAM_FM_MIDIOUT, &streamid) == OSSERR_SUCCESS) 
    {
        pDevCaps->ulCaps |= OSS32_CAPS_FMSYNTH_PLAYBACK;
        OSS32_MidiClose(streamid);
        streamid = 0;
    }
    if(OSS32_MidiOpen(deviceid, OSS32_STREAM_WAVETABLE_MIDIOUT, &streamid) == OSSERR_SUCCESS) 
    {
        pDevCaps->ulCaps |= OSS32_CAPS_WAVETABLE_PLAYBACK;
        OSS32_MidiClose(streamid);
        streamid = 0;
    }
    if(OSS32_MixQueryName(deviceid, &pDevCaps->szMixerName, sizeof(pDevCaps->szMixerName)) != OSSERR_SUCCESS) {
        DebugInt3();
        goto fail;
    }

    kfree(pcminfo);
    streamid = 0;

    return OSSERR_SUCCESS;

fail:
    DebugInt3();
    if(streamid)   OSS32_WaveClose(streamid);
    if(pcminfo)    kfree(pcminfo);

    return ret;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveOpen(ULONG deviceid, ULONG streamtype, OSSSTREAMID *pStreamId)
{
    soundhandle *pHandle;
    int          ret;

    *pStreamId = 0;

    if(alsa_fops == NULL) {
        DebugInt3();
        return OSSERR_NO_DEVICE_AVAILABLE;
    }

    pHandle = kmalloc(sizeof(soundhandle), GFP_KERNEL);
    if(pHandle == NULL) {
        DebugInt3();
        return OSSERR_OUT_OF_MEMORY;
    }
    memset(pHandle, 0, sizeof(soundhandle));

    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    //setup pointers in file structure (used internally by ALSA)
    pHandle->file.f_dentry          = &pHandle->d_entry;
    pHandle->file.f_dentry->d_inode = &pHandle->inode;

    switch(streamtype) {
    case OSS32_STREAM_WAVEOUT:
        pHandle->file.f_mode  = FMODE_WRITE;
        pHandle->inode.i_rdev = SNDRV_MINOR(deviceid, SNDRV_MINOR_PCM_PLAYBACK);
        break;
    case OSS32_STREAM_WAVEIN:
        pHandle->file.f_mode  = FMODE_READ;
        pHandle->inode.i_rdev = SNDRV_MINOR(deviceid, SNDRV_MINOR_PCM_CAPTURE);
        break;
    default:
        DebugInt3();
        kfree(pHandle);
        return OSSERR_INVALID_PARAMETER;
    }

    ret = alsa_fops->open(&pHandle->inode, &pHandle->file);
    if(ret) {
        kfree(pHandle);
        DebugInt3();
        return UNIXToOSSError(ret);
    }
    pHandle->magic = MAGIC_WAVE_ALSA32;
    *pStreamId = (ULONG)pHandle;
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveClose(OSSSTREAMID streamid)
{
    soundhandle *pHandle = (soundhandle *)streamid;
    int          ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
        DebugInt3();
        return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;
    ret = pHandle->file.f_op->release(&pHandle->inode, &pHandle->file);
    kfree(pHandle);   //free handle data

    if(ret) {
        DebugInt3();
        return UNIXToOSSError(ret);
    }
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WavePrepare(OSSSTREAMID streamid)
{
    soundhandle    *pHandle = (soundhandle *)streamid;
    int             ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_PREPARE, 0);

    return UNIXToOSSError(ret);;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveStart(OSSSTREAMID streamid)
{
    soundhandle    *pHandle = (soundhandle *)streamid;
    int             ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_START, 0);

    return UNIXToOSSError(ret);;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveStop(OSSSTREAMID streamid)
{
    soundhandle *pHandle = (soundhandle *)streamid;
    int          ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_DROP, 0);

    return UNIXToOSSError(ret);;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WavePause(OSSSTREAMID streamid)
{
    soundhandle *pHandle = (soundhandle *)streamid;
    int          ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_PAUSE, 0);

    return UNIXToOSSError(ret);;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveResume(OSSSTREAMID streamid)
{
    soundhandle *pHandle = (soundhandle *)streamid;
    int          ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_PAUSE, 1);

    return UNIXToOSSError(ret);;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveSetHwParams(OSSSTREAMID streamid, OSS32_HWPARAMS *pHwParams)
{
    soundhandle        *pHandle = (soundhandle *)streamid;
    snd_pcm_hw_params_t params;
    snd_pcm_sw_params_t swparams;
    int                 ret, nrperiods, minnrperiods, maxnrperiods, samplesize;
    ULONG               bufsize, periodsize, minperiodsize, maxperiodsize;
    ULONG               periodbytes, minperiodbytes, maxperiodbytes;
    BOOL                fTryAgain = FALSE;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    if(pHwParams == NULL) {
          DebugInt3();
          return OSSERR_INVALID_PARAMETER;
    }
    if(pHwParams->ulDataType >= OSS32_PCM_MAX_FORMATS) {
          DebugInt3();
          return OSSERR_INVALID_PARAMETER;
    }

    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    //size of two samples (adpcm sample can be as small as 4 bits (mono), so take two)
    samplesize = snd_pcm_format_size(OSSToALSADataType[pHwParams->ulDataType], 1);
    pHandle->doublesamplesize  = samplesize * 2;
    pHandle->doublesamplesize *= pHwParams->ulNumChannels;
    periodbytes = pHwParams->ulPeriodSize;
    periodsize  = bytes_to_samples(periodbytes);

    //get all hardware parameters
    _snd_pcm_hw_params_any(&params);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                          SNDRV_PCM_ACCESS_RW_INTERLEAVED, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
                          pHwParams->ulBitsPerSample, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                          pHwParams->ulBitsPerSample*pHwParams->ulNumChannels, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_FORMAT,
                          OSSToALSADataType[pHwParams->ulDataType], 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
                           pHwParams->ulNumChannels, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_RATE,
                          pHwParams->ulSampleRate, 0);
    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_HW_REFINE, (ULONG)__Stack32ToFlat(&params));
    if(ret != 0) {
        DebugInt3();
        return UNIXToOSSError(ret);
    }

    //check period size against lower and upper boundaries
    minperiodbytes = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min;
    maxperiodbytes = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->max;
    if(periodbytes < minperiodbytes) {
        periodbytes = minperiodbytes;
    }
    else
    if(periodbytes > maxperiodbytes) {
        periodbytes = maxperiodbytes;
    }

    minperiodsize = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min;
    maxperiodsize = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->max;
    if(periodsize < minperiodsize) {
        periodsize = minperiodsize;
    }
    else
    if(periodsize > maxperiodsize) {
        periodsize = maxperiodsize;
    }

    if(samples_to_bytes(periodsize) < periodbytes) {
        periodbytes = samples_to_bytes(periodsize);
    }
    else
    if(bytes_to_samples(periodbytes) < periodsize) {
        periodsize = bytes_to_samples(periodbytes);
    }

    //make sure period size is a whole fraction of the buffer size
    bufsize = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->max;
    if(periodsize) {
        nrperiods = bufsize/periodbytes;
    }
    else {
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }
    //check nr of periods against lower and upper boundaries
    minnrperiods = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIODS)->min;
    maxnrperiods = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIODS)->max;
    if(nrperiods < minnrperiods) {
        nrperiods = minnrperiods;
    }
    else
    if(nrperiods > maxnrperiods) {
        nrperiods = maxnrperiods;
    }
    //an odd nr of periods is not always a good thing (CMedia -> clicks during 8 bps playback),
    //so we make sure it's an even number.
    if(nrperiods == 1) {
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }
    nrperiods &= ~1;

    //initialize parameter block & set sample rate, nr of channels and sample format, nr of periods,
    //period size and buffer size
tryagain:
    _snd_pcm_hw_params_any(&params);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                          SNDRV_PCM_ACCESS_RW_INTERLEAVED, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
                          pHwParams->ulBitsPerSample, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                          pHwParams->ulBitsPerSample*pHwParams->ulNumChannels, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_FORMAT,
                          OSSToALSADataType[pHwParams->ulDataType], 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
                           pHwParams->ulNumChannels, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_RATE,
                          pHwParams->ulSampleRate, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
                          periodsize, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 
                          periodbytes, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_PERIODS, 
                          nrperiods, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
                          periodsize*nrperiods, 0);
    _snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
                          periodbytes*nrperiods, 0);

    dprintf(("Hardware parameters: sample rate %d, data type %d, channels %d, period size %d, periods %d", 
             pHwParams->ulSampleRate, pHwParams->ulDataType, pHwParams->ulNumChannels, periodsize, nrperiods));

    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_HW_PARAMS, (ULONG)__Stack32ToFlat(&params));
    if(ret) {
        if(fTryAgain == FALSE) {
            minperiodsize = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min;
            maxperiodsize = hw_param_interval((&params), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->max;
            if(minperiodsize > maxperiodsize) {
                //ALSA doesn't like the period size; try suggested one
                periodsize  = maxperiodsize;
                periodbytes = samples_to_bytes(periodsize);
                fTryAgain = TRUE;
                goto tryagain;
            }
        }
        return UNIXToOSSError(ret);
    }

    //set silence threshold (all sizes in frames) (only needed for playback)
    if(pHandle->file.f_mode == FMODE_WRITE) 
    {
        swparams.avail_min         = periodsize;
        swparams.period_step       = 1;
        if(nrperiods <= 2) {
            swparams.silence_size  = (periodsize/2);
        }
        else {
            swparams.silence_size  = periodsize;
        }
        swparams.silence_threshold = swparams.silence_size;
        swparams.sleep_min         = 0;
        swparams.start_threshold   = 1;
        swparams.stop_threshold    = periodsize*nrperiods;
        swparams.tstamp_mode       = SNDRV_PCM_TSTAMP_NONE;
        swparams.xfer_align        = periodsize;

        ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_SW_PARAMS, (ULONG)__Stack32ToFlat(&swparams));
    }
    return UNIXToOSSError(ret);
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveAddBuffer(OSSSTREAMID streamid, ULONG buffer, ULONG size, ULONG *pTransferred)
{
    soundhandle        *pHandle = (soundhandle *)streamid;
    snd_pcm_status_t    status;
    int                 ret;
    LONG                transferred;
    ULONG               position;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
        DebugInt3();
        return OSSERR_INVALID_STREAMID;
    }
    if(pTransferred == NULL || buffer == 0 || size == 0) {
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }

    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    //first check how much room is left in the circular dma buffer
    //this is done to make sure we don't block inside ALSA while trying to write
    //more data than fits in the internal dma buffer.
    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_STATUS, (ULONG)__Stack32ToFlat(&status));

    if(ret) {
        DebugInt3();
        return UNIXToOSSError(ret);
    }
    size = min(size, samples_to_bytes(status.avail));
    if(size == 0) {
        dprintf2(("OSS32_WaveAddBuffer: no room left in hardware buffer!!"));
        *pTransferred = 0;
        return OSSERR_BUFFER_FULL;
    }
    dprintf2(("OSS32_WaveAddBuffer hw %x app %x avail %x, size %x", samples_to_bytes(status.hw_ptr), samples_to_bytes(status.appl_ptr), samples_to_bytes(status.avail), size));

    transferred = 0;
    switch(SNDRV_MINOR_DEVICE(MINOR(pHandle->inode.i_rdev)))
    {
    case SNDRV_MINOR_PCM_PLAYBACK:
        while(TRUE) {
            ret = pHandle->file.f_op->write(&pHandle->file, (char *)buffer, size, &pHandle->file.f_pos);
            if(ret < 0) {
                if(transferred > 0) {
                    dprintf(("OSS32_WaveAddBuffer failed on partial transfer %x %d; ret = %d", buffer, size, ret));

                    *pTransferred = transferred;
                    return OSSERR_SUCCESS;
                }
                dprintf(("OSS32_WaveAddBuffer failed!!"));
                DebugInt3();
                return UNIXToOSSError(transferred);
            }
            if(ret == 0) {
                break;
            }
            transferred += ret;

            buffer += ret;
            size   -= ret;
            if(size == 0) {
                break;
            }
        }
        break;
    case SNDRV_MINOR_PCM_CAPTURE:
        transferred = pHandle->file.f_op->read(&pHandle->file, (char *)buffer, size, &pHandle->file.f_pos);
        break;
    default:
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }
    if(ret < 0) {
        dprintf(("OSS32_WaveAddBuffer failed!!"));
        DebugInt3();
        return UNIXToOSSError(transferred);
    }

    dprintf2(("OSS32_WaveAddBuffer: transferred %x bytes", transferred));
    *pTransferred = transferred;

    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveGetPosition(ULONG streamid, ULONG *pPosition)
{
    soundhandle        *pHandle = (soundhandle *)streamid;
    snd_pcm_status_t    status;
    int                 ret;
    ULONG               delta;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
        DebugInt3();
        return OSSERR_INVALID_STREAMID;
    }
    if(pPosition == NULL) {
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }

    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    //Get the status of the stream
    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_STATUS, (ULONG)__Stack32ToFlat(&status));

    if(ret) {
        DebugInt3();
        return UNIXToOSSError(ret);
    }

    dprintf2(("OSS32_WaveGetPosition: hardware %x application %x", samples_to_bytes(status.hw_ptr), samples_to_bytes(status.appl_ptr)));
    *pPosition = samples_to_bytes(status.hw_ptr);  //return new hardware position
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveGetSpace(ULONG streamid, ULONG *pBytesAvail)
{
    soundhandle        *pHandle = (soundhandle *)streamid;
    snd_pcm_status_t    status;
    int                 ret;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
        DebugInt3();
        return OSSERR_INVALID_STREAMID;
    }
    if(pBytesAvail == NULL) {
        DebugInt3();
        return OSSERR_INVALID_PARAMETER;
    }

    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    //Get the nr of bytes left in the audio buffer
    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_STATUS, (ULONG)__Stack32ToFlat(&status));

    if(ret) {
        DebugInt3();
        return UNIXToOSSError(ret);
    }
    *pBytesAvail = samples_to_bytes(status.avail);
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_WaveSetVolume(OSSSTREAMID streamid, ULONG volume)
{
    soundhandle *pHandle = (soundhandle *)streamid;
    int          ret;
    int          leftvol, rightvol;
    snd_pcm_volume_t pcm_volume;

    if(pHandle == NULL || pHandle->magic != MAGIC_WAVE_ALSA32) {
          DebugInt3();
          return OSSERR_INVALID_STREAMID;
    }
    //set operation to non-blocking
    pHandle->file.f_flags = O_NONBLOCK;

    leftvol  = GET_VOLUME_L(volume);
    rightvol = GET_VOLUME_R(volume);

    pcm_volume.nrchannels = 4;
    pcm_volume.volume[SNDRV_PCM_VOL_FRONT_LEFT]  = leftvol;
    pcm_volume.volume[SNDRV_PCM_VOL_FRONT_RIGHT] = rightvol;
    pcm_volume.volume[SNDRV_PCM_VOL_REAR_LEFT]   = leftvol;
    pcm_volume.volume[SNDRV_PCM_VOL_REAR_RIGHT]  = rightvol;

    dprintf(("OSS32_WaveSetVolume %x to (%d,%d)", streamid, leftvol, rightvol));
    ret = pHandle->file.f_op->ioctl(&pHandle->inode, &pHandle->file, SNDRV_PCM_IOCTL_SETVOLUME, (ULONG)__Stack32ToFlat(&pcm_volume));
    return UNIXToOSSError(ret);
}
//******************************************************************************
//******************************************************************************

