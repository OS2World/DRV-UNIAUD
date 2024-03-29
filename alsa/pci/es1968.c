/*
 *  Driver for ESS Maestro 1/2/2E Sound Card (started 21.8.99)
 *  Copyright (c) by Matze Braun <MatzeBraun@gmx.de>.
 *                   Takashi Iwai <tiwai@suse.de>
 *                  
 *  Most of the driver code comes from Zach Brown(zab@redhat.com)
 *	Alan Cox OSS Driver
 *  Rewritted from card-es1938.c source.
 *
 *  TODO:
 *   Perhaps Synth
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
 *
 *  Notes from Zach Brown about the driver code
 *
 *  Hardware Description
 *
 *	A working Maestro setup contains the Maestro chip wired to a 
 *	codec or 2.  In the Maestro we have the APUs, the ASSP, and the
 *	Wavecache.  The APUs can be though of as virtual audio routing
 *	channels.  They can take data from a number of sources and perform
 *	basic encodings of the data.  The wavecache is a storehouse for
 *	PCM data.  Typically it deals with PCI and interracts with the
 *	APUs.  The ASSP is a wacky DSP like device that ESS is loth
 *	to release docs on.  Thankfully it isn't required on the Maestro
 *	until you start doing insane things like FM emulation and surround
 *	encoding.  The codecs are almost always AC-97 compliant codecs, 
 *	but it appears that early Maestros may have had PT101 (an ESS
 *	part?) wired to them.  The only real difference in the Maestro
 *	families is external goop like docking capability, memory for
 *	the ASSP, and initialization differences.
 *
 *  Driver Operation
 *
 *	We only drive the APU/Wavecache as typical DACs and drive the
 *	mixers in the codecs.  There are 64 APUs.  We assign 6 to each
 *	/dev/dsp? device.  2 channels for output, and 4 channels for
 *	input.
 *
 *	Each APU can do a number of things, but we only really use
 *	3 basic functions.  For playback we use them to convert PCM
 *	data fetched over PCI by the wavecahche into analog data that
 *	is handed to the codec.  One APU for mono, and a pair for stereo.
 *	When in stereo, the combination of smarts in the APU and Wavecache
 *	decide which wavecache gets the left or right channel.
 *
 *	For record we still use the old overly mono system.  For each in
 *	coming channel the data comes in from the codec, through a 'input'
 *	APU, through another rate converter APU, and then into memory via
 *	the wavecache and PCI.  If its stereo, we mash it back into LRLR in
 *	software.  The pass between the 2 APUs is supposedly what requires us
 *	to have a 512 byte buffer sitting around in wavecache/memory.
 *
 *	The wavecache makes our life even more fun.  First off, it can
 *	only address the first 28 bits of PCI address space, making it
 *	useless on quite a few architectures.  Secondly, its insane.
 *	It claims to fetch from 4 regions of PCI space, each 4 meg in length.
 *	But that doesn't really work.  You can only use 1 region.  So all our
 *	allocations have to be in 4meg of each other.  Booo.  Hiss.
 *	So we have a module parameter, dsps_order, that is the order of
 *	the number of dsps to provide.  All their buffer space is allocated
 *	on open time.  The sonicvibes OSS routines we inherited really want
 *	power of 2 buffers, so we have all those next to each other, then
 *	512 byte regions for the recording wavecaches.  This ends up
 *	wasting quite a bit of memory.  The only fixes I can see would be 
 *	getting a kernel allocator that could work in zones, or figuring out
 *	just how to coerce the WP into doing what we want.
 *
 *	The indirection of the various registers means we have to spinlock
 *	nearly all register accesses.  We have the main register indirection
 *	like the wave cache, maestro registers, etc.  Then we have beasts
 *	like the APU interface that is indirect registers gotten at through
 *	the main maestro indirection.  Ouch.  We spinlock around the actual
 *	ports on a per card basis.  This means spinlock activity at each IO
 *	operation, but the only IO operation clusters are in non critical 
 *	paths and it makes the code far easier to follow.  Interrupts are
 *	blocked while holding the locks because the int handler has to
 *	get at some of them :(.  The mixer interface doesn't, however.
 *	We also have an OSS state lock that is thrown around in a few
 *	places.
 */

#define __SND_OSS_COMPAT__
#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/pcm.h>
#include <sound/mpu401.h>
#include <sound/ac97_codec.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t es1968_t

#define CARD_NAME "ESS Maestro1/2"
#define DRIVER_NAME "ES1968"

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("ESS Maestro");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ESS,Maestro 2e},"
		"{ESS,Maestro 2},"
		"{ESS,Maestro 1},"
		"{TerraTec,DMX}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 1-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef TARGET_OS2
