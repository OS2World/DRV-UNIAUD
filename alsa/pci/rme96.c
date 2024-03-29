/*
 *   ALSA driver for RME Digi96, Digi96/8 and Digi96/8 PRO/PAD/PST audio
 *   interfaces 
 *
 *	Copyright (c) 2000, 2001 Anders Torger <torger@ludd.luth.se>
 *    
 *      Thanks to Henk Hesselink <henk@anda.nl> for the analog volume control
 *      code.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */      

#define SNDRV_MAIN_OBJECT_FILE

#include <sound/driver.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

/* note, two last pcis should be equal, it is not a bug */
EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("RME Digi96, Digi96/8, Digi96/8 PRO, Digi96/8 PST, "
		   "Digi96/8 PAD");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{RME,Digi96},"
		"{RME,Digi96/8},"
		"{RME,Digi96/8 PRO},"
		"{RME,Digi96/8 PST},"
		"{RME,Digi96/8 PAD}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for RME Digi96 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for RME Digi96 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable RME Digi96 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_AUTHOR("Anders Torger <torger@ludd.luth.se>");

/*
 * Defines for RME Digi96 series, from internal RME reference documents
 * dated 12.01.00
 */

#define RME96_SPDIF_NCHANNELS 2

/* Playback and capture buffer size */
#define RME96_BUFFER_SIZE 0x10000

/* IO area size */
#define RME96_IO_SIZE 0x60000

/* IO area offsets */
#define RME96_IO_PLAY_BUFFER      0x0
#define RME96_IO_REC_BUFFER       0x10000
#define RME96_IO_CONTROL_REGISTER 0x20000
#define RME96_IO_ADDITIONAL_REG   0x20004
#define RME96_IO_CONFIRM_PLAY_IRQ 0x20008
#define RME96_IO_CONFIRM_REC_IRQ  0x2000C
#define RME96_IO_SET_PLAY_POS     0x40000
#define RME96_IO_RESET_PLAY_POS   0x4FFFC
#define RME96_IO_SET_REC_POS      0x50000
#define RME96_IO_RESET_REC_POS    0x5FFFC
#define RME96_IO_GET_PLAY_POS     0x20000
#define RME96_IO_GET_REC_POS      0x30000

/* Write control register bits */
#define RME96_WCR_START     (1 << 0)
#define RME96_WCR_START_2   (1 << 1)
#define RME96_WCR_GAIN_0    (1 << 2)
#define RME96_WCR_GAIN_1    (1 << 3)
#define RME96_WCR_MODE24    (1 << 4)
#define RME96_WCR_MODE24_2  (1 << 5)
#define RME96_WCR_BM        (1 << 6)
#define RME96_WCR_BM_2      (1 << 7)
#define RME96_WCR_ADAT      (1 << 8)
#define RME96_WCR_FREQ_0    (1 << 9)
#define RME96_WCR_FREQ_1    (1 << 10)
#define RME96_WCR_DS        (1 << 11)
#define RME96_WCR_PRO       (1 << 12)
#define RME96_WCR_EMP       (1 << 13)
#define RME96_WCR_SEL       (1 << 14)
#define RME96_WCR_MASTER    (1 << 15)
#define RME96_WCR_PD        (1 << 16)
#define RME96_WCR_INP_0     (1 << 17)
#define RME96_WCR_INP_1     (1 << 18)
#define RME96_WCR_THRU_0    (1 << 19)
#define RME96_WCR_THRU_1    (1 << 20)
#define RME96_WCR_THRU_2    (1 << 21)
#define RME96_WCR_THRU_3    (1 << 22)
#define RME96_WCR_THRU_4    (1 << 23)
#define RME96_WCR_THRU_5    (1 << 24)
#define RME96_WCR_THRU_6    (1 << 25)
#define RME96_WCR_THRU_7    (1 << 26)
#define RME96_WCR_DOLBY     (1 << 27)
#define RME96_WCR_MONITOR_0 (1 << 28)
#define RME96_WCR_MONITOR_1 (1 << 29)
#define RME96_WCR_ISEL      (1 << 30)
#define RME96_WCR_IDIS      (1 << 31)

#define RME96_WCR_BITPOS_GAIN_0 2
#define RME96_WCR_BITPOS_GAIN_1 3
#define RME96_WCR_BITPOS_FREQ_0 9
#define RME96_WCR_BITPOS_FREQ_1 10
#define RME96_WCR_BITPOS_INP_0 17
#define RME96_WCR_BITPOS_INP_1 18
#define RME96_WCR_BITPOS_MONITOR_0 28
#define RME96_WCR_BITPOS_MONITOR_1 29

/* Read control register bits */
#define RME96_RCR_AUDIO_ADDR_MASK 0xFFFF
#define RME96_RCR_IRQ_2     (1 << 16)
#define RME96_RCR_T_OUT     (1 << 17)
#define RME96_RCR_DEV_ID_0  (1 << 21)
#define RME96_RCR_DEV_ID_1  (1 << 22)
#define RME96_RCR_LOCK      (1 << 23)
#define RME96_RCR_VERF      (1 << 26)
#define RME96_RCR_F0        (1 << 27)
#define RME96_RCR_F1        (1 << 28)
#define RME96_RCR_F2        (1 << 29)
#define RME96_RCR_AUTOSYNC  (1 << 30)
#define RME96_RCR_IRQ       (1 << 31)

#define RME96_RCR_BITPOS_F0 27
#define RME96_RCR_BITPOS_F1 28
#define RME96_RCR_BITPOS_F2 29

/* Additonal register bits */
#define RME96_AR_WSEL       (1 << 0)
#define RME96_AR_ANALOG     (1 << 1)
#define RME96_AR_FREQPAD_0  (1 << 2)
#define RME96_AR_FREQPAD_1  (1 << 3)
#define RME96_AR_FREQPAD_2  (1 << 4)
#define RME96_AR_PD2        (1 << 5)
#define RME96_AR_DAC_EN     (1 << 6)
#define RME96_AR_CLATCH     (1 << 7)
#define RME96_AR_CCLK       (1 << 8)
#define RME96_AR_CDATA      (1 << 9)

#define RME96_AR_BITPOS_F0 2
#define RME96_AR_BITPOS_F1 3
#define RME96_AR_BITPOS_F2 4

/* Monitor tracks */
#define RME96_MONITOR_TRACKS_1_2 0
#define RME96_MONITOR_TRACKS_3_4 1
#define RME96_MONITOR_TRACKS_5_6 2
#define RME96_MONITOR_TRACKS_7_8 3

/* Attenuation */
#define RME96_ATTENUATION_0 0
#define RME96_ATTENUATION_6 1
#define RME96_ATTENUATION_12 2
#define RME96_ATTENUATION_18 3

/* Input types */
#define RME96_INPUT_OPTICAL 0
#define RME96_INPUT_COAXIAL 1
#define RME96_INPUT_INTERNAL 2
#define RME96_INPUT_XLR 3
#define RME96_INPUT_ANALOG 4

/* Clock modes */
#define RME96_CLOCKMODE_SLAVE 0
#define RME96_CLOCKMODE_MASTER 1
#define RME96_CLOCKMODE_WORDCLOCK 2

/* Block sizes in bytes */
#define RME96_SMALL_BLOCK_SIZE 2048
#define RME96_LARGE_BLOCK_SIZE 8192

/* Volume control */
#define RME96_AD1852_VOL_BITS 14
#define RME96_AD1855_VOL_BITS 10

/*
 * PCI vendor/device ids, could in the future be defined in <linux/pci.h>,
 * therefore #ifndef is used.
 */
#ifndef PCI_VENDOR_ID_XILINX
#define PCI_VENDOR_ID_XILINX 0x10ee
#endif
#ifndef PCI_DEVICE_ID_DIGI96
#define PCI_DEVICE_ID_DIGI96 0x3fc0
#endif
#ifndef PCI_DEVICE_ID_DIGI96_8
#define PCI_DEVICE_ID_DIGI96_8 0x3fc1
#endif
#ifndef PCI_DEVICE_ID_DIGI96_8_PRO
#define PCI_DEVICE_ID_DIGI96_8_PRO 0x3fc2
#endif
#ifndef PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST
#define PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST 0x3fc3
#endif

typedef struct snd_rme96 {
	spinlock_t    lock;
	int irq;
	unsigned long port;
	struct resource *res_port;
	unsigned long iobase;
	
	u32 wcreg;    /* cached write control register value */
	u32 wcreg_spdif;		/* S/PDIF setup */
	u32 wcreg_spdif_stream;		/* S/PDIF setup (temporary) */
	u32 rcreg;    /* cached read control register value */
	u32 areg;     /* cached additional register value */
	u16 vol[2]; /* cached volume of analog output */

	u8 rev; /* card revision number */

	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;

	int playback_frlog; /* log2 of framesize */
	int capture_frlog;
	
	size_t playback_periodsize; /* in bytes, zero if not used */
	size_t capture_periodsize; /* in bytes, zero if not used */

	size_t playback_ptr;
	size_t capture_ptr;

	snd_card_t         *card;
	snd_pcm_t          *spdif_pcm;
	snd_pcm_t          *adat_pcm; 
	struct pci_dev     *pci;
	snd_info_entry_t   *proc_entry;
	snd_kcontrol_t	   *spdif_ctl;
} rme96_t;

static struct pci_device_id snd_rme96_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_DIGI96,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_DIGI96_8,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_DIGI96_8_PRO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, 
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_rme96_ids);

#define RME96_ISPLAYING(rme96) ((rme96)->wcreg & RME96_WCR_START)
#define RME96_ISRECORDING(rme96) ((rme96)->wcreg & RME96_WCR_START_2)
#define	RME96_HAS_ANALOG_IN(rme96) ((rme96)->pci->device == PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST)
#define	RME96_HAS_ANALOG_OUT(rme96) ((rme96)->pci->device == PCI_DEVICE_ID_DIGI96_8_PRO || \
				     (rme96)->pci->device == PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST)
#define	RME96_DAC_IS_1852(rme96) (RME96_HAS_ANALOG_OUT(rme96) && (rme96)->rev >= 4)
#define	RME96_DAC_IS_1855(rme96) (((rme96)->pci->device == PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST && (rme96)->rev < 4) || \
			          ((rme96)->pci->device == PCI_DEVICE_ID_DIGI96_8_PRO && (rme96)->rev == 2))
#define	RME96_185X_MAX_OUT(rme96) ((1 << (RME96_DAC_IS_1852(rme96) ? RME96_AD1852_VOL_BITS : RME96_AD1855_VOL_BITS)) - 1)

static int
snd_rme96_playback_prepare(snd_pcm_substream_t *substream);

static int
snd_rme96_capture_prepare(snd_pcm_substream_t *substream);

static int
snd_rme96_playback_trigger(snd_pcm_substream_t *substream, 
			   int cmd);

static int
snd_rme96_capture_trigger(snd_pcm_substream_t *substream, 
			  int cmd);

static snd_pcm_uframes_t
snd_rme96_playback_pointer(snd_pcm_substream_t *substream);

static snd_pcm_uframes_t
snd_rme96_capture_pointer(snd_pcm_substream_t *substream);

static void __init 
snd_rme96_proc_init(rme96_t *rme96);

static void
snd_rme96_proc_done(rme96_t *rme96);

static int
snd_rme96_create_switches(snd_card_t *card,
			  rme96_t *rme96);

static inline unsigned int
snd_rme96_playback_ptr(rme96_t *rme96)
{
	return (readl(rme96->iobase + RME96_IO_GET_PLAY_POS)
		& RME96_RCR_AUDIO_ADDR_MASK) >> rme96->playback_frlog;
}

static inline unsigned int
snd_rme96_capture_ptr(rme96_t *rme96)
{
	return (readl(rme96->iobase + RME96_IO_GET_REC_POS)
		& RME96_RCR_AUDIO_ADDR_MASK) >> rme96->capture_frlog;
}

static int
snd_rme96_playback_silence(snd_pcm_substream_t *substream,
			   int channel, /* not used (interleaved data) */
			   snd_pcm_uframes_t pos,
			   snd_pcm_uframes_t count)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	count <<= rme96->playback_frlog;
	pos <<= rme96->playback_frlog;
	memset_io(rme96->iobase + RME96_IO_PLAY_BUFFER + pos,
		  0, count);
	return 0;
}

static int
snd_rme96_playback_copy(snd_pcm_substream_t *substream,
			int channel, /* not used (interleaved data) */
			snd_pcm_uframes_t pos,
			void *src,
			snd_pcm_uframes_t count)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	count <<= rme96->playback_frlog;
	pos <<= rme96->playback_frlog;
	copy_from_user_toio(rme96->iobase + RME96_IO_PLAY_BUFFER + pos, src,
			    count);
	return 0;
}

static int
snd_rme96_capture_copy(snd_pcm_substream_t *substream,
		       int channel, /* not used (interleaved data) */
		       snd_pcm_uframes_t pos,
		       void *dst,
		       snd_pcm_uframes_t count)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	count <<= rme96->capture_frlog;
	pos <<= rme96->capture_frlog;
	copy_to_user_fromio(dst, rme96->iobase + RME96_IO_REC_BUFFER + pos,
			    count);
        return 0;
}

/*
 * Digital output capabilites (S/PDIF)
 */