static int snd_total_bufsize[SNDRV_CARDS] = { REPEAT_SNDRV(1024) };
static int snd_midi_enable[SNDRV_CARDS] = { REPEAT_SNDRV(0) };
static int snd_pcm_substreams_p[SNDRV_CARDS] = { REPEAT_SNDRV(4) };
static int snd_pcm_substreams_c[SNDRV_CARDS] = { REPEAT_SNDRV(1) };
#else
static int snd_total_bufsize[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1024 };
static int snd_midi_enable[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0 };
static int snd_pcm_substreams_p[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4 };
static int snd_pcm_substreams_c[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1 };
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_total_bufsize, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_total_bufsize, "Total buffer size in kB.");
MODULE_PARM_SYNTAX(snd_total_bufsize, SNDRV_ENABLED ",allows:{{1,4096}},skill:advanced");
MODULE_PARM(snd_midi_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_midi_enable, "Midi enabled for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_midi_enable, SNDRV_ENABLED "," SNDRV_ENABLE_DESC);
MODULE_PARM(snd_pcm_substreams_p, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_substreams_p, "PCM Playback substreams for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_pcm_substreams_p, SNDRV_ENABLED ",allows:{{1,8}}");
MODULE_PARM(snd_pcm_substreams_c, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_substreams_c, "PCM Capture substreams for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_pcm_substreams_c, SNDRV_ENABLED ",allows:{{0,8}}");


/* PCI Dev ID's */

#ifndef PCI_VENDOR_ID_ESS
#define PCI_VENDOR_ID_ESS	0x125D
#endif

#define PCI_VENDOR_ID_ESS_OLD	0x1285	/* Platform Tech, the people the ESS
					   was bought form */

#ifndef PCI_DEVICE_ID_ESS_M2E
#define PCI_DEVICE_ID_ESS_M2E	0x1978
#endif
#ifndef PCI_DEVICE_ID_ESS_M2
#define PCI_DEVICE_ID_ESS_M2	0x1968
#endif
#ifndef PCI_DEVICE_ID_ESS_M1
#define PCI_DEVICE_ID_ESS_M1	0x0100
#endif

#define NR_APUS			64
#define NR_APU_REGS		16

/* NEC Versas ? */
#define NEC_VERSA_SUBID1	0x80581033
#define NEC_VERSA_SUBID2	0x803c1033

/* Mode Flags */
#define ESS_FMT_STEREO     	0x01
#define ESS_FMT_16BIT      	0x02

#define DAC_RUNNING		1
#define ADC_RUNNING		2

/* Values for the ESM_LEGACY_AUDIO_CONTROL */

#define ESS_ENABLE_AUDIO	0x8000
#define ESS_ENABLE_SERIAL_IRQ	0x4000
#define IO_ADRESS_ALIAS		0x0020
#define MPU401_IRQ_ENABLE	0x0010
#define MPU401_IO_ENABLE	0x0008
#define GAME_IO_ENABLE		0x0004
#define FM_IO_ENABLE		0x0002
#define SB_IO_ENABLE		0x0001

/* Values for the ESM_CONFIG_A */

#define PIC_SNOOP1		0x4000
#define PIC_SNOOP2		0x2000
#define SAFEGUARD		0x0800
#define DMA_CLEAR		0x0700
#define DMA_DDMA		0x0000
#define DMA_TDMA		0x0100
#define DMA_PCPCI		0x0200
#define POST_WRITE		0x0080
#define ISA_TIMING		0x0040
#define SWAP_LR			0x0020
#define SUBTR_DECODE		0x0002

/* Values for the ESM_CONFIG_B */

#define SPDIF_CONFB		0x0100
#define HWV_CONFB		0x0080
#define DEBOUNCE		0x0040
#define GPIO_CONFB		0x0020
#define CHI_CONFB		0x0010
#define IDMA_CONFB		0x0008	/*undoc */
#define MIDI_FIX		0x0004	/*undoc */
#define IRQ_TO_ISA		0x0001	/*undoc */

/* Values for Ring Bus Control B */
#define	RINGB_2CODEC_ID_MASK	0x0003
#define RINGB_DIS_VALIDATION	0x0008
#define RINGB_EN_SPDIF		0x0010
#define	RINGB_EN_2CODEC		0x0020
#define RINGB_SING_BIT_DUAL	0x0040

/* ****Port Adresses**** */

/*   Write & Read */
#define ESM_INDEX		0x02
#define ESM_DATA		0x00

/*   AC97 + RingBus */
#define ESM_AC97_INDEX		0x30
#define	ESM_AC97_DATA		0x32
#define ESM_RING_BUS_DEST	0x34
#define ESM_RING_BUS_CONTR_A	0x36
#define ESM_RING_BUS_CONTR_B	0x38
#define ESM_RING_BUS_SDO	0x3A

/*   WaveCache*/
#define WC_INDEX		0x10
#define WC_DATA			0x12
#define WC_CONTROL		0x14

/*   ASSP*/
#define ASSP_INDEX		0x80
#define ASSP_MEMORY		0x82
#define ASSP_DATA		0x84
#define ASSP_CONTROL_A		0xA2
#define ASSP_CONTROL_B		0xA4
#define ASSP_CONTROL_C		0xA6
#define ASSP_HOSTW_INDEX	0xA8
#define ASSP_HOSTW_DATA		0xAA
#define ASSP_HOSTW_IRQ		0xAC
/* Midi */
#define ESM_MPU401_PORT		0x98
/* Others */
#define ESM_PORT_HOST_IRQ	0x18

#define IDR0_DATA_PORT		0x00
#define IDR1_CRAM_POINTER	0x01
#define IDR2_CRAM_DATA		0x02
#define IDR3_WAVE_DATA		0x03
#define IDR4_WAVE_PTR_LOW	0x04
#define IDR5_WAVE_PTR_HI	0x05
#define IDR6_TIMER_CTRL		0x06
#define IDR7_WAVE_ROMRAM	0x07

#define WRITEABLE_MAP		0xEFFFFF
#define READABLE_MAP		0x64003F

/* PCI Register */

#define ESM_LEGACY_AUDIO_CONTROL 0x40
#define ESM_ACPI_COMMAND	0x54
#define ESM_CONFIG_A		0x50
#define ESM_CONFIG_B		0x52
#define ESM_DDMA		0x60

/* Bob Bits */
#define ESM_BOB_ENABLE		0x0001
#define ESM_BOB_START		0x0001

/* Host IRQ Control Bits */
#define ESM_RESET_MAESTRO	0x8000
#define ESM_RESET_DIRECTSOUND   0x4000
#define ESM_HIRQ_ClkRun		0x0100
#define ESM_HIRQ_HW_VOLUME	0x0040
#define ESM_HIRQ_HARPO		0x0030	/* What's that? */
#define ESM_HIRQ_ASSP		0x0010
#define	ESM_HIRQ_DSIE		0x0004
#define ESM_HIRQ_MPU401		0x0002
#define ESM_HIRQ_SB		0x0001

/* Host IRQ Status Bits */
#define ESM_MPU401_IRQ		0x02
#define ESM_SB_IRQ		0x01
#define ESM_SOUND_IRQ		0x04
#define	ESM_ASSP_IRQ		0x10
#define ESM_HWVOL_IRQ		0x40

#define ESS_SYSCLK		50000000
#define ESM_BOB_FREQ 		200
#define ESM_BOB_FREQ_MAX	400

#define ESM_FREQ_ESM1  		(49152000L / 1024L)
#define ESM_FREQ_ESM2  		(50000000L / 1024L)
#define ESM_FREQ_ESM2E 		(50000000L / 1024L)

/* APU Modes: reg 0x00, bit 4-7 */
#define ESM_APU_MODE_SHIFT	4
#define ESM_APU_MODE_MASK	(0xf << 4)
#define	ESM_APU_OFF		0x00
#define	ESM_APU_16BITLINEAR	0x01	/* 16-Bit Linear Sample Player */
#define	ESM_APU_16BITSTEREO	0x02	/* 16-Bit Stereo Sample Player */
#define	ESM_APU_8BITLINEAR	0x03	/* 8-Bit Linear Sample Player */
#define	ESM_APU_8BITSTEREO	0x04	/* 8-Bit Stereo Sample Player */
#define	ESM_APU_8BITDIFF	0x05	/* 8-Bit Differential Sample Playrer */
#define	ESM_APU_DIGITALDELAY	0x06	/* Digital Delay Line */
#define	ESM_APU_DUALTAP		0x07	/* Dual Tap Reader */
#define	ESM_APU_CORRELATOR	0x08	/* Correlator */
#define	ESM_APU_INPUTMIXER	0x09	/* Input Mixer */
#define	ESM_APU_WAVETABLE	0x0A	/* Wave Table Mode */
#define	ESM_APU_SRCONVERTOR	0x0B	/* Sample Rate Convertor */
#define	ESM_APU_16BITPINGPONG	0x0C	/* 16-Bit Ping-Pong Sample Player */
#define	ESM_APU_RESERVED1	0x0D	/* Reserved 1 */
#define	ESM_APU_RESERVED2	0x0E	/* Reserved 2 */
#define	ESM_APU_RESERVED3	0x0F	/* Reserved 3 */

/* reg 0x00 */
#define ESM_APU_FILTER_Q_SHIFT		0
#define ESM_APU_FILTER_Q_MASK		(3 << 0)
/* APU Filtey Q Control */
#define ESM_APU_FILTER_LESSQ	0x00
#define ESM_APU_FILTER_MOREQ	0x03

#define ESM_APU_FILTER_TYPE_SHIFT	2
#define ESM_APU_FILTER_TYPE_MASK	(3 << 2)
#define ESM_APU_ENV_TYPE_SHIFT		8
#define ESM_APU_ENV_TYPE_MASK		(3 << 8)
#define ESM_APU_ENV_STATE_SHIFT		10
#define ESM_APU_ENV_STATE_MASK		(3 << 10)
#define ESM_APU_END_CURVE		(1 << 12)
#define ESM_APU_INT_ON_LOOP		(1 << 13)
#define ESM_APU_DMA_ENABLE		(1 << 14)

/* reg 0x02 */
#define ESM_APU_SUBMIX_GROUP_SHIRT	0
#define ESM_APU_SUBMIX_GROUP_MASK	(7 << 0)
#define ESM_APU_SUBMIX_MODE		(1 << 3)
#define ESM_APU_6dB			(1 << 4)
#define ESM_APU_DUAL_EFFECT		(1 << 5)
#define ESM_APU_EFFECT_CHANNELS_SHIFT	6
#define ESM_APU_EFFECT_CHANNELS_MASK	(3 << 6)

/* reg 0x03 */
#define ESM_APU_STEP_SIZE_MASK		0x0fff

/* reg 0x04 */
#define ESM_APU_PHASE_SHIFT		0
#define ESM_APU_PHASE_MASK		(0xff << 0)
#define ESM_APU_WAVE64K_PAGE_SHIFT	8	/* most 8bit of wave start offset */
#define ESM_APU_WAVE64K_PAGE_MASK	(0xff << 8)

/* reg 0x05 - wave start offset */
/* reg 0x06 - wave end offset */
/* reg 0x07 - wave loop length */

/* reg 0x08 */
#define ESM_APU_EFFECT_GAIN_SHIFT	0
#define ESM_APU_EFFECT_GAIN_MASK	(0xff << 0)
#define ESM_APU_TREMOLO_DEPTH_SHIFT	8
#define ESM_APU_TREMOLO_DEPTH_MASK	(0xf << 8)
#define ESM_APU_TREMOLO_RATE_SHIFT	12
#define ESM_APU_TREMOLO_RATE_MASK	(0xf << 12)

/* reg 0x09 */
/* bit 0-7 amplitude dest? */
#define ESM_APU_AMPLITUDE_NOW_SHIFT	8
#define ESM_APU_AMPLITUDE_NOW_MASK	(0xff << 8)

/* reg 0x0a */
#define ESM_APU_POLAR_PAN_SHIFT		0
#define ESM_APU_POLAR_PAN_MASK		(0x3f << 0)
/* Polar Pan Control */
#define	ESM_APU_PAN_CENTER_CIRCLE		0x00
#define	ESM_APU_PAN_MIDDLE_RADIUS		0x01
#define	ESM_APU_PAN_OUTSIDE_RADIUS		0x02

#define ESM_APU_FILTER_TUNING_SHIFT	8
#define ESM_APU_FILTER_TUNING_MASK	(0xff << 8)

/* reg 0x0b */
#define ESM_APU_DATA_SRC_A_SHIFT	0
#define ESM_APU_DATA_SRC_A_MASK		(0x7f << 0)
#define ESM_APU_INV_POL_A		(1 << 7)
#define ESM_APU_DATA_SRC_B_SHIFT	8
#define ESM_APU_DATA_SRC_B_MASK		(0x7f << 8)
#define ESM_APU_INV_POL_B		(1 << 15)

#define ESM_APU_VIBRATO_RATE_SHIFT	0
#define ESM_APU_VIBRATO_RATE_MASK	(0xf << 0)
#define ESM_APU_VIBRATO_DEPTH_SHIFT	4
#define ESM_APU_VIBRATO_DEPTH_MASK	(0xf << 4)
#define ESM_APU_VIBRATO_PHASE_SHIFT	8
#define ESM_APU_VIBRATO_PHASE_MASK	(0xff << 8)

/* reg 0x0c */
#define ESM_APU_RADIUS_SELECT		(1 << 6)

/* APU Filter Control */
#define	ESM_APU_FILTER_2POLE_LOPASS	0x00
#define	ESM_APU_FILTER_2POLE_BANDPASS	0x01
#define	ESM_APU_FILTER_2POLE_HIPASS	0x02
#define	ESM_APU_FILTER_1POLE_LOPASS	0x03
#define	ESM_APU_FILTER_1POLE_HIPASS	0x04
#define	ESM_APU_FILTER_OFF		0x05

/* APU ATFP Type */
#define	ESM_APU_ATFP_AMPLITUDE			0x00
#define	ESM_APU_ATFP_TREMELO			0x01
#define	ESM_APU_ATFP_FILTER			0x02
#define	ESM_APU_ATFP_PAN			0x03

/* APU ATFP Flags */
#define	ESM_APU_ATFP_FLG_OFF			0x00
#define	ESM_APU_ATFP_FLG_WAIT			0x01
#define	ESM_APU_ATFP_FLG_DONE			0x02
#define	ESM_APU_ATFP_FLG_INPROCESS		0x03


/* capture mixing buffer size */
#define ESM_MIXBUF_SIZE		512

#define ESM_MODE_PLAY		0
#define ESM_MODE_CAPTURE	1

/* acpi states */
enum {
	ACPI_D0=0,
	ACPI_D1,
	ACPI_D2,
	ACPI_D3
};

/* bits in the acpi masks */
#define ACPI_12MHZ	( 1 << 15)
#define ACPI_24MHZ	( 1 << 14)
#define ACPI_978	( 1 << 13)
#define ACPI_SPDIF	( 1 << 12)
#define ACPI_GLUE	( 1 << 11)
#define ACPI__10	( 1 << 10) /* reserved */
#define ACPI_PCIINT	( 1 << 9)
#define ACPI_HV		( 1 << 8) /* hardware volume */
#define ACPI_GPIO	( 1 << 7)
#define ACPI_ASSP	( 1 << 6)
#define ACPI_SB		( 1 << 5) /* sb emul */
#define ACPI_FM		( 1 << 4) /* fm emul */
#define ACPI_RB		( 1 << 3) /* ringbus / aclink */
#define ACPI_MIDI	( 1 << 2) 
#define ACPI_GP		( 1 << 1) /* game port */
#define ACPI_WP		( 1 << 0) /* wave processor */

#define ACPI_ALL	(0xffff)
#define ACPI_SLEEP	(~(ACPI_SPDIF|ACPI_ASSP|ACPI_SB|ACPI_FM| \
			ACPI_MIDI|ACPI_GP|ACPI_WP))
#define ACPI_NONE	(ACPI__10)

/* these masks indicate which units we care about at
	which states */
static u16 acpi_state_mask[] = {
#ifdef TARGET_OS2
	ACPI_ALL,
	ACPI_SLEEP,
	ACPI_SLEEP,
	ACPI_NONE
#else
	[ACPI_D0] = ACPI_ALL,
	[ACPI_D1] = ACPI_SLEEP,
	[ACPI_D2] = ACPI_SLEEP,
	[ACPI_D3] = ACPI_NONE
#endif
};


typedef struct snd_es1968 es1968_t;
typedef struct snd_esschan esschan_t;
typedef struct snd_esm_memory esm_memory_t;

/* APU use in the driver */
enum snd_enum_apu_type {
	ESM_APU_PCM_PLAY,
	ESM_APU_PCM_CAPTURE,
	ESM_APU_PCM_RATECONV,
	ESM_APU_FREE
};

/* DMA Hack! */
struct snd_esm_memory {
	char *buf;
	unsigned long addr;
	int size;
	int empty;	/* status */
	struct list_head list;
};

/* Playback Channel */
struct snd_esschan {
	int running;

	u8 apu[4];
	u8 apu_mode[4];

	/* playback/capture pcm buffer */
	esm_memory_t *memory;
	/* capture mixer buffer */
	esm_memory_t *mixbuf;

	unsigned int hwptr;	/* current hw pointer in bytes */
	unsigned int count;	/* sample counter in bytes */
	unsigned int dma_size;	/* total buffer size in bytes */
	unsigned int frag_size;	/* period size in bytes */
	unsigned int wav_shift;
	u16 base[4];		/* offset for ptr */

	/* stereo/16bit flag */
	unsigned char fmt;
	int mode;	/* playback / capture */

	int bob_freq;	/* required timer frequency */

	snd_pcm_substream_t *substream;

	/* linked list */
	struct list_head list;

#ifdef CONFIG_PM
	u16 wc_map[4];
#endif
};

struct snd_es1968 {
	/* Module Config */
	int midi_enabled;			/* bool */

	int total_bufsize;			/* in bytes */

	int playback_streams, capture_streams;

	/* buffer */
	void *dma_buf;
	dma_addr_t dma_buf_addr;
	unsigned long dma_buf_size;

	/* Resources... */
	int irq;
	unsigned long io_port;
	struct resource *res_io_port;
	int type;
	struct pci_dev *pci;
	snd_card_t *card;
	snd_pcm_t *pcm;

	/* DMA memory block */
	struct list_head buf_list;

	/* ALSA Stuff */
	ac97_t *ac97;
	snd_rawmidi_t *rmidi;

	spinlock_t reg_lock;

	/* Maestro Stuff */
	u16 maestro_map[32];
	atomic_t bobclient;	/* active timer instancs */
	int bob_freq;		/* timer frequency */
	spinlock_t bob_lock;
	struct semaphore memory_mutex;	/* memory lock */

	/* APU states */
	unsigned char apu[NR_APUS];

	/* active substreams */
	struct list_head substream_list;
	spinlock_t substream_lock;

#ifdef CONFIG_PM
	u16 apu_map[NR_APUS][NR_APU_REGS];
#endif
};

static void snd_es1968_interrupt(int irq, void *dev_id, struct pt_regs *regs);

#define CARD_TYPE_ESS_ES1978		0x12850100
#define CARD_TYPE_ESS_ES1968		0x125d1968
#define CARD_TYPE_ESS_ESOLDM1		0x125d1978

static struct pci_device_id snd_es1968_ids[] __devinitdata = {
	/* Maestro 1 */
        { 0x1285, 0x0100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0, },
	/* Maestro 2 */
	{ 0x125d, 0x1968, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0, },
	/* Maestro 2E */
        { 0x125d, 0x1978, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0, },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_es1968_ids);

/* *********************
   * Low Level Funcs!  *
   *********************/

/* no spinlock */
static void __maestro_write(es1968_t *chip, u16 reg, u16 data)
{
	outw(reg, chip->io_port + ESM_INDEX);
	outw(data, chip->io_port + ESM_DATA);
	chip->maestro_map[reg] = data;
}

inline static void maestro_write(es1968_t *chip, u16 reg, u16 data)
{
	unsigned long flags;
	spin_lock_irqsave(&chip->reg_lock, flags);
	__maestro_write(chip, reg, data);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/* no spinlock */
static u16 __maestro_read(es1968_t *chip, u16 reg)
{
	if (READABLE_MAP & (1 << reg)) {
		outw(reg, chip->io_port + ESM_INDEX);
		chip->maestro_map[reg] = inw(chip->io_port + ESM_DATA);
	}
	return chip->maestro_map[reg];
}

inline static u16 maestro_read(es1968_t *chip, u16 reg)
{
	unsigned long flags;
	u16 result;
	spin_lock_irqsave(&chip->reg_lock, flags);
	result = __maestro_read(chip, reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return result;
}

/* Wait for the codec bus to be free */
static int snd_es1968_ac97_wait(es1968_t *chip)
{
	int timeout = 100000;

	while (timeout-- > 0) {
		if (!(inb(chip->io_port + ESM_AC97_INDEX) & 1))
			return 0;
	}
	snd_printd("es1968: ac97 timeout\n");
	return 1; /* timeout */
}

static void snd_es1968_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short val)
{
	es1968_t *chip = snd_magic_cast(es1968_t, ac97->private_data, return);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);

	snd_es1968_ac97_wait(chip);

	/* Write the bus */
	outw(val, chip->io_port + ESM_AC97_DATA);
	mdelay(1);
	outb(reg, chip->io_port + ESM_AC97_INDEX);
	mdelay(1);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static unsigned short snd_es1968_ac97_read(ac97_t *ac97, unsigned short reg)
{
	u16 data = 0;
	es1968_t *chip = snd_magic_cast(es1968_t, ac97->private_data, return 0);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);

	snd_es1968_ac97_wait(chip);

	outb(reg | 0x80, chip->io_port + ESM_AC97_INDEX);
	mdelay(1);

	if (! snd_es1968_ac97_wait(chip)) {
		data = inw(chip->io_port + ESM_AC97_DATA);
		mdelay(1);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return data;
}

/* no spinlock */
static void apu_index_set(es1968_t *chip, u16 index)
{
	int i;
	__maestro_write(chip, IDR1_CRAM_POINTER, index);
	for (i = 0; i < 1000; i++)
		if (__maestro_read(chip, IDR1_CRAM_POINTER) == index)
			return;
	snd_printd("es1968: APU register select failed. (Timeout)\n");
}

/* no spinlock */
static void apu_data_set(es1968_t *chip, u16 data)
{
	int i;
	for (i = 0; i < 1000; i++) {
		if (__maestro_read(chip, IDR0_DATA_PORT) == data)
			return;
		__maestro_write(chip, IDR0_DATA_PORT, data);
	}
	snd_printd("es1968: APU register set probably failed (Timeout)!\n");
}

/* no spinlock */
static void __apu_set_register(es1968_t *chip, u16 channel, u8 reg, u16 data)
{
	snd_assert(channel < NR_APUS, return);
	reg |= (channel << 4);
	apu_index_set(chip, reg);
	apu_data_set(chip, data);
#ifdef CONFIG_PM
	chip->apu_map[channel][reg] = data;
#endif
}

inline static void apu_set_register(es1968_t *chip, u16 channel, u8 reg, u16 data)
{
	unsigned long flags;
	spin_lock_irqsave(&chip->reg_lock, flags);
	__apu_set_register(chip, channel, reg, data);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static u16 __apu_get_register(es1968_t *chip, u16 channel, u8 reg)
{
	snd_assert(channel < NR_APUS, return 0);
	reg |= (channel << 4);
	apu_index_set(chip, reg);
	return __maestro_read(chip, IDR0_DATA_PORT);
}

inline static u16 apu_get_register(es1968_t *chip, u16 channel, u8 reg)
{
	unsigned long flags;
	u16 v;
	spin_lock_irqsave(&chip->reg_lock, flags);
	v = __apu_get_register(chip, channel, reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return v;
}

#if 0 /* ASSP is not supported */

static void assp_set_register(es1968_t *chip, u32 reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	outl(reg, chip->io_port + ASSP_INDEX);
	outl(value, chip->io_port + ASSP_DATA);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static u32 assp_get_register(es1968_t *chip, u32 reg)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&chip->reg_lock, flags);
	outl(reg, chip->io_port + ASSP_INDEX);
	value = inl(chip->io_port + ASSP_DATA);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return value;
}

#endif

static void wave_set_register(es1968_t *chip, u16 reg, u16 value)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	outw(reg, chip->io_port + WC_INDEX);
	outw(value, chip->io_port + WC_DATA);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static u16 wave_get_register(es1968_t *chip, u16 reg)
{
	unsigned long flags;
	u16 value;

	spin_lock_irqsave(&chip->reg_lock, flags);
	outw(reg, chip->io_port + WC_INDEX);
	value = inw(chip->io_port + WC_DATA);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return value;
}

/* *******************
   * Bob the Timer!  *
   *******************/

static void snd_es1968_bob_stop(es1968_t *chip)
{
	u16 reg;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	reg = __maestro_read(chip, 0x11);
	reg &= ~ESM_BOB_ENABLE;
	__maestro_write(chip, 0x11, reg);
	reg = __maestro_read(chip, 0x17);
	reg &= ~ESM_BOB_START;
	__maestro_write(chip, 0x17, reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_es1968_bob_start(es1968_t *chip)
{
	int prescale;
	int divide;
	unsigned long flags;

	/* compute ideal interrupt frequency for buffer size & play rate */
	/* first, find best prescaler value to match freq */
	for (prescale = 5; prescale < 12; prescale++)
		if (chip->bob_freq > (ESS_SYSCLK >> (prescale + 9)))
			break;

	/* next, back off prescaler whilst getting divider into optimum range */
	divide = 1;
	while ((prescale > 5) && (divide < 32)) {
		prescale--;
		divide <<= 1;
	}
	divide >>= 1;

	/* now fine-tune the divider for best match */
	for (; divide < 31; divide++)
		if (chip->bob_freq >=
		    ((ESS_SYSCLK >> (prescale + 9)) / (divide + 1))) break;

	/* divide = 0 is illegal, but don't let prescale = 4! */
	if (divide == 0) {
		divide++;
		if (prescale > 5)
			prescale--;
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	__maestro_write(chip, 6, 0x9000 | (prescale << 5) | divide);	/* set reg */

	/* Now set IDR 11/17 */
	__maestro_write(chip, 0x11, __maestro_read(chip, 0x11) | 1);
	__maestro_write(chip, 0x17, __maestro_read(chip, 0x17) | 1);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_es1968_bob_inc(es1968_t *chip, int freq)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->bob_lock, flags);
	atomic_inc(&chip->bobclient);
	if (atomic_read(&chip->bobclient) == 1) {
		chip->bob_freq = freq;
		snd_es1968_bob_start(chip);
	} else if (chip->bob_freq < freq) {
		snd_es1968_bob_stop(chip);
		chip->bob_freq = freq;
		snd_es1968_bob_start(chip);
	}
	spin_unlock_irqrestore(&chip->bob_lock, flags);
}

static void snd_es1968_bob_dec(es1968_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->bob_lock, flags);
	atomic_dec(&chip->bobclient);
	if (atomic_read(&chip->bobclient) <= 0)
		snd_es1968_bob_stop(chip);
	else if (chip->bob_freq > ESM_BOB_FREQ) {
		/* check reduction of timer frequency */
		struct list_head *p;
		int max_freq = ESM_BOB_FREQ;
		spin_lock(&chip->substream_lock);
		list_for_each(p, &chip->substream_list) {
			esschan_t *es = list_entry(p, esschan_t, list);
			if (max_freq < es->bob_freq)
				max_freq = es->bob_freq;
		}
		spin_unlock(&chip->substream_lock);
		if (max_freq != chip->bob_freq) {
			snd_es1968_bob_stop(chip);
			chip->bob_freq = max_freq;
			snd_es1968_bob_start(chip);
		}
	}
	spin_unlock_irqrestore(&chip->bob_lock, flags);
}

static int
snd_es1968_calc_bob_rate(es1968_t *chip, esschan_t *es,
			 snd_pcm_runtime_t *runtime)
{
	/* we acquire 4 interrupts per period for precise control.. */
	int freq = runtime->rate * 4;
	if (es->fmt & ESS_FMT_STEREO)
		freq <<= 1;
	if (es->fmt & ESS_FMT_16BIT)
		freq <<= 1;
	freq /= es->frag_size;
	if (freq < ESM_BOB_FREQ)
		freq = ESM_BOB_FREQ;
	else if (freq > ESM_BOB_FREQ_MAX)
		freq = ESM_BOB_FREQ_MAX;
	return freq;
}


/*************
 *  PCM Part *
 *************/

static u32 snd_es1968_compute_rate(es1968_t * esm, u32 freq)
{
	u32 clock;
	switch (esm->type) {
	case CARD_TYPE_ESS_ES1978:
		clock = ESM_FREQ_ESM2E;
		break;
	case CARD_TYPE_ESS_ES1968:
		clock = ESM_FREQ_ESM2;
		break;
	case CARD_TYPE_ESS_ESOLDM1:
		clock = ESM_FREQ_ESM1;
		break;
	default:
		clock = ESM_FREQ_ESM2E;
		snd_printd("es1968: wrong type in compute_rate (please report!)\n");
	}

	if (freq == 48000)
		return 0x10000;

	return ((freq / clock) << 16) + (((freq % clock) << 16) / clock);
}

/* get current pointer */
inline static unsigned int
snd_es1968_get_dma_ptr(es1968_t *chip, esschan_t *es)
{
	unsigned int offset;

	offset = apu_get_register(chip, es->apu[0], 5);

	offset -= es->base[0];

	return (offset & 0xFFFE);	/* hardware is in words */
}

static void snd_es1968_apu_set_freq(es1968_t *chip, int apu, int freq)
{
	apu_set_register(chip, apu, 2,
			   (apu_get_register(chip, apu, 2) & 0x00FF) |
			   ((freq & 0xff) << 8) | 0x10);
	apu_set_register(chip, apu, 3, freq >> 8);
}

inline static void snd_es1968_trigger_apu(es1968_t *esm, int apu, int mode)
{
	/* dma on, no envelopes, filter to all 1s) */
	apu_set_register(esm, apu, 0, 0x400f | mode);
}

static void snd_es1968_pcm_start(es1968_t *chip, esschan_t *es)
{
	if (es->running)
		return;

	apu_set_register(chip, es->apu[0], 5, es->base[0]);
	snd_es1968_trigger_apu(chip, es->apu[0], es->apu_mode[0]);
	if (es->mode == ESM_MODE_CAPTURE) {
		apu_set_register(chip, es->apu[2], 5, es->base[2]);
		snd_es1968_trigger_apu(chip, es->apu[2], es->apu_mode[2]);
	}
	if (es->fmt & ESS_FMT_STEREO) {
		apu_set_register(chip, es->apu[1], 5, es->base[1]);
		snd_es1968_trigger_apu(chip, es->apu[1], es->apu_mode[1]);
		if (es->mode == ESM_MODE_CAPTURE) {
			apu_set_register(chip, es->apu[3], 5, es->base[3]);
			snd_es1968_trigger_apu(chip, es->apu[3], es->apu_mode[3]);
		}
	}
	es->running = 1;
}

static void snd_es1968_pcm_stop(es1968_t *chip, esschan_t *es)
{
	if (! es->running)
		return;

	snd_es1968_trigger_apu(chip, es->apu[0], 0);
	snd_es1968_trigger_apu(chip, es->apu[1], 0);
	if (es->mode == ESM_MODE_CAPTURE) {
		snd_es1968_trigger_apu(chip, es->apu[2], 0);
		snd_es1968_trigger_apu(chip, es->apu[3], 0);
	}
	es->running = 0;
}

/* set the wavecache control reg */
static void snd_es1968_program_wavecache(es1968_t *chip, esschan_t *es,
					 int channel, u32 addr)
{
	u32 tmpval = (addr - 0x10) & 0xFFF8;

	if (!(es->fmt & ESS_FMT_16BIT))
		tmpval |= 4;	/* 8bit */
	if (es->fmt & ESS_FMT_STEREO)
		tmpval |= 2;	/* stereo */

	/* set the wavecache control reg */
	wave_set_register(chip, es->apu[channel] << 3, tmpval);

#ifdef CONFIG_PM
	es->wc_map[channel] = tmpval;
#endif
}


static void snd_es1968_playback_setup(es1968_t *chip, esschan_t *es,
				      snd_pcm_runtime_t *runtime)
{
	u32 pa;
	int high_apu = 0;
	int channel, apu;
	int i, size;
	unsigned long flags;
	u32 freq;

	size = es->dma_size >> es->wav_shift;

	if (es->fmt & ESS_FMT_STEREO)
		high_apu++;

	for (channel = 0; channel <= high_apu; channel++) {
		apu = es->apu[channel];

		snd_es1968_program_wavecache(chip, es, channel, es->memory->addr);

		/* Offset to PCMBAR */
		pa = es->memory->addr;
		pa -= chip->dma_buf_addr;
		pa >>= 1;	/* words */

		/* base offset of dma calcs when reading the pointer
		   on this left one */
		es->base[channel] = pa & 0xFFFF;

		pa |= 0x00400000;	/* System RAM (Bit 22) */

		if (es->fmt & ESS_FMT_STEREO) {
			/* Enable stereo */
			if (channel)
				pa |= 0x00800000;	/* (Bit 23) */
			if (es->fmt & ESS_FMT_16BIT)
				pa >>= 1;
		}

		for (i = 0; i < 16; i++)
			apu_set_register(chip, apu, i, 0x0000);

		/* Load the buffer into the wave engine */
		apu_set_register(chip, apu, 4, ((pa >> 16) & 0xFF) << 8);
		apu_set_register(chip, apu, 5, pa & 0xFFFF);
		apu_set_register(chip, apu, 6, (pa + size) & 0xFFFF);
		/* setting loop == sample len */
		apu_set_register(chip, apu, 7, size);

		/* clear effects/env.. */
		apu_set_register(chip, apu, 8, 0x0000);
		/* set amp now to 0xd0 (?), low byte is 'amplitude dest'? */
		apu_set_register(chip, apu, 9, 0xD000);

		/* clear routing stuff */
		apu_set_register(chip, apu, 11, 0x0000);
		/* dma on, no envelopes, filter to all 1s) */
		// apu_set_register(chip, apu, 0, 0x400F);

		if (es->fmt & ESS_FMT_16BIT)
			es->apu_mode[channel] = 0x10;	/* 16bit mono */
		else
			es->apu_mode[channel] = 0x30;	/* 8bit mono */

		if (es->fmt & ESS_FMT_STEREO) {
			/* set panning: left or right */
			/* Check: different panning. On my Canyon 3D Chipset the
			   Channels are swapped. I don't know, about the output
			   to the SPDif Link. Perhaps you have to change this
			   and not the APU Regs 4-5. */
			apu_set_register(chip, apu, 10,
					 0x8F00 | (channel ? 0 : 0x10));
			es->apu_mode[channel] += 0x10;	/* stereo */
		} else
			apu_set_register(chip, apu, 10, 0x8F08);
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	/* clear WP interupts */
	outw(1, chip->io_port + 0x04);
	/* enable WP ints */
	outw(inw(chip->io_port + 0x18) | 4, chip->io_port + 0x18);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	freq = runtime->rate;
	/* set frequency */
	if (freq > 48000)
		freq = 48000;
	if (freq < 4000)
		freq = 4000;

	/* hmmm.. */
	if (!(es->fmt & ESS_FMT_16BIT) && !(es->fmt & ESS_FMT_STEREO))
		freq >>= 1;

	freq = snd_es1968_compute_rate(chip, freq);

	/* Load the frequency, turn on 6dB */
	snd_es1968_apu_set_freq(chip, es->apu[0], freq);
	snd_es1968_apu_set_freq(chip, es->apu[1], freq);
}

static void snd_es1968_capture_setup(es1968_t *chip, esschan_t *es,
				     snd_pcm_runtime_t *runtime)
{
	int apu_step = 2;
	int channel, apu;
	int i, size;
	u32 freq;
	unsigned long flags;

	size = es->dma_size >> es->wav_shift;

	/* we're given the full size of the buffer, but
	   in stereo each channel will only use its half */
	if (es->fmt & ESS_FMT_STEREO)
		apu_step = 1;

	/* APU assignments:
	   0 = mono/left SRC
	   1 = right SRC
	   2 = mono/left Input Mixer
	   3 = right Input Mixer */
	for (channel = 0; channel < 4; channel += apu_step) {
		int bsize, route;
		u32 pa;

		apu = es->apu[channel];

		/* data seems to flow from the codec, through an apu into
		   the 'mixbuf' bit of page, then through the SRC apu
		   and out to the real 'buffer'.  ok.  sure.  */

		if (channel & 2) {
			/* ok, we're an input mixer going from adc
			   through the mixbuf to the other apus */

			if (!(channel & 0x01)) {
				pa = es->mixbuf->addr;
			} else {
				pa = es->mixbuf->addr + ESM_MIXBUF_SIZE / 2;
			}

			/* we source from a 'magic' apu */
			bsize = ESM_MIXBUF_SIZE / 4; /* half of this channels alloc, in words */
			/* parallel in crap, see maestro reg 0xC [8-11] */
			route = 0x14 + channel - 2;
			es->apu_mode[channel] = 0x90;	/* Input Mixer */
		} else {
			/* we're a rate converter taking
			   input from the input apus and outputing it to
			   system memory */
			if (!(channel & 0x01))
				pa = es->memory->addr;
			else
				pa = es->memory->addr + size * 2; /* size is in word */

			es->apu_mode[channel] = 0xB0;	/* Sample Rate Converter */

			bsize = size;
			/* get input from inputing apu */
			route = es->apu[channel + 2];
		}

		/* set the wavecache control reg */
		snd_es1968_program_wavecache(chip, es, channel, pa);

		/* Offset to PCMBAR */
		pa -= chip->dma_buf_addr;
		pa >>= 1;	/* words */

		/* base offset of dma calcs when reading the pointer
		   on this left one */
		es->base[channel] = pa & 0xFFFF;

		pa |= 0x00400000;	/* bit 22 -> System RAM */
                
                /* Begin loading the APU */
		for (i = 0; i < 16; i++)
			apu_set_register(chip, apu, i, 0x0000);

		/* need to enable subgroups.. and we should probably
		   have different groups for different /dev/dsps..  */
		apu_set_register(chip, apu, 2, 0x8);

		/* Load the buffer into the wave engine */
		apu_set_register(chip, apu, 4, ((pa >> 16) & 0xFF) << 8);
		/* XXX reg is little endian.. */
		apu_set_register(chip, apu, 5, pa & 0xFFFF);
		apu_set_register(chip, apu, 6, (pa + bsize) & 0xFFFF);
		apu_set_register(chip, apu, 7, bsize);
#if 1
		if (es->fmt & ESS_FMT_STEREO) /* ??? really ??? */
			apu_set_register(chip, es->apu[channel], 7,
					 bsize - 1);
#endif

		/* clear effects/env.. */
		apu_set_register(chip, apu, 8, 0x00F0);
		/* amplitude now?  sure.  why not.  */
		apu_set_register(chip, apu, 9, 0x0000);
		/* set filter tune, radius, polar pan */
		apu_set_register(chip, apu, 10, 0x8F08);
		/* route input */
		apu_set_register(chip, apu, 11, route);
		/* dma on, no envelopes, filter to all 1s) */
		// apu_set_register(chip, apu, 0, 0x400F);
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	/* clear WP interupts */
	outw(1, chip->io_port + 0x04);
	/* enable WP ints */
	outw(inw(chip->io_port + 0x18) | 4, chip->io_port + 0x18);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	freq = runtime->rate;
	/* Sample Rate conversion APUs don't like 0x10000 for their rate */
	if (freq > 47999)
		freq = 47999;
	if (freq < 4000)
		freq = 4000;

	freq = snd_es1968_compute_rate(chip, freq);

	/* Load the frequency, turn on 6dB */
	snd_es1968_apu_set_freq(chip, es->apu[0], freq);
	snd_es1968_apu_set_freq(chip, es->apu[1], freq);

	/* fix mixer rate at 48khz.  and its _must_ be 0x10000. */
	freq = 0x10000;
	snd_es1968_apu_set_freq(chip, es->apu[2], freq);
	snd_es1968_apu_set_freq(chip, es->apu[3], freq);
}

/*******************
 *  ALSA Interface *
 *******************/

static int snd_es1968_pcm_prepare(snd_pcm_substream_t *substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	esschan_t *es = snd_magic_cast(esschan_t, runtime->private_data, return -ENXIO);

	es->dma_size = snd_pcm_lib_buffer_bytes(substream);
	es->frag_size = snd_pcm_lib_period_bytes(substream);

	es->wav_shift = 1; /* maestro handles always 16bit */
	es->fmt = 0;
	if (snd_pcm_format_width(runtime->format) == 16)
		es->fmt |= ESS_FMT_16BIT;
	if (runtime->channels > 1) {
		es->fmt |= ESS_FMT_STEREO;
		if (es->fmt & ESS_FMT_16BIT) /* 8bit is already word shifted */
			es->wav_shift++;
	}
	es->bob_freq = snd_es1968_calc_bob_rate(chip, es, runtime);

	switch (es->mode) {
	case ESM_MODE_PLAY:
		snd_es1968_playback_setup(chip, es, runtime);
		break;
	case ESM_MODE_CAPTURE:
		snd_es1968_capture_setup(chip, es, runtime);
		break;
	}

	return 0;
}

static int snd_es1968_pcm_trigger(snd_pcm_substream_t *substream, int up)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	esschan_t *es = snd_magic_cast(esschan_t, substream->runtime->private_data, return -ENXIO);
	unsigned long flags;

	if (up) {
		if (es->running)
			return 0;
		snd_es1968_bob_inc(chip, es->bob_freq);
		es->count = 0;
		es->hwptr = 0;
		snd_es1968_pcm_start(chip, es);
		spin_lock_irqsave(&chip->substream_lock, flags);
		list_add(&es->list, &chip->substream_list);
		spin_unlock_irqrestore(&chip->substream_lock, flags);
	} else {
		if (! es->running)
			return 0;
		snd_es1968_pcm_stop(chip, es);
		spin_lock_irqsave(&chip->substream_lock, flags);
		list_del(&es->list);
		spin_unlock_irqrestore(&chip->substream_lock, flags);
		snd_es1968_bob_dec(chip);
	}
	return 0;
}

static snd_pcm_uframes_t snd_es1968_pcm_pointer(snd_pcm_substream_t *substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	esschan_t *es = snd_magic_cast(esschan_t, substream->runtime->private_data, return -ENXIO);
	unsigned int ptr;

	ptr = snd_es1968_get_dma_ptr(chip, es) << es->wav_shift;
	
	return bytes_to_frames(substream->runtime, ptr % es->dma_size);
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_es1968_playback = {
/*	info:		  */	(SNDRV_PCM_INFO_MMAP |
               		         SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	65536,
/*	period_bytes_min: */	256,
/*	period_bytes_max: */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_es1968_capture = {
/*	info:		  */	(SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
/*	formats:	  */	/*SNDRV_PCM_FMTBIT_U8 |*/ SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	65536,
/*	period_bytes_min: */	256,
/*	period_bytes_max: */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_es1968_playback = {
	info:			(SNDRV_PCM_INFO_MMAP |
               		         SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	65536,
	period_bytes_min:	256,
	period_bytes_max:	65536,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_es1968_capture = {
	info:			(SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	formats:		/*SNDRV_PCM_FMTBIT_U8 |*/ SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	65536,
	period_bytes_min:	256,
	period_bytes_max:	65536,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

/* *************************
   * DMA memory management *
   *************************/

/* Because the Maestro can only take adresses relative to the PCM base adress
   register :( */

static int calc_available_memory_size(es1968_t *chip)
{
	struct list_head *p;
	int max_size = 0;
	
	down(&chip->memory_mutex);
	list_for_each(p, &chip->buf_list) {
		esm_memory_t *buf = list_entry(p, esm_memory_t, list);
		if (buf->empty && buf->size > max_size)
			max_size = buf->size;
	}
	up(&chip->memory_mutex);
	return max_size;
}

/* allocate a new memory chunk with the specified size */
static esm_memory_t *snd_es1968_new_memory(es1968_t *chip, int size)
{
	esm_memory_t *buf;
	struct list_head *p;
	
	down(&chip->memory_mutex);
	list_for_each(p, &chip->buf_list) {
		buf = list_entry(p, esm_memory_t, list);
		if (buf->empty && buf->size >= size)
			goto __found;
	}
	up(&chip->memory_mutex);
	return NULL;

__found:
	if (buf->size > size) {
		esm_memory_t *chunk = kmalloc(sizeof(*chunk), GFP_KERNEL);
		if (chunk == NULL)
			return NULL;
		chunk->size = buf->size - size;
		chunk->buf = buf->buf + size;
		chunk->addr = buf->addr + size;
		chunk->empty = 1;
		buf->size = size;
		list_add(&chunk->list, &buf->list);
	}
	buf->empty = 0;
	up(&chip->memory_mutex);
	return buf;
}

/* free a memory chunk */
static void snd_es1968_free_memory(es1968_t *chip, esm_memory_t *buf)
{
	esm_memory_t *chunk;

	down(&chip->memory_mutex);
	buf->empty = 1;
	if (buf->list.prev != &chip->buf_list) {
		chunk = list_entry(buf->list.prev, esm_memory_t, list);
		if (chunk->empty) {
			chunk->size += buf->size;
			list_del(&buf->list);
			kfree(buf);
			buf = chunk;
		}
	}
	if (buf->list.next != &chip->buf_list) {
		chunk = list_entry(buf->list.next, esm_memory_t, list);
		if (chunk->empty) {
			buf->size += chunk->size;
			list_del(&chunk->list);
			kfree(chunk);
		}
	}
	up(&chip->memory_mutex);
}

static void snd_es1968_free_dmabuf(es1968_t *chip)
{
	struct list_head *p;

	if (! chip->dma_buf)
		return;
	snd_free_pci_pages(chip->pci, chip->dma_buf_size, chip->dma_buf, chip->dma_buf_addr);
	while ((p = chip->buf_list.next) != &chip->buf_list) {
		esm_memory_t *chunk = list_entry(p, esm_memory_t, list);
		list_del(p);
		kfree(chunk);
	}
}

static int __init
snd_es1968_init_dmabuf(es1968_t *chip)
{
	esm_memory_t *chunk;
	chip->dma_buf = snd_malloc_pci_pages_fallback(chip->pci, chip->total_bufsize,
						      &chip->dma_buf_addr, &chip->dma_buf_size);
	//snd_printd("es1968: allocated buffer size %ld\n", chip->dma_buf_size);
	if (chip->dma_buf == NULL) {
		snd_printk("es1968: can't allocate dma pages for size %d\n",
			   chip->total_bufsize);
		return -ENOMEM;
	}
	if ((chip->dma_buf_addr + chip->dma_buf_size - 1) & ~((1 << 28) - 1)) {
		snd_es1968_free_dmabuf(chip);
		snd_printk("es1968: DMA buffer beyond 256MB.\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&chip->buf_list);
	/* allocate an empty chunk */
	chunk = kmalloc(sizeof(*chunk), GFP_KERNEL);
	if (chunk == NULL) {
		snd_es1968_free_dmabuf(chip);
		return -ENOMEM;
	}
	memset(chip->dma_buf, 0, 512);
#ifdef TARGET_OS2
	chunk->buf = (char *)chip->dma_buf + 512;
#else
	chunk->buf = chip->dma_buf + 512;
#endif
	chunk->addr = chip->dma_buf_addr + 512;
	chunk->size = chip->dma_buf_size - 512;
	chunk->empty = 1;
	list_add(&chunk->list, &chip->buf_list);

	return 0;
}

/* setup the dma_areas */
/* buffer is extracted from the pre-allocated memory chunk */
static int snd_es1968_hw_params(snd_pcm_substream_t *substream,
				snd_pcm_hw_params_t *hw_params)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	esschan_t *chan = snd_magic_cast(esschan_t, runtime->private_data, return -ENXIO);
	int size = params_buffer_bytes(hw_params);

	if (chan->memory) {
		if (chan->memory->size >= size) {
			runtime->dma_bytes = size;
			return 0;
		}
		snd_es1968_free_memory(chip, chan->memory);
	}
	chan->memory = snd_es1968_new_memory(chip, size);
	if (chan->memory == NULL) {
		// snd_printd("cannot allocate dma buffer: size = %d\n", size);
		return -ENOMEM;
	}
	runtime->dma_bytes = size;
	runtime->dma_area = chan->memory->buf;
	runtime->dma_addr = chan->memory->addr;
	return 1; /* area was changed */
}

/* remove dma areas if allocated */
static int snd_es1968_hw_free(snd_pcm_substream_t * substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	esschan_t *chan = snd_magic_cast(esschan_t, runtime->private_data, return -ENXIO);

	if (chan->memory) {
		snd_es1968_free_memory(chip, chan->memory);
		chan->memory = NULL;
	}
	return 0;
}


/*
 * allocate APU pair
 */
static int snd_es1968_alloc_apu_pair(es1968_t *chip, int type)
{
	int apu;

	for (apu = 0; apu < NR_APUS; apu += 2) {
		if (chip->apu[apu] == ESM_APU_FREE &&
		    chip->apu[apu + 1] == ESM_APU_FREE) {
			chip->apu[apu] = chip->apu[apu + 1] = type;
			return apu;
		}
	}
	return -EBUSY;
}

/*
 * release APU pair
 */
static void snd_es1968_free_apu_pair(es1968_t *chip, int apu)
{
	chip->apu[apu] = chip->apu[apu + 1] = ESM_APU_FREE;
}


/******************
 * PCM open/close *
 ******************/

static int snd_es1968_playback_open(snd_pcm_substream_t *substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	esschan_t *es;
	int apu1;

	/* search 2 APUs */
	apu1 = snd_es1968_alloc_apu_pair(chip, ESM_APU_PCM_PLAY);
	if (apu1 < 0)
		return apu1;

	es = snd_magic_kcalloc(esschan_t, 0, GFP_KERNEL);
	if (!es) {
		snd_es1968_free_apu_pair(chip, apu1);
		return -ENOMEM;
	}

	es->apu[0] = apu1;
	es->apu[1] = apu1 + 1;
	es->apu_mode[0] = 0;
	es->apu_mode[1] = 0;
	es->running = 0;
	es->substream = substream;
	es->mode = ESM_MODE_PLAY;

	runtime->private_data = es;
	runtime->hw = snd_es1968_playback;
	runtime->hw.buffer_bytes_max = runtime->hw.period_bytes_max =
		calc_available_memory_size(chip);

	return 0;
}

static int snd_es1968_capture_copy(snd_pcm_substream_t *substream,
				   int channel, snd_pcm_uframes_t pos,
				   void *buf, snd_pcm_uframes_t count)
{
	//es1968_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	esschan_t *es = snd_magic_cast(esschan_t, runtime->private_data, return -ENXIO);
	char *src = runtime->dma_area;

	if (runtime->channels == 1)
		return copy_to_user(buf, src + pos, count) ? -EFAULT : 0;
	else {
		count /= 2;
		pos /= 2;
		if (copy_to_user(buf, src + pos, count))
			return -EFAULT;
#ifdef TARGET_OS2
		if (copy_to_user((char *)buf + count, src + pos + es->dma_size/2, count))
#else
		if (copy_to_user(buf + count, src + pos + es->dma_size/2, count))
#endif
			return -EFAULT;
		return 0;
	}
}

static int snd_es1968_capture_open(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	es1968_t *chip = snd_pcm_substream_chip(substream);
	esschan_t *es;
	int apu1, apu2;

	apu1 = snd_es1968_alloc_apu_pair(chip, ESM_APU_PCM_CAPTURE);
	if (apu1 < 0)
		return apu1;
	apu2 = snd_es1968_alloc_apu_pair(chip, ESM_APU_PCM_RATECONV);
	if (apu2 < 0) {
		snd_es1968_free_apu_pair(chip, apu1);
		return apu2;
	}
	
	es = snd_magic_kcalloc(esschan_t, 0, GFP_KERNEL);
	if (!es) {
		snd_es1968_free_apu_pair(chip, apu1);
		snd_es1968_free_apu_pair(chip, apu2);
		return -ENOMEM;
	}

	es->apu[0] = apu1;
	es->apu[1] = apu1 + 1;
	es->apu[2] = apu2;
	es->apu[3] = apu2 + 1;
	es->apu_mode[0] = 0;
	es->apu_mode[1] = 0;
	es->apu_mode[2] = 0;
	es->apu_mode[3] = 0;
	es->running = 0;
	es->substream = substream;
	es->mode = ESM_MODE_CAPTURE;

	/* get mixbuffer */
	if ((es->mixbuf = snd_es1968_new_memory(chip, ESM_MIXBUF_SIZE)) == NULL) {
		snd_es1968_free_apu_pair(chip, apu1);
		snd_es1968_free_apu_pair(chip, apu2);
		snd_magic_kfree(es);
                return -ENOMEM;
        }

	runtime->private_data = es;
	runtime->hw = snd_es1968_capture;
	runtime->hw.buffer_bytes_max = runtime->hw.period_bytes_max =
		calc_available_memory_size(chip) - 1024;

	return 0;
}

static int snd_es1968_playback_close(snd_pcm_substream_t * substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	esschan_t *es;
	if (substream->runtime->private_data == NULL)
		return 0;
	es = snd_magic_cast(esschan_t, substream->runtime->private_data, return -ENXIO);
	snd_es1968_free_apu_pair(chip, es->apu[0]);
	snd_magic_kfree(es);

	return 0;
}

static int snd_es1968_capture_close(snd_pcm_substream_t * substream)
{
	es1968_t *chip = snd_pcm_substream_chip(substream);
	esschan_t *es;
	if (substream->runtime->private_data == NULL)
		return 0;
	es = snd_magic_cast(esschan_t, substream->runtime->private_data, return -ENXIO);
	snd_es1968_free_memory(chip, es->mixbuf);
	snd_es1968_free_apu_pair(chip, es->apu[0]);
	snd_es1968_free_apu_pair(chip, es->apu[2]);
	snd_magic_kfree(es);

	return 0;
}
#ifdef TARGET_OS2
static snd_pcm_ops_t snd_es1968_playback_ops = {
/*	open:	  */	snd_es1968_playback_open,
/*	close:	  */	snd_es1968_playback_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_es1968_hw_params,
/*	hw_free:  */	snd_es1968_hw_free,
/*	prepare:  */	snd_es1968_pcm_prepare,
/*	trigger:  */	snd_es1968_pcm_trigger,
/*	pointer:  */	snd_es1968_pcm_pointer,
        0,0
};

static snd_pcm_ops_t snd_es1968_capture_ops = {
/*	open:	  */	snd_es1968_capture_open,
/*	close:	  */	snd_es1968_capture_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_es1968_hw_params,
/*	hw_free:  */	snd_es1968_hw_free,
/*	prepare:  */	snd_es1968_pcm_prepare,
/*	trigger:  */	snd_es1968_pcm_trigger,
/*	pointer:  */	snd_es1968_pcm_pointer,
/*	copy:	  */	snd_es1968_capture_copy,
        0
};
#else
static snd_pcm_ops_t snd_es1968_playback_ops = {
	open:		snd_es1968_playback_open,
	close:		snd_es1968_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_es1968_hw_params,
	hw_free:	snd_es1968_hw_free,
	prepare:	snd_es1968_pcm_prepare,
	trigger:	snd_es1968_pcm_trigger,
	pointer:	snd_es1968_pcm_pointer,
};

static snd_pcm_ops_t snd_es1968_capture_ops = {
	open:		snd_es1968_capture_open,
	close:		snd_es1968_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_es1968_hw_params,
	hw_free:	snd_es1968_hw_free,
	prepare:	snd_es1968_pcm_prepare,
	trigger:	snd_es1968_pcm_trigger,
	pointer:	snd_es1968_pcm_pointer,
	copy:		snd_es1968_capture_copy,
};
#endif

static void snd_es1968_pcm_free(snd_pcm_t *pcm)
{
	es1968_t *esm = snd_magic_cast(es1968_t, pcm->private_data, return);
	snd_es1968_free_dmabuf(esm);
	esm->pcm = NULL;
}

static int __init
snd_es1968_pcm(es1968_t *chip, int device)
{
	snd_pcm_t *pcm;
	int err;

	/* get DMA buffer */
	if ((err = snd_es1968_init_dmabuf(chip)) < 0)
		return err;

	/* set PCMBAR */
	wave_set_register(chip, 0x01FC, chip->dma_buf_addr >> 12);
	wave_set_register(chip, 0x01FD, chip->dma_buf_addr >> 12);
	wave_set_register(chip, 0x01FE, chip->dma_buf_addr >> 12);
	wave_set_register(chip, 0x01FF, chip->dma_buf_addr >> 12);

	if ((err = snd_pcm_new(chip->card, "ESS Maestro", device,
			       chip->playback_streams,
			       chip->capture_streams, &pcm)) < 0)
		return err;

	pcm->private_data = chip;
	pcm->private_free = snd_es1968_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_es1968_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_es1968_capture_ops);

	pcm->info_flags = 0;

	strcpy(pcm->name, "ESS Maestro");

	chip->pcm = pcm;

	return 0;
}

/*
 * update pointer
 */
static void snd_es1968_update_pcm(es1968_t *chip, esschan_t *es)
{
	unsigned int hwptr;
	unsigned int diff;
	snd_pcm_substream_t *subs = es->substream;
        
	if (subs == NULL || !es->running)
		return;

	hwptr = snd_es1968_get_dma_ptr(chip, es) << es->wav_shift;
	hwptr %= es->dma_size;

	diff = (es->dma_size + hwptr - es->hwptr) % es->dma_size;

	es->hwptr = hwptr;
	es->count += diff;

	if (es->count > es->frag_size) {
		spin_unlock(&chip->substream_lock);
		snd_pcm_period_elapsed(subs);
		spin_lock(&chip->substream_lock);
		es->count = 0;
	}
}

/*
 * interrupt handler
 */
static void snd_es1968_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	es1968_t *chip = snd_magic_cast(es1968_t, dev_id, return);
	u32 event;

	if (!(event = inb(chip->io_port + 0x1A)))
		return;

	outw(inw(chip->io_port + 4) & 1, chip->io_port + 4);

	if (event & ESM_HWVOL_IRQ) {
		/* XXX if we have a hw volume control int enable
		   all the ints?  doesn't make sense.. */
		event = inw(chip->io_port + 0x18);
		outb(0xFF, chip->io_port + 0x1A);
	} else {
		/* else ack 'em all, i imagine */
		outb(0xFF, chip->io_port + 0x1A);
	}

	if ((event & ESM_MPU401_IRQ) && chip->rmidi) {
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
	}

	if (event & ESM_SOUND_IRQ) {
		struct list_head *p;
		spin_lock(&chip->substream_lock);
		list_for_each(p, &chip->substream_list) {
			esschan_t *es = list_entry(p, esschan_t, list);
			snd_es1968_update_pcm(chip, es);
		}
		spin_unlock(&chip->substream_lock);
	}
}

/*
 *  Mixer stuff
 */

static int __init
snd_es1968_mixer(es1968_t *chip)
{
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_es1968_ac97_write;
	ac97.read = snd_es1968_ac97_read;
	ac97.private_data = chip;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;
	return 0;
}

/*
 * reset ac97 codec
 */

static void snd_es1968_ac97_reset(es1968_t *chip)
{
	unsigned long ioaddr = chip->io_port;

	unsigned short save_ringbus_a;
	unsigned short save_68;
	unsigned short w;
	unsigned int vend;

	/* save configuration */
	save_ringbus_a = inw(ioaddr + 0x36);

	//outw(inw(ioaddr + 0x38) & 0xfffc, ioaddr + 0x38); /* clear second codec id? */
	/* set command/status address i/o to 1st codec */
	outw(inw(ioaddr + 0x3a) & 0xfffc, ioaddr + 0x3a);
	outw(inw(ioaddr + 0x3c) & 0xfffc, ioaddr + 0x3c);

	/* disable ac link */
	outw(0x0000, ioaddr + 0x36);
	save_68 = inw(ioaddr + 0x68);
	pci_read_config_word(chip->pci, 0x58, &w);	/* something magical with gpio and bus arb. */
	pci_read_config_dword(chip->pci, PCI_SUBSYSTEM_VENDOR_ID, &vend);
	if (w & 1)
		save_68 |= 0x10;
	outw(0xfffe, ioaddr + 0x64);	/* unmask gpio 0 */
	outw(0x0001, ioaddr + 0x68);	/* gpio write */
	outw(0x0000, ioaddr + 0x60);	/* write 0 to gpio 0 */
	udelay(20);
	outw(0x0001, ioaddr + 0x60);	/* write 1 to gpio 1 */
	mdelay(20);

	outw(save_68 | 0x1, ioaddr + 0x68);	/* now restore .. */
	outw((inw(ioaddr + 0x38) & 0xfffc) | 0x1, ioaddr + 0x38);
	outw((inw(ioaddr + 0x3a) & 0xfffc) | 0x1, ioaddr + 0x3a);
	outw((inw(ioaddr + 0x3c) & 0xfffc) | 0x1, ioaddr + 0x3c);

	/* now the second codec */
	/* disable ac link */
	outw(0x0000, ioaddr + 0x36);
	outw(0xfff7, ioaddr + 0x64);	/* unmask gpio 3 */
	save_68 = inw(ioaddr + 0x68);
	outw(0x0009, ioaddr + 0x68);	/* gpio write 0 & 3 ?? */
	outw(0x0001, ioaddr + 0x60);	/* write 1 to gpio */
	udelay(20);
	outw(0x0009, ioaddr + 0x60);	/* write 9 to gpio */
	mdelay(500);		/* .. ouch.. */
	//outw(inw(ioaddr + 0x38) & 0xfffc, ioaddr + 0x38);
	outw(inw(ioaddr + 0x3a) & 0xfffc, ioaddr + 0x3a);
	outw(inw(ioaddr + 0x3c) & 0xfffc, ioaddr + 0x3c);

#if 0				/* the loop here needs to be much better if we want it.. */
	snd_printk("trying software reset\n");
	/* try and do a software reset */
	outb(0x80 | 0x7c, ioaddr + 0x30);
	for (w = 0;; w++) {
		if ((inw(ioaddr + 0x30) & 1) == 0) {
			if (inb(ioaddr + 0x32) != 0)
				break;

			outb(0x80 | 0x7d, ioaddr + 0x30);
			if (((inw(ioaddr + 0x30) & 1) == 0)
			    && (inb(ioaddr + 0x32) != 0))
				break;
			outb(0x80 | 0x7f, ioaddr + 0x30);
			if (((inw(ioaddr + 0x30) & 1) == 0)
			    && (inb(ioaddr + 0x32) != 0))
				break;
		}

		if (w > 10000) {
			outb(inb(ioaddr + 0x37) | 0x08, ioaddr + 0x37);	/* do a software reset */
			mdelay(500);	/* oh my.. */
			outb(inb(ioaddr + 0x37) & ~0x08,
				ioaddr + 0x37);
			udelay(1);
			outw(0x80, ioaddr + 0x30);
			for (w = 0; w < 10000; w++) {
				if ((inw(ioaddr + 0x30) & 1) == 0)
					break;
			}
		}
	}
#endif
	if (vend == NEC_VERSA_SUBID1 || vend == NEC_VERSA_SUBID2) {
		/* turn on external amp? */
		outw(0xf9ff, ioaddr + 0x64);
		outw(inw(ioaddr + 0x68) | 0x600, ioaddr + 0x68);
		outw(0x0209, ioaddr + 0x60);
	}

	/* restore.. */
	outw(save_ringbus_a, ioaddr + 0x36);
}

static void snd_es1968_reset(es1968_t *chip)
{
	/* Reset */
	outw(ESM_RESET_MAESTRO | ESM_RESET_DIRECTSOUND,
	     chip->io_port + ESM_PORT_HOST_IRQ);
	udelay(10);
	outw(0x0000, chip->io_port + ESM_PORT_HOST_IRQ);
	udelay(10);
}

/*
 * power management
 */
static void snd_es1968_set_acpi(es1968_t *chip, int state)
{
	u16 active_mask = acpi_state_mask[state];

	pci_set_power_state(chip->pci, state);
	/* make sure the units we care about are on 
		XXX we might want to do this before state flipping? */
	pci_write_config_word(chip->pci, 0x54, ~ active_mask);
	pci_write_config_word(chip->pci, 0x56, ~ active_mask);
}


/*
 * initialize maestro chip
 */
static void snd_es1968_chip_init(es1968_t *chip)
{
	struct pci_dev *pci = chip->pci;
	int i;
	unsigned long iobase  = chip->io_port;
	u16 w;
	u32 n;

	/* We used to muck around with pci config space that
	 * we had no business messing with.  We don't know enough
	 * about the machine to know which DMA mode is appropriate, 
	 * etc.  We were guessing wrong on some machines and making
	 * them unhappy.  We now trust in the BIOS to do things right,
	 * which almost certainly means a new host of problems will
	 * arise with broken BIOS implementations.  screw 'em. 
	 * We're already intolerant of machines that don't assign
	 * IRQs.
	 */
	
	/* do config work at full power */
	snd_es1968_set_acpi(chip, ACPI_D0);

	/* Config Reg A */
	pci_read_config_word(pci, ESM_CONFIG_A, &w);

	/*      Use TDMA for now. TDMA works on all boards, so while its
	 *      not the most efficient its the simplest. */
	w &= ~DMA_CLEAR;	/* Clear DMA bits */
	w |= DMA_TDMA;		/* TDMA on */
	w &= ~(PIC_SNOOP1 | PIC_SNOOP2);	/* Clear Pic Snoop Mode Bits */
	w &= ~SAFEGUARD;	/* Safeguard off */
	w |= POST_WRITE;	/* Posted write */
	w |= ISA_TIMING;	/* ISA timing on */
	/* XXX huh?  claims to be reserved.. */
	w &= ~SWAP_LR;		/* swap left/right 
				   seems to only have effect on SB
				   Emulation */
	w &= ~SUBTR_DECODE;	/* Subtractive decode off */

	pci_write_config_word(pci, ESM_CONFIG_A, w);

	/* Config Reg B */

	pci_read_config_word(pci, ESM_CONFIG_B, &w);

	w &= ~(1 << 15);	/* Turn off internal clock multiplier */
	/* XXX how do we know which to use? */
	w &= ~(1 << 14);	/* External clock */

	w &= ~SPDIF_CONFB;	/* disable S/PDIF output */
	w &= ~HWV_CONFB;	/* HWV off */
	w &= ~DEBOUNCE;		/* Debounce off */
	w &= ~GPIO_CONFB;	/* GPIO 4:5 */
	w |= CHI_CONFB;		/* Disconnect from the CHI.  Enabling this made a dell 7500 work. */
	w &= ~IDMA_CONFB;	/* IDMA off (undocumented) */
	w &= ~MIDI_FIX;		/* MIDI fix off (undoc) */
	w &= ~(1 << 1);		/* reserved, always write 0 */
	w &= ~IRQ_TO_ISA;	/* IRQ to ISA off (undoc) */

	pci_write_config_word(pci, ESM_CONFIG_B, w);

	/* DDMA off */

	pci_read_config_word(pci, ESM_DDMA, &w);
	w &= ~(1 << 0);
	pci_write_config_word(pci, ESM_DDMA, w);

	/*
	 *	Legacy mode
	 */

	pci_read_config_word(pci, ESM_LEGACY_AUDIO_CONTROL, &w);

	w &= ~ESS_ENABLE_AUDIO;	/* Disable Legacy Audio */
	w &= ~ESS_ENABLE_SERIAL_IRQ;	/* Disable SIRQ */
	w &= ~(0x1f);		/* disable mpu irq/io, game port, fm, SB */

	pci_write_config_word(pci, ESM_LEGACY_AUDIO_CONTROL, w);

	/* Sound Reset */

	snd_es1968_reset(chip);

	/*
	 *	Ring Bus Setup
	 */

	/* setup usual 0x34 stuff.. 0x36 may be chip specific */
	outw(0xC090, iobase + ESM_RING_BUS_DEST); /* direct sound, stereo */
	udelay(20);
	outw(0x3000, iobase + ESM_RING_BUS_CONTR_A); /* enable ringbus/serial */
	udelay(20);

	/*
	 *	Reset the CODEC
	 */
	 
	snd_es1968_ac97_reset(chip);

	/* Ring Bus Control B */

	n = inl(iobase + ESM_RING_BUS_CONTR_B);
	n &= ~RINGB_EN_SPDIF;	/* SPDIF off */
	//w |= RINGB_EN_2CODEC;	/* enable 2nd codec */
	outl(n, iobase + ESM_RING_BUS_CONTR_B);

	/* it appears some maestros (dell 7500) only work if these are set,
	   regardless of wether we use the assp or not. */

	outb(0, iobase + ASSP_CONTROL_B);
	outb(3, iobase + ASSP_CONTROL_A);	/* M: Reserved bits... */
	outb(0, iobase + ASSP_CONTROL_C);	/* M: Disable ASSP, ASSP IRQ's and FM Port */

	/* Enable IRQ's */

	if (chip->midi_enabled)
		w = ESM_HIRQ_DSIE | ESM_HIRQ_MPU401;
	else
		w = ESM_HIRQ_DSIE;

	outw(w, iobase + ESM_PORT_HOST_IRQ);

	/*
	 * set up wavecache
	 */
	for (i = 0; i < 16; i++) {
		/* Write 0 into the buffer area 0x1E0->1EF */
		outw(0x01E0 + i, iobase + WC_INDEX);
		outw(0x0000, iobase + WC_DATA);

		/* The 1.10 test program seem to write 0 into the buffer area
		 * 0x1D0-0x1DF too.*/
		outw(0x01D0 + i, iobase + WC_INDEX);
		outw(0x0000, iobase + WC_DATA);
	}
	wave_set_register(chip, IDR7_WAVE_ROMRAM,
			  (wave_get_register(chip, IDR7_WAVE_ROMRAM) & 0xFF00));
	wave_set_register(chip, IDR7_WAVE_ROMRAM,
			  wave_get_register(chip, IDR7_WAVE_ROMRAM) | 0x100);
	wave_set_register(chip, IDR7_WAVE_ROMRAM,
			  wave_get_register(chip, IDR7_WAVE_ROMRAM) & ~0x200);
	wave_set_register(chip, IDR7_WAVE_ROMRAM,
			  wave_get_register(chip, IDR7_WAVE_ROMRAM) | ~0x400);


	maestro_write(chip, IDR2_CRAM_DATA, 0x0000);
	/* Now back to the DirectSound stuff */
	/* audio serial configuration.. ? */
	maestro_write(chip, 0x08, 0xB004);
	maestro_write(chip, 0x09, 0x001B);
	maestro_write(chip, 0x0A, 0x8000);
	maestro_write(chip, 0x0B, 0x3F37);
	maestro_write(chip, 0x0C, 0x0098);

	/* parallel in, has something to do with recording :) */
	maestro_write(chip, 0x0C,
		      (maestro_read(chip, 0x0C) & ~0xF000) | 0x8000);
	/* parallel out */
	maestro_write(chip, 0x0C,
		      (maestro_read(chip, 0x0C) & ~0x0F00) | 0x0500);

	maestro_write(chip, 0x0D, 0x7632);

	/* Wave cache control on - test off, sg off, 
	   enable, enable extra chans 1Mb */

	w = inw(iobase + WC_CONTROL);

	w &= ~0xFA00;		/* Seems to be reserved? I don't know */
	w |= 0xA000;		/* reserved... I don't know */
	w &= ~0x0200;		/* Channels 56,57,58,59 as Extra Play,Rec Channel enable
				   Seems to crash the Computer if enabled... */
	w |= 0x0100;		/* Wave Cache Operation Enabled */
	w |= 0x0080;		/* Channels 60/61 as Placback/Record enabled */
	w &= ~0x0060;		/* Clear Wavtable Size */
	w |= 0x0020;		/* Wavetable Size : 1MB */
	/* Bit 4 is reserved */
	w &= ~0x000C;		/* DMA Stuff? I don't understand what the datasheet means */
	/* Bit 1 is reserved */
	w &= ~0x0001;		/* Test Mode off */

	outw(w, iobase + WC_CONTROL);

	/* Now clear the APU control ram */
	for (i = 0; i < NR_APUS; i++) {
		for (w = 0; w < NR_APU_REGS; w++)
			apu_set_register(chip, i, w, 0);

	}
}

#ifdef CONFIG_PM
/*
 * PM support
 */
#ifdef PCI_NEW_SUSPEND
static int snd_es1968_suspend(struct pci_dev *dev, u32 state)
#else
static void snd_es1968_suspend(struct pci_dev *dev)
#endif
{
#ifdef PCI_NEW_SUSPEND
	es1968_t *chip = snd_magic_cast(es1968_t, PCI_GET_DRIVER_DATA(dev), return -ENXIO);
#else
	es1968_t *chip = snd_magic_cast(es1968_t, PCI_GET_DRIVER_DATA(dev), return);
#endif
	unsigned long flags;
	struct list_head *p;

	/* we have to read from the apu regs, need to power it up */
	snd_es1968_set_acpi(chip, ACPI_D0);

	spin_lock_irqsave(&chip->substream_lock, flags);
	list_for_each(p, &chip->substream_list) {
		esschan_t *es = list_entry(p, esschan_t, list);
		if (es->running) {
			snd_es1968_pcm_stop(chip, es);
			es->running = 1; /* to flip again at resume */
		}
	}

	spin_unlock_irqrestore(&chip->substream_lock, flags);

	snd_es1968_bob_stop(chip);

	/* we trust in the bios to power down the chip on suspend.
	 * XXX I'm also not sure that in_suspend will protect
	 * against all reg accesses from here on out. 
	 */
#ifdef PCI_NEW_SUSPEND
	return 0;
#endif
}

#ifdef PCI_NEW_SUSPEND
static int snd_es1968_resume(struct pci_dev *dev)
#else
static void snd_es1968_resume(struct pci_dev *dev)
#endif
{
#ifdef PCI_NEW_SUSPEND
	es1968_t *chip = snd_magic_cast(es1968_t, PCI_GET_DRIVER_DATA(dev), return -ENXIO);
#else
	es1968_t *chip = snd_magic_cast(es1968_t, PCI_GET_DRIVER_DATA(dev), return);
#endif
	unsigned long flags;
	struct list_head *p;
	int i, reg;

	/* restore all our config */
	snd_es1968_chip_init(chip);

	/* need to restore the base pointers.. */ 
	if (chip->dma_buf_addr) {
		/* set PCMBAR */
		wave_set_register(chip, 0x01FC, chip->dma_buf_addr >> 12);
	}

	/* restore ac97 state */
	snd_ac97_resume(chip->ac97);

	/* set each channels' apu control registers before
	 * restoring audio 
	 */
	spin_lock_irqsave(&chip->substream_lock, flags);
	list_for_each(p, &chip->substream_list) {
		esschan_t *es = list_entry(p, esschan_t, list);
		if (! es->running)
			continue;
		for (i = 0; i < 4; i++) {
			wave_set_register(chip, es->apu[i]<<3, es->wc_map[i]);
			/* don't set reg 0 here */
			for (reg = 1; reg < NR_APU_REGS; reg++)  
				apu_set_register(chip, i, reg, chip->apu_map[es->apu[i]][reg]);
		}
		snd_es1968_pcm_start(chip, es);
	}
	spin_unlock_irqrestore(&chip->substream_lock, flags);

	/* start timer again */
	if (atomic_read(&chip->bobclient))
		snd_es1968_bob_start(chip);
#ifdef PCI_NEW_SUSPEND
	return 0;
#endif
}
#endif

static int snd_es1968_free(es1968_t *chip)
{
	snd_es1968_set_acpi(chip, ACPI_D3);
	if (chip->res_io_port)
		release_resource(chip->res_io_port);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	snd_magic_kfree(chip);
	return 0;
}

static int snd_es1968_dev_free(snd_device_t *device)
{
	es1968_t *chip = snd_magic_cast(es1968_t, device->device_data, return -ENXIO);
	return snd_es1968_free(chip);
}

static int __init snd_es1968_create(snd_card_t * card,
				    struct pci_dev *pci,
				    int midi_enabled,
				    int total_bufsize,
				    int play_streams,
				    int capt_streams,
				    es1968_t **chip_ret)
{
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_es1968_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:       snd_es1968_dev_free,
	};
#endif
	es1968_t *chip;
	int i, err;

	*chip_ret = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 28 bits */
	if (!pci_dma_supported(pci, 0x0fffffff)) {
		snd_printk("architecture does not support 28bit PCI busmaster DMA\n");
		return -ENXIO;
	}
	pci_set_dma_mask(pci, 0x0fffffff);

	chip = (es1968_t *) snd_magic_kcalloc(es1968_t, 0, GFP_KERNEL);
	if (! chip)
		return -ENOMEM;

	/* Set Vars */
	chip->type = (pci->vendor << 16) | pci->device;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->substream_lock);
	spin_lock_init(&chip->bob_lock);
	INIT_LIST_HEAD(&chip->buf_list);
	INIT_LIST_HEAD(&chip->substream_list);
	init_MUTEX(&chip->memory_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->midi_enabled = midi_enabled;
	chip->total_bufsize = total_bufsize;	/* in bytes */
	chip->playback_streams = play_streams;
	chip->capture_streams = capt_streams;

	chip->io_port = pci_resource_start(pci, 0);
	if ((chip->res_io_port = request_region(chip->io_port, 0x100, "ESS Maestro")) == NULL) {
		snd_es1968_free(chip);
		snd_printk("unable to grab region 0x%lx-0x%lx\n", chip->io_port, chip->io_port + 0x100 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_es1968_interrupt, SA_INTERRUPT|SA_SHIRQ,
			"ESS Maestro", (void*)chip)) {
		snd_es1968_free(chip);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	        
	/* Clear Maestro_map */
	for (i = 0; i < 32; i++)
		chip->maestro_map[i] = 0;

	/* Clear Apu Map */
	for (i = 0; i < NR_APUS; i++)
		chip->apu[i] = ESM_APU_FREE;

	atomic_set(&chip->bobclient, 0);

	/* just to be sure */
	pci_set_master(pci);

	snd_es1968_chip_init(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_es1968_free(chip);
		return err;
	}

	*chip_ret = chip;

	return 0;
}

/* *************
   * Midi Part *
   *************/
static int __init
snd_es1968_midi(es1968_t *chip, int device, snd_rawmidi_t ** rawmidi)
{
	unsigned long mpu_port;
	int err;
	snd_rawmidi_t *rm;

	rm = NULL;

	mpu_port = chip->io_port + ESM_MPU401_PORT;

	err = snd_mpu401_uart_new(chip->card, device,
				  MPU401_HW_MPU401,
				  mpu_port, 1, chip->irq, 0, &rm);
	if (err < 0)
		return err;

	chip->rmidi = rm;
	if (rawmidi)
		*rawmidi = rm;
	return 0;
}

/*
 */
static int __init snd_es1968_probe(struct pci_dev *pci,
				   const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	es1968_t *chip;
	int err;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev]) {
			dev++;
			return -ENOENT;
		}
		break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (!card)
		return -ENOMEM;
                
	if (snd_total_bufsize[dev] < 128)
		snd_total_bufsize[dev] = 128;
	if (snd_total_bufsize[dev] > 4096)
		snd_total_bufsize[dev] = 4096;
	if ((err = snd_es1968_create(card, pci,
				     snd_midi_enable[dev],
				     snd_total_bufsize[dev] * 1024, /* in bytes */
				     snd_pcm_substreams_p[dev], 
				     snd_pcm_substreams_c[dev],
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	switch (chip->type) {
	case CARD_TYPE_ESS_ES1978:
		strcpy(card->driver, "ES1978");
		strcpy(card->shortname, "ESS ES1978 (Maestro 2E)");
		break;
	case CARD_TYPE_ESS_ES1968:
		strcpy(card->driver, "ES1968");
		strcpy(card->shortname, "ESS ES1968 (Maestro 2)");
		break;
	case CARD_TYPE_ESS_ESOLDM1:
		strcpy(card->driver, "ESM1");
		strcpy(card->shortname, "ESS Maestro 1");
		break;
	}

	if ((err = snd_es1968_pcm(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_es1968_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (snd_midi_enable[dev]) {
		if ((err = snd_es1968_midi(chip, 0, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->io_port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	PCI_SET_DRIVER_DATA(pci, chip);
	dev++;
	return 0;
}

static void __exit snd_es1968_remove(struct pci_dev *pci)
{
	es1968_t *chip = snd_magic_cast(es1968_t, PCI_GET_DRIVER_DATA(pci), return);
	if (chip)
		snd_card_free(chip->card);
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0, 0, "ES1968 (ESS Maestro)",
	snd_es1968_ids,
	snd_es1968_probe,
	snd_es1968_remove,
#ifdef CONFIG_PM
	snd_es1968_suspend,
	snd_es1968_resume,
#else   
        0, 0
#endif
};
#else
static struct pci_driver driver = {
	name: "ES1968 (ESS Maestro)",
	id_table: snd_es1968_ids,
	probe: snd_es1968_probe,
	remove: snd_es1968_remove,
#ifdef CONFIG_PM
	suspend: snd_es1968_suspend,
	resume: snd_es1968_resume,
#endif
};
#endif

static int snd_es1968_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
	pci_unregister_driver(&driver);
	return NOTIFY_OK;
}

static struct notifier_block snd_es1968_nb = {snd_es1968_notifier, NULL, 0};

static int __init alsa_card_es1968_init(void)
{
	int err;

        if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("ESS Maestro soundcard not found or device busy\n");
#endif
		return err;
	}
	/* If this driver is not shutdown cleanly at reboot, it can
	   leave the speaking emitting an annoying noise, so we catch
	   shutdown events. */ 
	if (register_reboot_notifier(&snd_es1968_nb)) {
		snd_printk("reboot notifier registration failed; may make noise at shutdown.\n");
	}
	return 0;
}

static void __exit alsa_card_es1968_exit(void)
{
	unregister_reboot_notifier(&snd_es1968_nb);
	pci_unregister_driver(&driver);
}

module_init(alsa_card_es1968_init)
module_exit(alsa_card_es1968_exit)

#ifndef MODULE

/* format is: snd-card-es1968=snd_enable,snd_index,snd_id,
			      snd_total_bufsize,snd_midi_enable,
			      snd_pcm_substreams_p,
			      snd_pcm_substreams_c */

static int __init alsa_card_es1968_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_total_bufsize[nr_dev]) == 2 &&
	       get_option(&str,&snd_midi_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_substreams_p[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_substreams_c[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-es1968=", alsa_card_es1968_setup);

#endif /* ifndef MODULE */