#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_rme96_playback_spdif_info =
{
/*	info:		  */   (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
/*	formats:	  */   (SNDRV_PCM_FMTBIT_S16_LE |
			      SNDRV_PCM_FMTBIT_S32_LE),
/*	rates:		  */   (SNDRV_PCM_RATE_32000 |
			      SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000 | 
			      SNDRV_PCM_RATE_64000 |
			      SNDRV_PCM_RATE_88200 | 
			      SNDRV_PCM_RATE_96000),
/*	rate_min:	  */   32000,
/*	rate_max:	  */   96000,
/*	channels_min:	  */   2,
/*	channels_max:	  */   2,
/*	buffer_bytes_max: */  RME96_BUFFER_SIZE,
/*	period_bytes_min: */  RME96_SMALL_BLOCK_SIZE,
/*	period_bytes_max: */  RME96_LARGE_BLOCK_SIZE,
/*	periods_min:	  */   RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
/*	periods_max:	  */  RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
/*	fifo_size:	  */   0,
};

/*
 * Digital input capabilites (S/PDIF)
 */
static snd_pcm_hardware_t snd_rme96_capture_spdif_info =
{
/*	info:		  */   (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
/*	formats:	  */   (SNDRV_PCM_FMTBIT_S16_LE |
			      SNDRV_PCM_FMTBIT_S32_LE),
/*	rates:		  */   (SNDRV_PCM_RATE_32000 |
			      SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000 | 
			      SNDRV_PCM_RATE_64000 |
			      SNDRV_PCM_RATE_88200 | 
			      SNDRV_PCM_RATE_96000),
/*	rate_min:	  */   32000,
/*	rate_max:	  */   96000,
/*	channels_min:	  */   2,
/*	channels_max:	  */   2,
/*	buffer_bytes_max: */  RME96_BUFFER_SIZE,
/*	period_bytes_min: */  RME96_SMALL_BLOCK_SIZE,
/*	period_bytes_max: */  RME96_LARGE_BLOCK_SIZE,
/*	periods_min:	  */   RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
/*	periods_max:	  */   RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
/*	fifo_size:	  */   0,
};

/*
 * Digital output capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme96_playback_adat_info =
{
/*	info:		  */   (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
/*	formats:          */   SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:	          */   (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
/*	rate_min:         */   44100,
/*	rate_max:         */   48000,
/*	channels_min:     */   8,
/*	channels_max:	  */   8,
/*	buffer_bytes_max: */  RME96_BUFFER_SIZE,
/*	period_bytes_min: */  RME96_SMALL_BLOCK_SIZE,
/*	period_bytes_max: */  RME96_LARGE_BLOCK_SIZE,
/*	periods_min:	  */   RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
/*	periods_max:	  */   RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
/*	fifo_size:	  */   0,
};

/*
 * Digital input capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme96_capture_adat_info =
{
/*	info:		  */   (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
/*	formats:          */   SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:	          */   (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
/*	rate_min:         */   44100,
/*	rate_max:         */   48000,
/*	channels_min:     */   8,
/*	channels_max:	  */   8,
/*	buffer_bytes_max: */  RME96_BUFFER_SIZE,
/*	period_bytes_min: */  RME96_SMALL_BLOCK_SIZE,
/*	period_bytes_max: */  RME96_LARGE_BLOCK_SIZE,
/*	periods_min:	  */   RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
/*	periods_max:	  */   RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
/*	fifo_size:        */   0,
};
#else
static snd_pcm_hardware_t snd_rme96_playback_spdif_info =
{
	info:		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	formats:	     (SNDRV_PCM_FMTBIT_S16_LE |
			      SNDRV_PCM_FMTBIT_S32_LE),
	rates:		     (SNDRV_PCM_RATE_32000 |
			      SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000 | 
			      SNDRV_PCM_RATE_64000 |
			      SNDRV_PCM_RATE_88200 | 
			      SNDRV_PCM_RATE_96000),
	rate_min:	     32000,
	rate_max:	     96000,
	channels_min:	     2,
	channels_max:	     2,
	buffer_bytes_max:   RME96_BUFFER_SIZE,
	period_bytes_min:   RME96_SMALL_BLOCK_SIZE,
	period_bytes_max:   RME96_LARGE_BLOCK_SIZE,
	periods_min:	     RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
	periods_max:	     RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
	fifo_size:	     0,
};

/*
 * Digital input capabilites (S/PDIF)
 */
static snd_pcm_hardware_t snd_rme96_capture_spdif_info =
{
	info:		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	formats:	     (SNDRV_PCM_FMTBIT_S16_LE |
			      SNDRV_PCM_FMTBIT_S32_LE),
	rates:		     (SNDRV_PCM_RATE_32000 |
			      SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000 | 
			      SNDRV_PCM_RATE_64000 |
			      SNDRV_PCM_RATE_88200 | 
			      SNDRV_PCM_RATE_96000),
	rate_min:	     32000,
	rate_max:	     96000,
	channels_min:	     2,
	channels_max:	     2,
	buffer_bytes_max:   RME96_BUFFER_SIZE,
	period_bytes_min:   RME96_SMALL_BLOCK_SIZE,
	period_bytes_max:   RME96_LARGE_BLOCK_SIZE,
	periods_min:	     RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
	periods_max:	     RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
	fifo_size:	     0,
};

/*
 * Digital output capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme96_playback_adat_info =
{
	info:		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	formats:             SNDRV_PCM_FMTBIT_S16_LE,
	rates:	             (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
	rate_min:            44100,
	rate_max:            48000,
	channels_min:        8,
	channels_max:	     8,
	buffer_bytes_max:   RME96_BUFFER_SIZE,
	period_bytes_min:   RME96_SMALL_BLOCK_SIZE,
	period_bytes_max:   RME96_LARGE_BLOCK_SIZE,
	periods_min:	     RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
	periods_max:	     RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
	fifo_size:	     0,
};

/*
 * Digital input capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme96_capture_adat_info =
{
	info:		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	formats:             SNDRV_PCM_FMTBIT_S16_LE,
	rates:	             (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
	rate_min:            44100,
	rate_max:            48000,
	channels_min:        8,
	channels_max:	     8,
	buffer_bytes_max:   RME96_BUFFER_SIZE,
	period_bytes_min:   RME96_SMALL_BLOCK_SIZE,
	period_bytes_max:   RME96_LARGE_BLOCK_SIZE,
	periods_min:	     RME96_BUFFER_SIZE / RME96_LARGE_BLOCK_SIZE,
	periods_max:	     RME96_BUFFER_SIZE / RME96_SMALL_BLOCK_SIZE,
	fifo_size:           0,
};
#endif

/*
 * The CDATA, CCLK and CLATCH bits can be used to write to the SPI interface
 * of the AD1852 or AD1852 D/A converter on the board.  CDATA must be set up
 * on the falling edge of CCLK and be stable on the rising edge.  The rising
 * edge of CLATCH after the last data bit clocks in the whole data word.
 * A fast processor could probably drive the SPI interface faster than the
 * DAC can handle (3MHz for the 1855, unknown for the 1852).  The udelay(1)
 * limits the data rate to 500KHz and only causes a delay of 33 microsecs.
 *
 * NOTE: increased delay from 1 to 10, since there where problems setting
 * the volume.
 */
static void
snd_rme96_write_SPI(rme96_t *rme96, u16 val)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (val & 0x8000) {
			rme96->areg |= RME96_AR_CDATA;
		} else {
			rme96->areg &= ~RME96_AR_CDATA;
		}
		rme96->areg &= ~(RME96_AR_CCLK | RME96_AR_CLATCH);
		writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
		udelay(10);
		rme96->areg |= RME96_AR_CCLK;
		writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
		udelay(10);
		val <<= 1;
	}
	rme96->areg &= ~(RME96_AR_CCLK | RME96_AR_CDATA);
	rme96->areg |= RME96_AR_CLATCH;
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
	udelay(10);
	rme96->areg &= ~RME96_AR_CLATCH;
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
}

static void
snd_rme96_apply_dac_volume(rme96_t *rme96)
{
	if (RME96_DAC_IS_1852(rme96)) {
		snd_rme96_write_SPI(rme96, (rme96->vol[0] << 2) | 0x0);
		snd_rme96_write_SPI(rme96, (rme96->vol[1] << 2) | 0x2);
	} else if (RME96_DAC_IS_1855(rme96)) {
		snd_rme96_write_SPI(rme96, (rme96->vol[0] & 0x3FF) | 0x000);
		snd_rme96_write_SPI(rme96, (rme96->vol[1] & 0x3FF) | 0x400);
	}
}

static void
snd_rme96_reset_dac(rme96_t *rme96)
{
	writel(rme96->wcreg | RME96_WCR_PD,
	       rme96->iobase + RME96_IO_CONTROL_REGISTER);
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static int
snd_rme96_getmontracks(rme96_t *rme96)
{
	return ((rme96->wcreg >> RME96_WCR_BITPOS_MONITOR_0) & 1) +
		(((rme96->wcreg >> RME96_WCR_BITPOS_MONITOR_1) & 1) << 1);
}

static int
snd_rme96_setmontracks(rme96_t *rme96,
		       int montracks)
{
	if (montracks & 1) {
		rme96->wcreg |= RME96_WCR_MONITOR_0;
	} else {
		rme96->wcreg &= ~RME96_WCR_MONITOR_0;
	}
	if (montracks & 2) {
		rme96->wcreg |= RME96_WCR_MONITOR_1;
	} else {
		rme96->wcreg &= ~RME96_WCR_MONITOR_1;
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	return 0;
}

static int
snd_rme96_getattenuation(rme96_t *rme96)
{
	return ((rme96->wcreg >> RME96_WCR_BITPOS_GAIN_0) & 1) +
		(((rme96->wcreg >> RME96_WCR_BITPOS_GAIN_1) & 1) << 1);
}

static int
snd_rme96_setattenuation(rme96_t *rme96,
			 int attenuation)
{
	switch (attenuation) {
	case 0:
		rme96->wcreg = (rme96->wcreg & ~RME96_WCR_GAIN_0) &
			~RME96_WCR_GAIN_1;
		break;
	case 1:
		rme96->wcreg = (rme96->wcreg | RME96_WCR_GAIN_0) &
			~RME96_WCR_GAIN_1;
		break;
	case 2:
		rme96->wcreg = (rme96->wcreg & ~RME96_WCR_GAIN_0) |
			RME96_WCR_GAIN_1;
		break;
	case 3:
		rme96->wcreg = (rme96->wcreg | RME96_WCR_GAIN_0) |
			RME96_WCR_GAIN_1;
		break;
	default:
		return -EINVAL;
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	return 0;
}


static int
snd_rme96_playback_getrate(rme96_t *rme96)
{
	int rate;
	
	rate = ((rme96->wcreg >> RME96_WCR_BITPOS_FREQ_0) & 1) +
		(((rme96->wcreg >> RME96_WCR_BITPOS_FREQ_1) & 1) << 1);
	switch (rate) {
	case 1:
		rate = 32000;
		break;
	case 2:
		rate = 44100;
		break;
	case 3:
		rate = 48000;
		break;
	default:
		return -1;
	}
	return (rme96->wcreg & RME96_WCR_DS) ? rate << 1 : rate;
}

static int
snd_rme96_capture_getrate(rme96_t *rme96,
			  int *is_adat)
{	
	int n, rate;

	*is_adat = 0;
	if (rme96->areg & RME96_AR_ANALOG) {
		/* Analog input, overrides S/PDIF setting */
		n = ((rme96->areg >> RME96_AR_BITPOS_F0) & 1) +
			(((rme96->areg >> RME96_AR_BITPOS_F1) & 1) << 1);
		switch (n) {
		case 1:
			rate = 32000;
			break;
		case 2:
			rate = 44100;
			break;
		case 3:
			rate = 48000;
			break;
		default:
			return -1;
		}
		return (rme96->areg & RME96_AR_BITPOS_F2) ? rate << 1 : rate;
	}

	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	if (rme96->rcreg & RME96_RCR_LOCK) {
		/* ADAT rate */
		*is_adat = 1;
		if (rme96->rcreg & RME96_RCR_T_OUT) {
			return 48000;
		}
		return 44100;
	}

	if (rme96->rcreg & RME96_RCR_VERF) {
		return -1;
	}
	
	/* S/PDIF rate */
	n = ((rme96->rcreg >> RME96_RCR_BITPOS_F0) & 1) +
		(((rme96->rcreg >> RME96_RCR_BITPOS_F1) & 1) << 1) +
		(((rme96->rcreg >> RME96_RCR_BITPOS_F2) & 1) << 2);
	
	switch (n) {
	case 0:		
		if (rme96->rcreg & RME96_RCR_T_OUT) {
			return 64000;
		}
		return -1;
	case 3: return 96000;
	case 4: return 88200;
	case 5: return 48000;
	case 6: return 44100;
	case 7: return 32000;
	default:
		break;
	}
	return -1;
}

static int
snd_rme96_playback_setrate(rme96_t *rme96,
			   int rate)
{
	int ds;

	ds = rme96->wcreg & RME96_WCR_DS;
	switch (rate) {
	case 32000:
		rme96->wcreg &= ~RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_0) &
			~RME96_WCR_FREQ_1;
		break;
	case 44100:
		rme96->wcreg &= ~RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_1) &
			~RME96_WCR_FREQ_0;
		break;
	case 48000:
		rme96->wcreg &= ~RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_0) |
			RME96_WCR_FREQ_1;
		break;
	case 64000:
		rme96->wcreg |= RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_0) &
			~RME96_WCR_FREQ_1;
		break;
	case 88200:
		rme96->wcreg |= RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_1) &
			~RME96_WCR_FREQ_0;
		break;
	case 96000:
		rme96->wcreg |= RME96_WCR_DS;
		rme96->wcreg = (rme96->wcreg | RME96_WCR_FREQ_0) |
			RME96_WCR_FREQ_1;
		break;
	default:
		return -EINVAL;
	}
	if ((!ds && rme96->wcreg & RME96_WCR_DS) ||
	    (ds && !(rme96->wcreg & RME96_WCR_DS)))
	{
		/* change to/from double-speed: reset the DAC (if available) */
		snd_rme96_reset_dac(rme96);
	} else {
		writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	}
	return 0;
}

static int
snd_rme96_capture_analog_setrate(rme96_t *rme96,
				 int rate)
{
	switch (rate) {
	case 32000:
		rme96->areg = ((rme96->areg | RME96_AR_FREQPAD_0) &
			       ~RME96_AR_FREQPAD_1) & ~RME96_AR_FREQPAD_2;
		break;
	case 44100:
		rme96->areg = ((rme96->areg & ~RME96_AR_FREQPAD_0) |
			       RME96_AR_FREQPAD_1) & ~RME96_AR_FREQPAD_2;
		break;
	case 48000:
		rme96->areg = ((rme96->areg | RME96_AR_FREQPAD_0) |
			       RME96_AR_FREQPAD_1) & ~RME96_AR_FREQPAD_2;
		break;
	case 64000:
		if (rme96->rev < 4) {
			return -EINVAL;
		}
		rme96->areg = ((rme96->areg | RME96_AR_FREQPAD_0) &
			       ~RME96_AR_FREQPAD_1) | RME96_AR_FREQPAD_2;
		break;
	case 88200:
		if (rme96->rev < 4) {
			return -EINVAL;
		}
		rme96->areg = ((rme96->areg & ~RME96_AR_FREQPAD_0) |
			       RME96_AR_FREQPAD_1) | RME96_AR_FREQPAD_2;
		break;
	case 96000:
		rme96->areg = ((rme96->areg | RME96_AR_FREQPAD_0) |
			       RME96_AR_FREQPAD_1) | RME96_AR_FREQPAD_2;
		break;
	default:
		return -EINVAL;
	}
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
	return 0;
}

static int
snd_rme96_setclockmode(rme96_t *rme96,
		       int mode)
{
	switch (mode) {
	case RME96_CLOCKMODE_SLAVE:
		rme96->wcreg &= ~RME96_WCR_MASTER;
		rme96->areg &= ~RME96_AR_WSEL;
		break;
	case RME96_CLOCKMODE_MASTER:
		rme96->wcreg |= RME96_WCR_MASTER;
		rme96->areg &= ~RME96_AR_WSEL;
		break;
	case RME96_CLOCKMODE_WORDCLOCK:
		/* Word clock is a master mode */
		rme96->wcreg |= RME96_WCR_MASTER; 
		rme96->areg |= RME96_AR_WSEL;
		break;
	default:
		return -EINVAL;
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
	return 0;
}

static int
snd_rme96_getclockmode(rme96_t *rme96)
{
	if (rme96->areg & RME96_AR_WSEL) {
		return RME96_CLOCKMODE_WORDCLOCK;
	}
	return (rme96->wcreg & RME96_WCR_MASTER) ? RME96_CLOCKMODE_MASTER :
		RME96_CLOCKMODE_SLAVE;
}

static int
snd_rme96_setinputtype(rme96_t *rme96,
		       int type)
{
	int n;

	switch (type) {
	case RME96_INPUT_OPTICAL:
		rme96->wcreg = (rme96->wcreg & ~RME96_WCR_INP_0) &
			~RME96_WCR_INP_1;
		break;
	case RME96_INPUT_COAXIAL:
		rme96->wcreg = (rme96->wcreg | RME96_WCR_INP_0) &
			~RME96_WCR_INP_1;
		break;
	case RME96_INPUT_INTERNAL:
		rme96->wcreg = (rme96->wcreg & ~RME96_WCR_INP_0) |
			RME96_WCR_INP_1;
		break;
	case RME96_INPUT_XLR:
		if (rme96->pci->device != PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST ||
		    rme96->pci->device != PCI_DEVICE_ID_DIGI96_8_PRO ||
		    (rme96->pci->device == PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST &&
		     rme96->rev > 4))
		{
			/* Only Digi96/8 PRO and Digi96/8 PAD supports XLR */
			return -EINVAL;
		}
		rme96->wcreg = (rme96->wcreg | RME96_WCR_INP_0) |
			RME96_WCR_INP_1;
		break;
	case RME96_INPUT_ANALOG:
		if (!RME96_HAS_ANALOG_IN(rme96)) {
			return -EINVAL;
		}
		rme96->areg |= RME96_AR_ANALOG;
		writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
		if (rme96->rev < 4) {
			/*
			 * Revision less than 004 does not support 64 and
			 * 88.2 kHz
			 */
			if (snd_rme96_capture_getrate(rme96, &n) == 88200) {
				snd_rme96_capture_analog_setrate(rme96, 44100);
			}
			if (snd_rme96_capture_getrate(rme96, &n) == 64000) {
				snd_rme96_capture_analog_setrate(rme96, 32000);
			}
		}
		return 0;
	default:
		return -EINVAL;
	}
	if (type != RME96_INPUT_ANALOG && RME96_HAS_ANALOG_IN(rme96)) {
		rme96->areg &= ~RME96_AR_ANALOG;
		writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	return 0;
}

static int
snd_rme96_getinputtype(rme96_t *rme96)
{
	if (rme96->areg & RME96_AR_ANALOG) {
		return RME96_INPUT_ANALOG;
	}
	return ((rme96->wcreg >> RME96_WCR_BITPOS_INP_0) & 1) +
		(((rme96->wcreg >> RME96_WCR_BITPOS_INP_1) & 1) << 1);
}

static void
snd_rme96_setframelog(rme96_t *rme96,
		      int n_channels,
		      int is_playback)
{
	int frlog;
	
	if (n_channels == 2) {
		frlog = 1;
	} else {
		/* assume 8 channels */
		frlog = 3;
	}
	if (is_playback) {
		frlog += (rme96->wcreg & RME96_WCR_MODE24) ? 2 : 1;
		rme96->playback_frlog = frlog;
	} else {
		frlog += (rme96->wcreg & RME96_WCR_MODE24_2) ? 2 : 1;
		rme96->capture_frlog = frlog;
	}
}

static int
snd_rme96_playback_setformat(rme96_t *rme96,
			     int format)
{
	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		rme96->wcreg &= ~RME96_WCR_MODE24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		rme96->wcreg |= RME96_WCR_MODE24;
		break;
	default:
		return -EINVAL;
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	return 0;
}

static int
snd_rme96_capture_setformat(rme96_t *rme96,
			    int format)
{
	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		rme96->wcreg &= ~RME96_WCR_MODE24_2;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		rme96->wcreg |= RME96_WCR_MODE24_2;
		break;
	default:
		return -EINVAL;
	}
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	return 0;
}

static void
snd_rme96_set_period_properties(rme96_t *rme96,
				size_t period_bytes)
{
	switch (period_bytes) {
	case RME96_LARGE_BLOCK_SIZE:
		rme96->wcreg &= ~RME96_WCR_ISEL;
		break;
	case RME96_SMALL_BLOCK_SIZE:
		rme96->wcreg |= RME96_WCR_ISEL;
		break;
	default:
		snd_BUG();
		break;
	}
	rme96->wcreg &= ~RME96_WCR_IDIS;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static int
snd_rme96_playback_hw_params(snd_pcm_substream_t *substream,
			     snd_pcm_hw_params_t *params)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params))) < 0)
		return err;
	spin_lock_irqsave(&rme96->lock, flags);
	if ((err = snd_rme96_playback_setrate(rme96, params_rate(params))) < 0) {
		spin_unlock_irqrestore(&rme96->lock, flags);
		return err;
	}
	if ((err = snd_rme96_playback_setformat(rme96, params_format(params))) < 0) {
		spin_unlock_irqrestore(&rme96->lock, flags);
		return err;
	}
	snd_rme96_setframelog(rme96, params_channels(params), 1);
	if (rme96->capture_periodsize != 0) {
		if (params_period_size(params) << rme96->playback_frlog !=
		    rme96->capture_periodsize)
		{
			spin_unlock_irqrestore(&rme96->lock, flags);
			return -EBUSY;
		}
	}
	rme96->playback_periodsize =
		params_period_size(params) << rme96->playback_frlog;
	snd_rme96_set_period_properties(rme96, rme96->playback_periodsize);
	/* S/PDIF setup */
	if ((rme96->wcreg & RME96_WCR_ADAT) == 0) {
		rme96->wcreg &= ~(RME96_WCR_PRO | RME96_WCR_DOLBY | RME96_WCR_EMP);
		writel(rme96->wcreg |= rme96->wcreg_spdif_stream, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	}
	spin_unlock_irqrestore(&rme96->lock, flags);
		
	return 0;
}

static int
snd_rme96_playback_hw_free(snd_pcm_substream_t *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int
snd_rme96_capture_hw_params(snd_pcm_substream_t *substream,
			    snd_pcm_hw_params_t *params)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	int err, isadat;
	
	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params))) < 0)
		return err;
	spin_lock_irqsave(&rme96->lock, flags);
	if ((err = snd_rme96_capture_setformat(rme96, params_format(params))) < 0) {
		spin_unlock_irqrestore(&rme96->lock, flags);
		return err;
	}
	if (snd_rme96_getinputtype(rme96) == RME96_INPUT_ANALOG) {
		if ((err = snd_rme96_capture_analog_setrate(rme96,
							    params_rate(params))) < 0)
		{
			spin_unlock_irqrestore(&rme96->lock, flags);
			return err;
		}
	} else if (params_rate(params) != snd_rme96_capture_getrate(rme96, &isadat)) {
		spin_unlock_irqrestore(&rme96->lock, flags);
		return -EBUSY;
	}
	snd_rme96_setframelog(rme96, params_channels(params), 0);
	if (rme96->playback_periodsize != 0) {
		if (params_period_size(params) << rme96->capture_frlog !=
		    rme96->playback_periodsize)
		{
			spin_unlock_irqrestore(&rme96->lock, flags);
			return -EBUSY;
		}
	}
	rme96->capture_periodsize =
		params_period_size(params) << rme96->capture_frlog;
	snd_rme96_set_period_properties(rme96, rme96->capture_periodsize);
	spin_unlock_irqrestore(&rme96->lock, flags);

	return 0;
}

static int
snd_rme96_capture_hw_free(snd_pcm_substream_t *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static void
snd_rme96_playback_start(rme96_t *rme96,
			 int from_pause)
{
	snd_pcm_runtime_t *runtime = rme96->playback_substream->runtime;
	
	if (!from_pause) {
		writel(0, rme96->iobase + RME96_IO_RESET_PLAY_POS);
		if (runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
			memcpy_toio(rme96->iobase + RME96_IO_PLAY_BUFFER,
				    runtime->dma_area,
				    rme96->playback_periodsize);
			rme96->playback_ptr = rme96->playback_periodsize;
		}
	}

	rme96->wcreg |= RME96_WCR_START;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static void
snd_rme96_capture_start(rme96_t *rme96,
			int from_pause)
{
	if (!from_pause) {
		writel(0, rme96->iobase + RME96_IO_RESET_REC_POS);
	}

	rme96->wcreg |= RME96_WCR_START_2;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static void
snd_rme96_playback_stop(rme96_t *rme96)
{
	/*
	 * Check if there is an unconfirmed IRQ, if so confirm it, or else
	 * the hardware will not stop generating interrupts
	 */
	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	if (rme96->rcreg & RME96_RCR_IRQ) {
		writel(0, rme96->iobase + RME96_IO_CONFIRM_PLAY_IRQ);
	}	
	rme96->wcreg &= ~RME96_WCR_START;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static void
snd_rme96_capture_stop(rme96_t *rme96)
{
	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	if (rme96->rcreg & RME96_RCR_IRQ_2) {
		writel(0, rme96->iobase + RME96_IO_CONFIRM_REC_IRQ);
	}	
	rme96->wcreg &= ~RME96_WCR_START_2;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
}

static void
snd_rme96_interrupt(int irq,
		    void *dev_id,
		    struct pt_regs *regs)
{
	rme96_t *rme96 = (rme96_t *)dev_id;
	snd_pcm_runtime_t *runtime;

	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	/* fastpath out, to ease interrupt sharing */
	if (!((rme96->rcreg & RME96_RCR_IRQ) ||
	      (rme96->rcreg & RME96_RCR_IRQ_2)))
	{
		return;
	}
	
	if (rme96->rcreg & RME96_RCR_IRQ) {
		/* playback */
		runtime = rme96->playback_substream->runtime;
		if (runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
			memcpy_toio(rme96->iobase + RME96_IO_PLAY_BUFFER +
				    rme96->playback_ptr,
				    runtime->dma_area + rme96->playback_ptr,
				    rme96->playback_periodsize);
			rme96->playback_ptr += rme96->playback_periodsize;
			if (rme96->playback_ptr == RME96_BUFFER_SIZE) {
				rme96->playback_ptr = 0;
			}
		}
		snd_pcm_period_elapsed(rme96->playback_substream);
		writel(0, rme96->iobase + RME96_IO_CONFIRM_PLAY_IRQ);
	}
	if (rme96->rcreg & RME96_RCR_IRQ_2) {
		/* capture */
		runtime = rme96->capture_substream->runtime;
		if (runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
			memcpy_fromio(runtime->dma_area + rme96->capture_ptr,
				      rme96->iobase + RME96_IO_REC_BUFFER +
				      rme96->capture_ptr,
				      rme96->capture_periodsize);
			rme96->capture_ptr += rme96->capture_periodsize;
			if (rme96->capture_ptr == RME96_BUFFER_SIZE) {
				rme96->capture_ptr = 0;
			}
		}
		snd_pcm_period_elapsed(rme96->capture_substream);		
		writel(0, rme96->iobase + RME96_IO_CONFIRM_REC_IRQ);
	}
}

static unsigned int period_bytes[] = { RME96_SMALL_BLOCK_SIZE, RME96_LARGE_BLOCK_SIZE };

#define PERIOD_BYTES sizeof(period_bytes) / sizeof(period_bytes[0])

#ifdef TARGET_OS2
static snd_pcm_hw_constraint_list_t hw_constraints_period_bytes = {
	PERIOD_BYTES,
	period_bytes,
	0
};
#else
static snd_pcm_hw_constraint_list_t hw_constraints_period_bytes = {
	count: PERIOD_BYTES,
	list: period_bytes,
	mask: 0
};
#endif

static int
snd_rme96_playback_spdif_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->wcreg &= ~RME96_WCR_ADAT;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	rme96->playback_substream = substream;
	rme96->playback_ptr = 0;
	spin_unlock_irqrestore(&rme96->lock, flags);

	runtime->hw = snd_rme96_playback_spdif_info;	
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);

	rme96->wcreg_spdif_stream = rme96->wcreg_spdif;
	rme96->spdif_ctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(rme96->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &rme96->spdif_ctl->id);
	return 0;
}

static int
snd_rme96_capture_spdif_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	int isadat;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	if (snd_rme96_capture_getrate(rme96, &isadat) < 0) {
		/* no input */
		return -EIO;
	}
	if (isadat) {
		/* ADAT input */
		return -EBUSY;
	}
	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->capture_substream = substream;
	rme96->capture_ptr = 0;
	spin_unlock_irqrestore(&rme96->lock, flags);
	
	runtime->hw = snd_rme96_capture_spdif_info;
	
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);

	return 0;
}

static int
snd_rme96_playback_adat_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	
	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->wcreg |= RME96_WCR_ADAT;
	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	rme96->playback_substream = substream;
	rme96->playback_ptr = 0;
	spin_unlock_irqrestore(&rme96->lock, flags);
	
	runtime->hw = snd_rme96_playback_adat_info;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);
	return 0;
}

static int
snd_rme96_capture_adat_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	int isadat;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);
	if (snd_rme96_capture_getrate(rme96, &isadat) < 0) {
		/* no input */
		return -EIO;
	}
	if (!isadat) {
		/* S/PDIF input */
		return -EBUSY;
	}
	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->capture_substream = substream;
	rme96->capture_ptr = 0;
	spin_unlock_irqrestore(&rme96->lock, flags);

	runtime->hw = snd_rme96_capture_adat_info;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);
	return 0;
}

static int
snd_rme96_playback_close(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	int spdif = 0;

	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->playback_substream = NULL;
	rme96->playback_periodsize = 0;
	spdif = (rme96->wcreg & RME96_WCR_ADAT) == 0;
	spin_unlock_irqrestore(&rme96->lock, flags);
	if (spdif) {
		rme96->spdif_ctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(rme96->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO, &rme96->spdif_ctl->id);
	}
	return 0;
}

static int
snd_rme96_capture_close(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	
	spin_lock_irqsave(&rme96->lock, flags);	
	rme96->capture_substream = NULL;
	rme96->capture_periodsize = 0;
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}

static int
snd_rme96_playback_prepare(snd_pcm_substream_t *substream)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);	
	if (RME96_ISPLAYING(rme96)) {
		snd_rme96_playback_stop(rme96);
	}
	writel(0, rme96->iobase + RME96_IO_RESET_PLAY_POS);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}

static int
snd_rme96_capture_prepare(snd_pcm_substream_t *substream)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);	
	if (RME96_ISRECORDING(rme96)) {
		snd_rme96_capture_stop(rme96);
	}
	writel(0, rme96->iobase + RME96_IO_RESET_REC_POS);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}

static int
snd_rme96_playback_trigger(snd_pcm_substream_t *substream, 
			   int cmd)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!RME96_ISPLAYING(rme96)) {
			if (substream != rme96->playback_substream) {
				return -EBUSY;
			}
			snd_rme96_playback_start(rme96, 0);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (RME96_ISPLAYING(rme96)) {
			if (substream != rme96->playback_substream) {
				return -EBUSY;
			}
			snd_rme96_playback_stop(rme96);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (RME96_ISPLAYING(rme96)) {
			snd_rme96_playback_stop(rme96);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!RME96_ISPLAYING(rme96)) {
			snd_rme96_playback_start(rme96, 1);
		}
		break;
		
	default:
		return -EINVAL;
	}
	return 0;
}

static int
snd_rme96_capture_trigger(snd_pcm_substream_t *substream, 
			  int cmd)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!RME96_ISRECORDING(rme96)) {
			if (substream != rme96->capture_substream) {
				return -EBUSY;
			}
			snd_rme96_capture_start(rme96, 0);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (RME96_ISRECORDING(rme96)) {
			if (substream != rme96->capture_substream) {
				return -EBUSY;
			}
			snd_rme96_capture_stop(rme96);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (RME96_ISRECORDING(rme96)) {
			snd_rme96_capture_stop(rme96);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!RME96_ISRECORDING(rme96)) {
			snd_rme96_capture_start(rme96, 1);
		}
		break;
		
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t
snd_rme96_playback_pointer(snd_pcm_substream_t *substream)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	return snd_rme96_playback_ptr(rme96);
}

static snd_pcm_uframes_t
snd_rme96_capture_pointer(snd_pcm_substream_t *substream)
{
	rme96_t *rme96 = _snd_pcm_substream_chip(substream);
	return snd_rme96_capture_ptr(rme96);
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_rme96_playback_spdif_ops = {
/*	open:	  */	snd_rme96_playback_spdif_open,
/*	close:	  */	snd_rme96_playback_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_rme96_playback_hw_params,
/*	hw_free:  */	snd_rme96_playback_hw_free,
/*	prepare:  */	snd_rme96_playback_prepare,
/*	trigger:  */	snd_rme96_playback_trigger,
/*	pointer:  */	snd_rme96_playback_pointer,
/*	copy:	  */	snd_rme96_playback_copy,
/*	silence:  */	snd_rme96_playback_silence,
};

static snd_pcm_ops_t snd_rme96_capture_spdif_ops = {
/*	open:	  */	snd_rme96_capture_spdif_open,
/*	close:	  */	snd_rme96_capture_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_rme96_capture_hw_params,
/*	hw_free:  */	snd_rme96_capture_hw_free,
/*	prepare:  */	snd_rme96_capture_prepare,
/*	trigger:  */	snd_rme96_capture_trigger,
/*	pointer:  */	snd_rme96_capture_pointer,
/*	copy:	  */	snd_rme96_capture_copy,
        0
};

static snd_pcm_ops_t snd_rme96_playback_adat_ops = {
/*	open:	  */	snd_rme96_playback_adat_open,
/*	close:	  */	snd_rme96_playback_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_rme96_playback_hw_params,
/*	hw_free:  */	snd_rme96_playback_hw_free,
/*	prepare:  */	snd_rme96_playback_prepare,
/*	trigger:  */	snd_rme96_playback_trigger,
/*	pointer:  */	snd_rme96_playback_pointer,
/*	copy:	  */	snd_rme96_playback_copy,
/*	silence:  */	snd_rme96_playback_silence,
};

static snd_pcm_ops_t snd_rme96_capture_adat_ops = {
/*	open:	  */	snd_rme96_capture_adat_open,
/*	close:	  */	snd_rme96_capture_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_rme96_capture_hw_params,
/*	hw_free:  */	snd_rme96_capture_hw_free,
/*	prepare:  */	snd_rme96_capture_prepare,
/*	trigger:  */	snd_rme96_capture_trigger,
/*	pointer:  */	snd_rme96_capture_pointer,
/*	copy:	  */	snd_rme96_capture_copy,
};
#else
static snd_pcm_ops_t snd_rme96_playback_spdif_ops = {
	open:		snd_rme96_playback_spdif_open,
	close:		snd_rme96_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_rme96_playback_hw_params,
	hw_free:	snd_rme96_playback_hw_free,
	prepare:	snd_rme96_playback_prepare,
	trigger:	snd_rme96_playback_trigger,
	pointer:	snd_rme96_playback_pointer,
	copy:		snd_rme96_playback_copy,
	silence:	snd_rme96_playback_silence,
};

static snd_pcm_ops_t snd_rme96_capture_spdif_ops = {
	open:		snd_rme96_capture_spdif_open,
	close:		snd_rme96_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_rme96_capture_hw_params,
	hw_free:	snd_rme96_capture_hw_free,
	prepare:	snd_rme96_capture_prepare,
	trigger:	snd_rme96_capture_trigger,
	pointer:	snd_rme96_capture_pointer,
	copy:		snd_rme96_capture_copy,
};

static snd_pcm_ops_t snd_rme96_playback_adat_ops = {
	open:		snd_rme96_playback_adat_open,
	close:		snd_rme96_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_rme96_playback_hw_params,
	hw_free:	snd_rme96_playback_hw_free,
	prepare:	snd_rme96_playback_prepare,
	trigger:	snd_rme96_playback_trigger,
	pointer:	snd_rme96_playback_pointer,
	copy:		snd_rme96_playback_copy,
	silence:	snd_rme96_playback_silence,
};

static snd_pcm_ops_t snd_rme96_capture_adat_ops = {
	open:		snd_rme96_capture_adat_open,
	close:		snd_rme96_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_rme96_capture_hw_params,
	hw_free:	snd_rme96_capture_hw_free,
	prepare:	snd_rme96_capture_prepare,
	trigger:	snd_rme96_capture_trigger,
	pointer:	snd_rme96_capture_pointer,
	copy:		snd_rme96_capture_copy,
};
#endif

static void
snd_rme96_free(void *private_data)
{
	rme96_t *rme96 = (rme96_t *)private_data;

	if (rme96 == NULL) {
	        return;
	}
	if (rme96->irq >= 0) {
		snd_rme96_playback_stop(rme96);
		snd_rme96_capture_stop(rme96);
		rme96->areg &= ~RME96_AR_DAC_EN;
		writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
		snd_rme96_proc_done(rme96);
		free_irq(rme96->irq, (void *)rme96);
		rme96->irq = -1;
	}
	if (rme96->iobase) {
		iounmap((void *)rme96->iobase);
		rme96->iobase = 0;
	}
	if (rme96->res_port != NULL) {
		release_resource(rme96->res_port);
		rme96->res_port = NULL;
	}
}

static void
snd_rme96_free_spdif_pcm(snd_pcm_t *pcm)
{
	rme96_t *rme96 = (rme96_t *) pcm->private_data;
	rme96->spdif_pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static void
snd_rme96_free_adat_pcm(snd_pcm_t *pcm)
{
	rme96_t *rme96 = (rme96_t *) pcm->private_data;
	rme96->adat_pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __init
snd_rme96_create(rme96_t *rme96)
{
	struct pci_dev *pci = rme96->pci;
	int err;

	rme96->irq = -1;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	rme96->port = pci_resource_start(rme96->pci, 0);

	if ((rme96->res_port = request_mem_region(rme96->port, RME96_IO_SIZE, "RME96")) == NULL) {
		snd_printk("unable to grab memory region 0x%lx-0x%lx\n", rme96->port, rme96->port + RME96_IO_SIZE - 1);
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_rme96_interrupt, SA_INTERRUPT|SA_SHIRQ, "RME96", (void *)rme96)) {
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	rme96->irq = pci->irq;

	spin_lock_init(&rme96->lock);
	if ((rme96->iobase = (unsigned long) ioremap(rme96->port, RME96_IO_SIZE)) == 0) {
		snd_printk("unable to remap memory region 0x%lx-0x%lx\n", rme96->port, rme96->port + RME96_IO_SIZE - 1);
		return -ENOMEM;
	}

	/* read the card's revision number */
	pci_read_config_byte(pci, 8, &rme96->rev);	
	
	/* set up ALSA pcm device for S/PDIF */
	if ((err = snd_pcm_new(rme96->card, "Digi96 IEC958", 0,
			       1, 1, &rme96->spdif_pcm)) < 0)
	{
		return err;
	}
	rme96->spdif_pcm->private_data = rme96;
	rme96->spdif_pcm->private_free = snd_rme96_free_spdif_pcm;
	strcpy(rme96->spdif_pcm->name, "Digi96 IEC958");
	snd_pcm_set_ops(rme96->spdif_pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_rme96_playback_spdif_ops);
	snd_pcm_set_ops(rme96->spdif_pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_rme96_capture_spdif_ops);

	rme96->spdif_pcm->info_flags = 0;

	snd_pcm_lib_preallocate_pages_for_all(rme96->spdif_pcm, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE, GFP_KERNEL);

	/* set up ALSA pcm device for ADAT */
	if (pci->device == PCI_DEVICE_ID_DIGI96) {
		/* ADAT is not available on the base model */
		rme96->adat_pcm = NULL;
	} else {
		if ((err = snd_pcm_new(rme96->card, "Digi96 ADAT", 1,
				       1, 1, &rme96->adat_pcm)) < 0)
		{
			return err;
		}		
		rme96->adat_pcm->private_data = rme96;
		rme96->adat_pcm->private_free = snd_rme96_free_adat_pcm;
		strcpy(rme96->adat_pcm->name, "Digi96 ADAT");
		snd_pcm_set_ops(rme96->adat_pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_rme96_playback_adat_ops);
		snd_pcm_set_ops(rme96->adat_pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_rme96_capture_adat_ops);
		
		rme96->adat_pcm->info_flags = 0;

		snd_pcm_lib_preallocate_pages_for_all(rme96->adat_pcm, RME96_BUFFER_SIZE, RME96_BUFFER_SIZE, GFP_KERNEL);
	}

	rme96->playback_periodsize = 0;
	rme96->capture_periodsize = 0;
	
	/* make sure playback/capture is stopped, if by some reason active */
	snd_rme96_playback_stop(rme96);
	snd_rme96_capture_stop(rme96);
	
	/* set default values in registers */
	rme96->wcreg =
		RME96_WCR_FREQ_1 | /* set 44.1 kHz playback */
		RME96_WCR_SEL |    /* normal playback */
		RME96_WCR_MASTER | /* set to master clock mode */
		RME96_WCR_INP_0;   /* set coaxial input */

	rme96->areg = RME96_AR_FREQPAD_1; /* set 44.1 kHz analog capture */

	writel(rme96->wcreg, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);
	
	/* reset the ADC */
	writel(rme96->areg | RME96_AR_PD2,
	       rme96->iobase + RME96_IO_ADDITIONAL_REG);
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);	

	/* reset and enable the DAC (order is important). */
	snd_rme96_reset_dac(rme96);
	rme96->areg |= RME96_AR_DAC_EN;
	writel(rme96->areg, rme96->iobase + RME96_IO_ADDITIONAL_REG);

	/* reset playback and record buffer pointers */
	writel(0, rme96->iobase + RME96_IO_RESET_PLAY_POS);
	writel(0, rme96->iobase + RME96_IO_RESET_REC_POS);

	/* reset volume */
	rme96->vol[0] = rme96->vol[1] = 0;
	if (RME96_HAS_ANALOG_OUT(rme96)) {
		snd_rme96_apply_dac_volume(rme96);
	}
	
	/* init switch interface */
	if ((err = snd_rme96_create_switches(rme96->card, rme96)) < 0) {
		return err;
	}

        /* init proc interface */
	snd_rme96_proc_init(rme96);
	
	return 0;
}

/*
 * proc interface
 */

static void 
snd_rme96_proc_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	int n;
	rme96_t *rme96 = (rme96_t *)entry->private_data;
	
	rme96->rcreg = readl(rme96->iobase + RME96_IO_CONTROL_REGISTER);

	snd_iprintf(buffer, rme96->card->longname);
	snd_iprintf(buffer, " (index #%d)\n", rme96->card->number + 1);

	snd_iprintf(buffer, "\nGeneral settings\n");
	if (rme96->wcreg & RME96_WCR_IDIS) {
		snd_iprintf(buffer, "  period size: N/A (interrupts "
			    "disabled)\n");
	} else if (rme96->wcreg & RME96_WCR_ISEL) {
		snd_iprintf(buffer, "  period size: 2048 bytes\n");
	} else {
		snd_iprintf(buffer, "  period size: 8192 bytes\n");
	}	
	snd_iprintf(buffer, "\nInput settings\n");
	switch (snd_rme96_getinputtype(rme96)) {
	case RME96_INPUT_OPTICAL:
		snd_iprintf(buffer, "  input: optical");
		break;
	case RME96_INPUT_COAXIAL:
		snd_iprintf(buffer, "  input: coaxial");
		break;
	case RME96_INPUT_INTERNAL:
		snd_iprintf(buffer, "  input: internal");
		break;
	case RME96_INPUT_XLR:
		snd_iprintf(buffer, "  input: XLR");
		break;
	case RME96_INPUT_ANALOG:
		snd_iprintf(buffer, "  input: analog");
		break;
	}
	if (snd_rme96_capture_getrate(rme96, &n) < 0) {
		snd_iprintf(buffer, "\n  sample rate: no valid signal\n");
	} else {
		if (n) {
			snd_iprintf(buffer, " (8 channels)\n");
		} else {
			snd_iprintf(buffer, " (2 channels)\n");
		}
		snd_iprintf(buffer, "  sample rate: %d Hz\n",
			    snd_rme96_capture_getrate(rme96, &n));
	}
	if (rme96->wcreg & RME96_WCR_MODE24_2) {
		snd_iprintf(buffer, "  sample format: 24 bit\n");
	} else {
		snd_iprintf(buffer, "  sample format: 16 bit\n");
	}
	
	snd_iprintf(buffer, "\nOutput settings\n");
	if (rme96->wcreg & RME96_WCR_SEL) {
		snd_iprintf(buffer, "  output signal: normal playback\n");
	} else {
		snd_iprintf(buffer, "  output signal: same as input\n");
	}
	snd_iprintf(buffer, "  sample rate: %d Hz\n",
		    snd_rme96_playback_getrate(rme96));
	if (rme96->wcreg & RME96_WCR_MODE24) {
		snd_iprintf(buffer, "  sample format: 24 bit\n");
	} else {
		snd_iprintf(buffer, "  sample format: 16 bit\n");
	}
	if (rme96->areg & RME96_AR_WSEL) {
		snd_iprintf(buffer, "  clock mode: word clock\n");
	} else if (rme96->wcreg & RME96_WCR_MASTER) {
		snd_iprintf(buffer, "  clock mode: master\n");
	} else {
		snd_iprintf(buffer, "  clock mode: slave\n");
	}
	if (rme96->wcreg & RME96_WCR_PRO) {
		snd_iprintf(buffer, "  format: AES/EBU (professional)\n");
	} else {
		snd_iprintf(buffer, "  format: IEC958 (consumer)\n");
	}
	if (rme96->wcreg & RME96_WCR_EMP) {
		snd_iprintf(buffer, "  emphasis: on\n");
	} else {
		snd_iprintf(buffer, "  emphasis: off\n");
	}
	if (rme96->wcreg & RME96_WCR_DOLBY) {
		snd_iprintf(buffer, "  non-audio (dolby): on\n");
	} else {
		snd_iprintf(buffer, "  non-audio (dolby): off\n");
	}
	if (RME96_HAS_ANALOG_IN(rme96)) {
		snd_iprintf(buffer, "\nAnalog output settings\n");
		switch (snd_rme96_getmontracks(rme96)) {
		case RME96_MONITOR_TRACKS_1_2:
			snd_iprintf(buffer, "  monitored ADAT tracks: 1+2\n");
			break;
		case RME96_MONITOR_TRACKS_3_4:
			snd_iprintf(buffer, "  monitored ADAT tracks: 3+4\n");
			break;
		case RME96_MONITOR_TRACKS_5_6:
			snd_iprintf(buffer, "  monitored ADAT tracks: 5+6\n");
			break;
		case RME96_MONITOR_TRACKS_7_8:
			snd_iprintf(buffer, "  monitored ADAT tracks: 7+8\n");
			break;
		}
		switch (snd_rme96_getattenuation(rme96)) {
		case RME96_ATTENUATION_0:
			snd_iprintf(buffer, "  attenuation: 0 dB\n");
			break;
		case RME96_ATTENUATION_6:
			snd_iprintf(buffer, "  attenuation: -6 dB\n");
			break;
		case RME96_ATTENUATION_12:
			snd_iprintf(buffer, "  attenuation: -12 dB\n");
			break;
		case RME96_ATTENUATION_18:
			snd_iprintf(buffer, "  attenuation: -18 dB\n");
			break;
		}
		snd_iprintf(buffer, "  volume left: %u\n", rme96->vol[0]);
		snd_iprintf(buffer, "  volume right: %u\n", rme96->vol[1]);
	}
}

static void __init 
snd_rme96_proc_init(rme96_t *rme96)
{
	snd_info_entry_t *entry;

	if ((entry = snd_info_create_card_entry(rme96->card, "rme96", rme96->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = rme96;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_rme96_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	rme96->proc_entry = entry;
}

static void
snd_rme96_proc_done(rme96_t * rme96)
{
	if (rme96->proc_entry) {
		snd_info_unregister(rme96->proc_entry);
		rme96->proc_entry = NULL;
	}
}

/*
 * control interface
 */

static int
snd_rme96_info_loopback_control(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
static int
snd_rme96_get_loopback_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);
	ucontrol->value.integer.value[0] = rme96->wcreg & RME96_WCR_SEL ? 0 : 1;
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}
static int
snd_rme96_put_loopback_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;
	
	val = ucontrol->value.integer.value[0] ? 0 : RME96_WCR_SEL;
	spin_lock_irqsave(&rme96->lock, flags);
	val = (rme96->wcreg & ~RME96_WCR_SEL) | val;
	change = val != rme96->wcreg;
	writel(rme96->wcreg = val, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int
snd_rme96_info_inputtype_control(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
#ifdef TARGET_OS2
	static char *_texts[5] = { "Optical", "Coaxial", "Internal", "XLR", "Analog" };
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	char *texts[5] = { "Optical", "Coaxial", "Internal", "XLR", "Analog" };
#else
	static char *_texts[5] = { "Optical", "Coaxial", "Internal", "XLR", "Analog" };
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	char *texts[5] = { _texts[0], _texts[1], _texts[2], _texts[3], _texts[4] };
#endif
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	switch (rme96->pci->device) {
	case PCI_DEVICE_ID_DIGI96:
	case PCI_DEVICE_ID_DIGI96_8:
		uinfo->value.enumerated.items = 3;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PRO:
		uinfo->value.enumerated.items = 4;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST:
		if (rme96->rev > 4) {
			/* PST */
			uinfo->value.enumerated.items = 4;
			texts[3] = _texts[4]; /* Analog instead of XLR */
		} else {
			/* PAD */
			uinfo->value.enumerated.items = 5;
		}
		break;
	default:
		snd_BUG();
		break;
	}
	if (uinfo->value.enumerated.item > uinfo->value.enumerated.items - 1) {
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	}
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme96_get_inputtype_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int items = 3;
	
	spin_lock_irqsave(&rme96->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme96_getinputtype(rme96);
	
	switch (rme96->pci->device) {
	case PCI_DEVICE_ID_DIGI96:
	case PCI_DEVICE_ID_DIGI96_8:
		items = 3;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PRO:
		items = 4;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST:
		if (rme96->rev > 4) {
			/* for handling PST case, (INPUT_ANALOG is moved to INPUT_XLR */
			if (ucontrol->value.enumerated.item[0] == RME96_INPUT_ANALOG) {
				ucontrol->value.enumerated.item[0] = RME96_INPUT_XLR;
			}
			items = 4;
		} else {
			items = 5;
		}
		break;
	default:
		snd_BUG();
		break;
	}
	if (ucontrol->value.enumerated.item[0] >= items) {
		ucontrol->value.enumerated.item[0] = items - 1;
	}
	
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}
static int
snd_rme96_put_inputtype_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change, items = 3;
	
	switch (rme96->pci->device) {
	case PCI_DEVICE_ID_DIGI96:
	case PCI_DEVICE_ID_DIGI96_8:
		items = 3;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PRO:
		items = 4;
		break;
	case PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST:
		if (rme96->rev > 4) {
			items = 4;
		} else {
			items = 5;
		}
		break;
	default:
		snd_BUG();
		break;
	}
	val = ucontrol->value.enumerated.item[0] % items;
	
	/* special case for PST */
	if (rme96->pci->device == PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST && rme96->rev > 4) {
		if (val == RME96_INPUT_XLR) {
			val = RME96_INPUT_ANALOG;
		}
	}
	
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != snd_rme96_getinputtype(rme96);
	snd_rme96_setinputtype(rme96, val);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int
snd_rme96_info_clockmode_control(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[3] = { "Slave", "Master", "Wordclock" };
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2) {
		uinfo->value.enumerated.item = 2;
	}
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme96_get_clockmode_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme96_getclockmode(rme96);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}
static int
snd_rme96_put_clockmode_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;
	
	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != snd_rme96_getclockmode(rme96);
	snd_rme96_setclockmode(rme96, val);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int
snd_rme96_info_attenuation_control(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = { "0 dB", "-6 dB", "-12 dB", "-18 dB" };
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3) {
		uinfo->value.enumerated.item = 3;
	}
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme96_get_attenuation_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme96_getattenuation(rme96);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}
static int
snd_rme96_put_attenuation_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;
	
	val = ucontrol->value.enumerated.item[0] % 4;
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != snd_rme96_getattenuation(rme96);
	snd_rme96_setattenuation(rme96, val);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int
snd_rme96_info_montracks_control(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = { "1+2", "3+4", "5+6", "7+8" };
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3) {
		uinfo->value.enumerated.item = 3;
	}
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme96_get_montracks_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&rme96->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme96_getmontracks(rme96);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return 0;
}
static int
snd_rme96_put_montracks_control(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;
	
	val = ucontrol->value.enumerated.item[0] % 4;
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != snd_rme96_getmontracks(rme96);
	snd_rme96_setmontracks(rme96, val);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static u32 snd_rme96_convert_from_aes(snd_aes_iec958_t *aes)
{
	u32 val = 0;
	val |= (aes->status[0] & IEC958_AES0_PROFESSIONAL) ? RME96_WCR_PRO : 0;
	val |= (aes->status[0] & IEC958_AES0_NONAUDIO) ? RME96_WCR_DOLBY : 0;
	if (val & RME96_WCR_PRO)
		val |= (aes->status[0] & IEC958_AES0_PRO_EMPHASIS_5015) ? RME96_WCR_EMP : 0;
	else
		val |= (aes->status[0] & IEC958_AES0_CON_EMPHASIS_5015) ? RME96_WCR_EMP : 0;
	return val;
}

static void snd_rme96_convert_to_aes(snd_aes_iec958_t *aes, u32 val)
{
	aes->status[0] = ((val & RME96_WCR_PRO) ? IEC958_AES0_PROFESSIONAL : 0) |
			 ((val & RME96_WCR_DOLBY) ? IEC958_AES0_NONAUDIO : 0);
	if (val & RME96_WCR_PRO)
		aes->status[0] |= (val & RME96_WCR_EMP) ? IEC958_AES0_PRO_EMPHASIS_5015 : 0;
	else
		aes->status[0] |= (val & RME96_WCR_EMP) ? IEC958_AES0_CON_EMPHASIS_5015 : 0;
}

static int snd_rme96_control_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme96_control_spdif_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	
	snd_rme96_convert_to_aes(&ucontrol->value.iec958, rme96->wcreg_spdif);
	return 0;
}

static int snd_rme96_control_spdif_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;
	
	val = snd_rme96_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != rme96->wcreg_spdif;
	rme96->wcreg_spdif = val;
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int snd_rme96_control_spdif_stream_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme96_control_spdif_stream_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	
	snd_rme96_convert_to_aes(&ucontrol->value.iec958, rme96->wcreg_spdif_stream);
	return 0;
}

static int snd_rme96_control_spdif_stream_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;
	
	val = snd_rme96_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&rme96->lock, flags);
	change = val != rme96->wcreg_spdif_stream;
	rme96->wcreg_spdif_stream = val;
	rme96->wcreg &= ~(RME96_WCR_PRO | RME96_WCR_DOLBY | RME96_WCR_EMP);
	writel(rme96->wcreg |= val, rme96->iobase + RME96_IO_CONTROL_REGISTER);
	spin_unlock_irqrestore(&rme96->lock, flags);
	return change;
}

static int snd_rme96_control_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme96_control_spdif_mask_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = kcontrol->private_value;
	return 0;
}

static int
snd_rme96_dac_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
	uinfo->value.integer.max = RME96_185X_MAX_OUT(rme96);
        return 0;
}

static int
snd_rme96_dac_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&rme96->lock, flags);
        u->value.integer.value[0] = rme96->vol[0];
        u->value.integer.value[1] = rme96->vol[1];
	spin_unlock_irqrestore(&rme96->lock, flags);

        return 0;
}

static int
snd_rme96_dac_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	rme96_t *rme96 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int change = 0;

	if (!RME96_HAS_ANALOG_OUT(rme96)) {
		return -EINVAL;
	}
	spin_lock_irqsave(&rme96->lock, flags);
        if (u->value.integer.value[0] != rme96->vol[0]) {
		rme96->vol[0] = u->value.integer.value[0];
                change = 1;
        }
        if (u->value.integer.value[1] != rme96->vol[1]) {
		rme96->vol[1] = u->value.integer.value[1];
                change = 1;
        }
	if (change) {
		snd_rme96_apply_dac_volume(rme96);
	}
	spin_unlock_irqrestore(&rme96->lock, flags);

        return change;
}

#ifdef TARGET_OS2
static snd_kcontrol_new_t snd_rme96_controls[] = {
{
			SNDRV_CTL_ELEM_IFACE_PCM,0,0,
			SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),0,0,
			snd_rme96_control_spdif_info,
			snd_rme96_control_spdif_get,
			snd_rme96_control_spdif_put,0
},
{
			SNDRV_CTL_ELEM_IFACE_PCM,0,0,
			SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),0,
			SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
			snd_rme96_control_spdif_stream_info,
			snd_rme96_control_spdif_stream_get,
			snd_rme96_control_spdif_stream_put,0
},
{
			SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
			SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),0,
			SNDRV_CTL_ELEM_ACCESS_READ,
			snd_rme96_control_spdif_mask_info,
			snd_rme96_control_spdif_mask_get,0,
	 		IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_CON_EMPHASIS
},
{
			SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
			SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),0,
			SNDRV_CTL_ELEM_ACCESS_READ,
			snd_rme96_control_spdif_mask_info,
			snd_rme96_control_spdif_mask_get,0,
			IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_PRO_EMPHASIS
},
{
                  SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	          "Input Connector",0,0,
	          snd_rme96_info_inputtype_control, 
	          snd_rme96_get_inputtype_control,
	          snd_rme96_put_inputtype_control, 0
},
{
                  SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	           "Loopback Input",0,0,
	           snd_rme96_info_loopback_control,
	            snd_rme96_get_loopback_control,
	            snd_rme96_put_loopback_control,0
},
{
        SNDRV_CTL_ELEM_IFACE_PCM,0,0,
        "Clock Mode",0,0,
        snd_rme96_info_clockmode_control, 
        snd_rme96_get_clockmode_control,
        snd_rme96_put_clockmode_control,0
}, 
{
        SNDRV_CTL_ELEM_IFACE_PCM,0,0,
        "Monitor Tracks",0,0,
        snd_rme96_info_montracks_control, 
        snd_rme96_get_montracks_control,
        snd_rme96_put_montracks_control,0
},
{
        SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	"Attenuation",0,0,
	snd_rme96_info_attenuation_control, 
	snd_rme96_get_attenuation_control,
	snd_rme96_put_attenuation_control,0
},
{
        SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"DAC Playback Volume",0,0,
	snd_rme96_dac_volume_info,
	snd_rme96_dac_volume_get,
	snd_rme96_dac_volume_put,0
}
};
#else
static snd_kcontrol_new_t snd_rme96_controls[] = {
{
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	info:		snd_rme96_control_spdif_info,
	get:		snd_rme96_control_spdif_get,
	put:		snd_rme96_control_spdif_put
},
{
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	info:		snd_rme96_control_spdif_stream_info,
	get:		snd_rme96_control_spdif_stream_get,
	put:		snd_rme96_control_spdif_stream_put
},
{
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	info:		snd_rme96_control_spdif_mask_info,
	get:		snd_rme96_control_spdif_mask_get,
	private_value:	IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_CON_EMPHASIS
},
{
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	info:		snd_rme96_control_spdif_mask_info,
	get:		snd_rme96_control_spdif_mask_get,
	private_value:	IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_PRO_EMPHASIS
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_PCM,
	name:           "Input Connector",
	info:           snd_rme96_info_inputtype_control, 
	get:            snd_rme96_get_inputtype_control,
	put:            snd_rme96_put_inputtype_control 
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_PCM,
	name:           "Loopback Input",
	info:           snd_rme96_info_loopback_control,
	get:            snd_rme96_get_loopback_control,
	put:            snd_rme96_put_loopback_control
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_PCM,
	name:           "Clock Mode",
	info:           snd_rme96_info_clockmode_control, 
	get:            snd_rme96_get_clockmode_control,
	put:            snd_rme96_put_clockmode_control
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_PCM,
	name:           "Monitor Tracks",
	info:           snd_rme96_info_montracks_control, 
	get:            snd_rme96_get_montracks_control,
	put:            snd_rme96_put_montracks_control
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_PCM,
	name:           "Attenuation",
	info:           snd_rme96_info_attenuation_control, 
	get:            snd_rme96_get_attenuation_control,
	put:            snd_rme96_put_attenuation_control
},
{
        iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "DAC Playback Volume",
	info:           snd_rme96_dac_volume_info,
	get:            snd_rme96_dac_volume_get,
	put:            snd_rme96_dac_volume_put
}
};
#endif

static int
snd_rme96_create_switches(snd_card_t *card,
			  rme96_t *rme96)
{
	int idx, err;
	snd_kcontrol_t *kctl;

	for (idx = 0; idx < 7; idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_rme96_controls[idx], rme96))) < 0)
			return err;
		if (idx == 1)	/* IEC958 (S/PDIF) Stream */
			rme96->spdif_ctl = kctl;
	}

	if (RME96_HAS_ANALOG_OUT(rme96)) {
		for (idx = 7; idx < 10; idx++)
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_rme96_controls[idx], rme96))) < 0)
				return err;
	}
	
	return 0;
}

/*
 * Card initialisation
 */

static void snd_rme96_card_free(snd_card_t *card)
{
	snd_rme96_free(card->private_data);
}

static int __init
snd_rme96_probe(struct pci_dev *pci,
		const struct pci_device_id *id)
{
	static int dev = 0;
	rme96_t *rme96;
	snd_card_t *card;
	int err;
	u8 val;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev]) {
			dev++;
			return -ENOENT;
		}
		break;
	}
	if (dev >= SNDRV_CARDS) {
		return -ENODEV;
	}
	if ((card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
				 sizeof(rme96_t))) == NULL)
		return -ENOMEM;
	card->private_free = snd_rme96_card_free;
	rme96 = (rme96_t *)card->private_data;	
	rme96->card = card;
	rme96->pci = pci;
	if ((err = snd_rme96_create(rme96)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	strcpy(card->driver, "Digi96");
	switch (rme96->pci->device) {
	case PCI_DEVICE_ID_DIGI96:
		strcpy(card->shortname, "RME Digi96");
		break;
	case PCI_DEVICE_ID_DIGI96_8:
		strcpy(card->shortname, "RME Digi96/8");
		break;
	case PCI_DEVICE_ID_DIGI96_8_PRO:
		strcpy(card->shortname, "RME Digi96/8 PRO");
		break;
	case PCI_DEVICE_ID_DIGI96_8_PAD_OR_PST:
		pci_read_config_byte(rme96->pci, 8, &val);
		if (val < 5) {
			strcpy(card->shortname, "RME Digi96/8 PAD");
		} else {
			strcpy(card->shortname, "RME Digi96/8 PST");
		}
		break;
	}
	sprintf(card->longname, "%s at 0x%lx, irq %d", card->shortname,
		rme96->port, rme96->irq);
	
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;	
	}
	PCI_SET_DRIVER_DATA(pci, card);
	dev++;
	return 0;
}

static void __exit snd_rme96_remove(struct pci_dev *pci)
{
	snd_card_free(PCI_GET_DRIVER_DATA(pci));
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0,0, "RME Digi96",
	snd_rme96_ids,
	snd_rme96_probe,
	snd_rme96_remove,0,0
};
#else
static struct pci_driver driver = {
	name: "RME Digi96",
	id_table: snd_rme96_ids,
	probe: snd_rme96_probe,
	remove: snd_rme96_remove,
};
#endif

static int __init alsa_card_rme96_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("No RME Digi96 cards found\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_rme96_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_rme96_init)
module_exit(alsa_card_rme96_exit)

#ifndef MODULE

/* format is: snd-card-rme96=snd_enable,snd_index,snd_id */

static int __init alsa_card_rme96_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-rme96=", alsa_card_rme96_setup);

#endif /* ifndef MODULE */
