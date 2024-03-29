/*
 *   Driver for the Korg 1212 IO PCI card
 *
 *	Copyright (c) 2001 Haroldo Gamal <gamal@alternex.com.br>
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define DEBUG                    1
//#define LARGEALLOC               1
#define PRINTK	printk

// ----------------------------------------------------------------------------
// the following enum defines the valid states of the Korg 1212 I/O card.
// ----------------------------------------------------------------------------
typedef enum {
   K1212_STATE_NONEXISTENT,		// there is no card here
   K1212_STATE_UNINITIALIZED,		// the card is awaiting DSP download
   K1212_STATE_DSP_IN_PROCESS,		// the card is currently downloading its DSP code
   K1212_STATE_DSP_COMPLETE,		// the card has finished the DSP download
   K1212_STATE_READY,			// the card can be opened by an application.  Any application
					//    requests prior to this state should fail.  Only an open
					//    request can be made at this state.
   K1212_STATE_OPEN,			// an application has opened the card
   K1212_STATE_SETUP,			// the card has been setup for play
   K1212_STATE_PLAYING,			// the card is playing
   K1212_STATE_MONITOR,			// the card is in the monitor mode
   K1212_STATE_CALIBRATING,		// the card is currently calibrating
   K1212_STATE_ERRORSTOP,		// the card has stopped itself because of an error and we
					//    are in the process of cleaning things up.
   K1212_STATE_MAX_STATE		// state values of this and beyond are invalid
} CardState;

// ----------------------------------------------------------------------------
// The following enumeration defines the constants written to the card's
// host-to-card doorbell to initiate a command.
// ----------------------------------------------------------------------------
typedef enum {
   K1212_DB_RequestForData        = 0,    // sent by the card to request a buffer fill.
   K1212_DB_TriggerPlay           = 1,    // starts playback/record on the card.
   K1212_DB_SelectPlayMode        = 2,    // select monitor, playback setup, or stop.
   K1212_DB_ConfigureBufferMemory = 3,    // tells card where the host audio buffers are.
   K1212_DB_RequestAdatTimecode   = 4,    // asks the card for the latest ADAT timecode value.
   K1212_DB_SetClockSourceRate    = 5,    // sets the clock source and rate for the card.
   K1212_DB_ConfigureMiscMemory   = 6,    // tells card where other buffers are.
   K1212_DB_TriggerFromAdat       = 7,    // tells card to trigger from Adat at a specific
                                          //    timecode value.
   K1212_DB_RebootCard            = 0xA0, // instructs the card to reboot.
   K1212_DB_BootFromDSPPage4      = 0xA4, // instructs the card to boot from the DSP microcode
                                          //    on page 4 (local page to card).
   K1212_DB_DSPDownloadDone       = 0xAE, // sent by the card to indicate the download has
                                          //    completed.
   K1212_DB_StartDSPDownload      = 0xAF  // tells the card to download its DSP firmware.
} korg1212_dbcnst_t;

#define K1212_ISRCODE_DMAERROR      0x80
#define K1212_ISRCODE_CARDSTOPPED   0x81

// ----------------------------------------------------------------------------
// The following enumeration defines return codes for DeviceIoControl() calls
// to the Korg 1212 I/O driver.
// ----------------------------------------------------------------------------
typedef enum {
   K1212_CMDRET_Success         = 0,   // command was successfully placed
   K1212_CMDRET_DIOCFailure,           // the DeviceIoControl call failed
   K1212_CMDRET_PMFailure,             // the protected mode call failed
   K1212_CMDRET_FailUnspecified,       // unspecified failure
   K1212_CMDRET_FailBadState,          // the specified command can not be given in
                                       //    the card's current state. (or the wave device's
                                       //    state)
   K1212_CMDRET_CardUninitialized,     // the card is uninitialized and cannot be used
   K1212_CMDRET_BadIndex,              // an out of range card index was specified
   K1212_CMDRET_BadHandle,             // an invalid card handle was specified
   K1212_CMDRET_NoFillRoutine,         // a play request has been made before a fill routine set
   K1212_CMDRET_FillRoutineInUse,      // can't set a new fill routine while one is in use
   K1212_CMDRET_NoAckFromCard,         // the card never acknowledged a command
   K1212_CMDRET_BadParams,             // bad parameters were provided by the caller

   // --------------------------------------------------------------
   // the following return errors are specific to the wave device
   // driver interface.  These will not be encountered by users of
   // the 32 bit DIOC interface (a.k.a. custom or native API).
   // --------------------------------------------------------------
   K1212_CMDRET_BadDevice,             // the specified wave device was out of range
   K1212_CMDRET_BadFormat              // the specified wave format is unsupported
} snd_korg1212rc;

// ----------------------------------------------------------------------------
// The following enumeration defines the constants used to select the play
// mode for the card in the SelectPlayMode command.
// ----------------------------------------------------------------------------
typedef enum {
   K1212_MODE_SetupPlay  = 0x00000001,     // provides card with pre-play information
   K1212_MODE_MonitorOn  = 0x00000002,     // tells card to turn on monitor mode
   K1212_MODE_MonitorOff = 0x00000004,     // tells card to turn off monitor mode
   K1212_MODE_StopPlay   = 0x00000008      // stops playback on the card
} PlayModeSelector;

// ----------------------------------------------------------------------------
// The following enumeration defines the constants used to select the monitor
// mode for the card in the SetMonitorMode command.
// ----------------------------------------------------------------------------
typedef enum {
   K1212_MONMODE_Off  = 0,     // tells card to turn off monitor mode
   K1212_MONMODE_On            // tells card to turn on monitor mode
} MonitorModeSelector;

#define MAILBOX0_OFFSET      0x40	// location of mailbox 0 relative to base address
#define MAILBOX1_OFFSET      0x44	// location of mailbox 1 relative to base address
#define MAILBOX2_OFFSET      0x48	// location of mailbox 2 relative to base address
#define MAILBOX3_OFFSET      0x4c	// location of mailbox 3 relative to base address
#define OUT_DOORBELL_OFFSET  0x60	// location of PCI to local doorbell "
#define IN_DOORBELL_OFFSET   0x64	// location of local to PCI doorbell "
#define STATUS_REG_OFFSET    0x68	// location of interrupt control/status register "
#define PCI_CONTROL_OFFSET   0x6c	// location of the EEPROM, PCI, User I/O, init control
					//    register
#define SENS_CONTROL_OFFSET  0x6e	// location of the input sensitivity setting register.
					//    this is the upper word of the PCI control reg.
#define DEV_VEND_ID_OFFSET   0x70	// location of the device and vendor ID register

#define COMMAND_ACK_DELAY    13        // number of RTC ticks to wait for an acknowledgement
                                        //    from the card after sending a command.
#define INTERCOMMAND_DELAY   40
#define MAX_COMMAND_RETRIES  5         // maximum number of times the driver will attempt
                                       //    to send a command before giving up.
#define COMMAND_ACK_MASK     0x8000    // the MSB is set in the command acknowledgment from
                                        //    the card.
#define DOORBELL_VAL_MASK    0x00FF    // the doorbell value is one byte

#define CARD_BOOT_DELAY_IN_MS  10

#define DSP_BOOT_DELAY_IN_MS   200

#define kNumBuffers		8
#define k1212MaxCards		4
#define k1212NumWaveDevices	6
#define k16BitChannels		10
#define k32BitChannels		2
#define kAudioChannels		(k16BitChannels + k32BitChannels)
#define kPlayBufferFrames	1024

#define K1212_CHANNELS     	16
#define K1212_FRAME_SIZE        (sizeof(KorgAudioFrame))
#define K1212_MAX_SAMPLES	(kPlayBufferFrames*kNumBuffers)
#define K1212_PERIODS		(K1212_BUF_SIZE/K1212_BLOCK_SIZE)
#define K1212_PERIOD_BYTES	(K1212_BLOCK_SIZE)
#define K1212_BLOCK_SIZE        (K1212_FRAME_SIZE*kPlayBufferFrames)
#define K1212_BUF_SIZE          (K1212_BLOCK_SIZE*kNumBuffers)

#define k1212MinADCSens     0x7f
#define k1212MaxADCSens     0x00
#define k1212MaxVolume      0x7fff
#define k1212MaxWaveVolume  0xffff
#define k1212MinVolume      0x0000
#define k1212MaxVolInverted 0x8000

// -----------------------------------------------------------------
// the following bits are used for controlling interrupts in the
// interrupt control/status reg
// -----------------------------------------------------------------
#define  PCI_INT_ENABLE_BIT               0x00000100
#define  PCI_DOORBELL_INT_ENABLE_BIT      0x00000200
#define  LOCAL_INT_ENABLE_BIT             0x00010000
#define  LOCAL_DOORBELL_INT_ENABLE_BIT    0x00020000
#define  LOCAL_DMA1_INT_ENABLE_BIT        0x00080000

// -----------------------------------------------------------------
// the following bits are defined for the PCI command register
// -----------------------------------------------------------------
#define  PCI_CMD_MEM_SPACE_ENABLE_BIT     0x0002
#define  PCI_CMD_IO_SPACE_ENABLE_BIT      0x0001
#define  PCI_CMD_BUS_MASTER_ENABLE_BIT    0x0004

// -----------------------------------------------------------------
// the following bits are defined for the PCI status register
// -----------------------------------------------------------------
#define  PCI_STAT_PARITY_ERROR_BIT        0x8000
#define  PCI_STAT_SYSTEM_ERROR_BIT        0x4000
#define  PCI_STAT_MASTER_ABORT_RCVD_BIT   0x2000
#define  PCI_STAT_TARGET_ABORT_RCVD_BIT   0x1000
#define  PCI_STAT_TARGET_ABORT_SENT_BIT   0x0800

// ------------------------------------------------------------------------
// the following constants are used in setting the 1212 I/O card's input
// sensitivity.
// ------------------------------------------------------------------------
#define  SET_SENS_LOCALINIT_BITPOS        15
#define  SET_SENS_DATA_BITPOS             10
#define  SET_SENS_CLOCK_BITPOS            8
#define  SET_SENS_LOADSHIFT_BITPOS        0

#define  SET_SENS_LEFTCHANID              0x00
#define  SET_SENS_RIGHTCHANID             0x01

#define  K1212SENSUPDATE_DELAY_IN_MS      50

// --------------------------------------------------------------------------
// WaitRTCTicks
//
//    This function waits the specified number of real time clock ticks.
//    According to the DDK, each tick is ~0.8 microseconds.
//    The defines following the function declaration can be used for the
//    numTicksToWait parameter.
// --------------------------------------------------------------------------
#define ONE_RTC_TICK         1
#define SENSCLKPULSE_WIDTH   4
#define LOADSHIFT_DELAY      4
#define INTERCOMMAND_DELAY  40
#define STOPCARD_DELAY      300        // max # RTC ticks for the card to stop once we write
                                       //    the command register.  (could be up to 180 us)
#define COMMAND_ACK_DELAY   13         // number of RTC ticks to wait for an acknowledgement
                                       //    from the card after sending a command.

#include "korg1212-firmware.h"

typedef struct _snd_korg1212 korg1212_t;

typedef u16 K1212Sample;          // channels 0-9 use 16 bit samples
typedef u32 K1212SpdifSample;     // channels 10-11 use 32 bits - only 20 are sent
                                  //  across S/PDIF.
typedef u32 K1212TimeCodeSample;  // holds the ADAT timecode value

typedef enum {
   K1212_CLKIDX_AdatAt44_1K = 0,    // selects source as ADAT at 44.1 kHz
   K1212_CLKIDX_AdatAt48K,          // selects source as ADAT at 48 kHz
   K1212_CLKIDX_WordAt44_1K,        // selects source as S/PDIF at 44.1 kHz
   K1212_CLKIDX_WordAt48K,          // selects source as S/PDIF at 48 kHz
   K1212_CLKIDX_LocalAt44_1K,       // selects source as local clock at 44.1 kHz
   K1212_CLKIDX_LocalAt48K,         // selects source as local clock at 48 kHz
   K1212_CLKIDX_Invalid             // used to check validity of the index
} ClockSourceIndex;

typedef enum {
   K1212_CLKIDX_Adat = 0,    // selects source as ADAT
   K1212_CLKIDX_Word,        // selects source as S/PDIF
   K1212_CLKIDX_Local        // selects source as local clock
} ClockSourceType;

typedef struct KorgAudioFrame {
   K1212Sample          frameData16[k16BitChannels];
   K1212SpdifSample     frameData32[k32BitChannels];
   K1212TimeCodeSample  timeCodeVal;
} KorgAudioFrame;

typedef struct KorgAudioBuffer {
   KorgAudioFrame  bufferData[kPlayBufferFrames];     /* buffer definition */
} KorgAudioBuffer;

typedef struct KorgSharedBuffer {
#ifdef LARGEALLOC
   KorgAudioBuffer   playDataBufs[kNumBuffers];
   KorgAudioBuffer   recordDataBufs[kNumBuffers];
#endif
   short             volumeData[kAudioChannels];
   u32               cardCommand;
   u16               routeData [kAudioChannels];
   u32               AdatTimeCode;                 // ADAT timecode value
} KorgSharedBuffer;

typedef struct SensBits {
   union {
      struct {
         unsigned int leftChanVal:8;
         unsigned int leftChanId:8;
      } v;
      u16  leftSensBits;
   } l;
   union {
      struct {
         unsigned int rightChanVal:8;
         unsigned int rightChanId:8;
      } v;
      u16  rightSensBits;
   } r;
} SensBits;

struct _snd_korg1212 {
        struct pci_dev *pci;
        snd_card_t *card;
        snd_pcm_t *pcm16;
        int irq;

        spinlock_t    lock;

        wait_queue_head_t wait;

        unsigned long iomem;
        unsigned long ioport;
	unsigned long iomem2;
        unsigned long irqcount;
        unsigned long inIRQ;
        unsigned long iobase;

	struct resource *res_iomem;
	struct resource *res_ioport;
	struct resource *res_iomem2;

        u32 dspCodeSize;
        u32 dspMemPhy;          // DSP memory block handle (Physical Address)
        void * dspMemPtr;       //            block memory (Virtual Address)

	u32 DataBufsSize;

        KorgAudioBuffer  * playDataBufsPtr;
        KorgAudioBuffer  * recordDataBufsPtr;

	KorgSharedBuffer * sharedBufferPtr;
	u32 RecDataPhy;
	u32 PlayDataPhy;
	unsigned long sharedBufferPhy;
	u32 VolumeTablePhy;
	u32 RoutingTablePhy;
	u32 AdatTimeCodePhy;

        u32 * statusRegPtr;	     // address of the interrupt status/control register
        u32 * outDoorbellPtr;	     // address of the host->card doorbell register
        u32 * inDoorbellPtr;	     // address of the card->host doorbell register
        u32 * mailbox0Ptr;	     // address of mailbox 0 on the card
        u32 * mailbox1Ptr;	     // address of mailbox 1 on the card
        u32 * mailbox2Ptr;	     // address of mailbox 2 on the card
        u32 * mailbox3Ptr;	     // address of mailbox 3 on the card
        u32 * controlRegPtr;	     // address of the EEPROM, PCI, I/O, Init ctrl reg
        u16 * sensRegPtr;	     // address of the sensitivity setting register
        u32 * idRegPtr;		     // address of the device and vendor ID registers


        size_t periodsize;
        size_t currentBuffer;

        snd_pcm_substream_t *playback_substream;
        snd_pcm_substream_t *capture_substream;
        snd_info_entry_t * proc_entry;

 	CardState cardState;
        int running;
        int idleMonitorOn;           // indicates whether the card is in idle monitor mode.
        u32 cmdRetryCount;           // tracks how many times we have retried sending to the card.

        ClockSourceIndex clkSrcRate; // sample rate and clock source

        ClockSourceType clkSource;   // clock source
        int clkRate;                 // clock rate

        int volumePhase[kAudioChannels];

        u16 leftADCInSens;           // ADC left channel input sensitivity
        u16 rightADCInSens;          // ADC right channel input sensitivity
};

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("korg1212");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{KORG,korg1212}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;     /* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	   /* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE; /* Enable this card */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Korg 1212 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Korg 1212 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Korg 1212 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_AUTHOR("Haroldo Gamal <gamal@alternex.com.br>");

static struct pci_device_id snd_korg1212_ids[] __devinitdata = {
	{ 0x10b5, 0x906d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ 0, }
};

static char* stateName[] = {
		        "Non-existent",
                        "Uninitialized",
                        "DSP download in process",
                        "DSP download complete",
                        "Ready",
                        "Open",
                        "Setup for play",
                        "Playing",
                        "Monitor mode on",
                        "Calibrating"
                        "Invalid"
};

static char* clockSourceTypeName[] = { "ADAT", "S/PDIF", "local" };

static char* clockSourceName[] = {
                        "ADAT at 44.1 kHz",
                        "ADAT at 48 kHz",
                        "S/PDIF at 44.1 kHz",
                        "S/PDIF at 48 kHz",
                        "local clock at 44.1 kHz",
                        "local clock at 48 kHz"
};

static char* channelName[] = {
                        "ADAT-1",
                        "ADAT-2",
                        "ADAT-3",
                        "ADAT-4",
                        "ADAT-5",
                        "ADAT-6",
                        "ADAT-7",
                        "ADAT-8",
                        "Analog-L",
                        "Analog-R",
                        "SPDIF-L",
                        "SPDIF-R",
};

u16 ClockSourceSelector[] = {0x8000,   // selects source as ADAT at 44.1 kHz
                             0x0000,   // selects source as ADAT at 48 kHz
                             0x8001,   // selects source as S/PDIF at 44.1 kHz
                             0x0001,   // selects source as S/PDIF at 48 kHz
                             0x8002,   // selects source as local clock at 44.1 kHz
                             0x0002    // selects source as local clock at 48 kHz
                            };

snd_korg1212rc rc;


MODULE_DEVICE_TABLE(pci, snd_korg1212_ids);

typedef union swap_u32 { unsigned char c[4]; u32 i; } swap_u32;

#ifdef SNDRV_BIG_ENDIAN
static u32 LowerWordSwap(u32 swappee)
#else
static u32 UpperWordSwap(u32 swappee)
#endif
{
   swap_u32 retVal, swapper;

   swapper.i = swappee;
   retVal.c[2] = swapper.c[3];
   retVal.c[3] = swapper.c[2];
   retVal.c[1] = swapper.c[1];
   retVal.c[0] = swapper.c[0];

   return retVal.i;
}

#ifdef SNDRV_BIG_ENDIAN
static u32 UpperWordSwap(u32 swappee)
#else
static u32 LowerWordSwap(u32 swappee)
#endif
{
   swap_u32 retVal, swapper;

   swapper.i = swappee;
   retVal.c[2] = swapper.c[2];
   retVal.c[3] = swapper.c[3];
   retVal.c[1] = swapper.c[0];
   retVal.c[0] = swapper.c[1];

   return retVal.i;
}

#if 0 /* not used */

static u32 EndianSwap(u32 swappee)
{
   swap_u32 retVal, swapper;

   swapper.i = swappee;
   retVal.c[0] = swapper.c[3];
   retVal.c[1] = swapper.c[2];
   retVal.c[2] = swapper.c[1];
   retVal.c[3] = swapper.c[0];

   return retVal.i;
}

#endif /* not used */

void TickDelay(int time)
{
        udelay(time);
}

#define SetBitInWord(theWord,bitPosition)       (*theWord) |= (0x0001 << bitPosition)
#define SetBitInDWord(theWord,bitPosition)      (*theWord) |= (0x00000001 << bitPosition)
#define ClearBitInWord(theWord,bitPosition)     (*theWord) &= ~(0x0001 << bitPosition)
#define ClearBitInDWord(theWord,bitPosition)    (*theWord) &= ~(0x00000001 << bitPosition)

static snd_korg1212rc snd_korg1212_Send1212Command(korg1212_t *korg1212, korg1212_dbcnst_t doorbellVal,
                            u32 mailBox0Val, u32 mailBox1Val, u32 mailBox2Val, u32 mailBox3Val)
{
        u32 retryCount;
        u16 mailBox3Lo;

        if (korg1212->outDoorbellPtr) {
#ifdef DEBUG
		PRINTK("DEBUG: Card <- 0x%08x 0x%08x [%s]\n", doorbellVal, mailBox0Val, stateName[korg1212->cardState]);
#endif
                for (retryCount = 0; retryCount < MAX_COMMAND_RETRIES; retryCount++) {

                        writel(mailBox3Val, korg1212->mailbox3Ptr);
                        writel(mailBox2Val, korg1212->mailbox2Ptr);
                        writel(mailBox1Val, korg1212->mailbox1Ptr);
                        writel(mailBox0Val, korg1212->mailbox0Ptr);
                        writel(doorbellVal, korg1212->outDoorbellPtr);  // interrupt the card

                        // --------------------------------------------------------------
                        // the reboot command will not give an acknowledgement.
                        // --------------------------------------------------------------
                        switch (doorbellVal) {
                                case K1212_DB_RebootCard:
                                case K1212_DB_BootFromDSPPage4:
                                case K1212_DB_StartDSPDownload:
                                        return K1212_CMDRET_Success;
                                default:
                                        break;
                        }

                        // --------------------------------------------------------------
                        // See if the card acknowledged the command.  Wait a bit, then
                        // read in the low word of mailbox3.  If the MSB is set and the
                        // low byte is equal to the doorbell value, then it ack'd.
                        // --------------------------------------------------------------
                        TickDelay(COMMAND_ACK_DELAY);
                        mailBox3Lo = readl(korg1212->mailbox3Ptr);
                        if (mailBox3Lo & COMMAND_ACK_MASK) {
                                if ((mailBox3Lo & DOORBELL_VAL_MASK) == (doorbellVal & DOORBELL_VAL_MASK)) {
                                        korg1212->cmdRetryCount += retryCount;
                                        return K1212_CMDRET_Success;
                                }
                        }
                }
                korg1212->cmdRetryCount += retryCount;
                return K1212_CMDRET_NoAckFromCard;
        } else {
                return K1212_CMDRET_CardUninitialized;
        }
}

static void snd_korg1212_WaitForCardStopAck(korg1212_t *korg1212)
{
        unsigned long endtime = jiffies + 20 * HZ;

#ifdef DEBUG
        PRINTK("DEBUG: WaitForCardStopAck [%s]\n", stateName[korg1212->cardState]);
#endif // DEBUG

        if (korg1212->inIRQ)
                return;

        do {
                if (readl(&korg1212->sharedBufferPtr->cardCommand) == 0)
                        return;
                if (!korg1212->inIRQ)
                        schedule();
        } while (jiffies < endtime);

        writel(0, &korg1212->sharedBufferPtr->cardCommand);
}

static void snd_korg1212_TurnOnIdleMonitor(korg1212_t *korg1212)
{
        TickDelay(INTERCOMMAND_DELAY);
        korg1212->idleMonitorOn = 1;
        rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SelectPlayMode,
                        K1212_MODE_MonitorOn, 0, 0, 0);
}

static void snd_korg1212_TurnOffIdleMonitor(korg1212_t *korg1212)
{
        if (korg1212->idleMonitorOn) {
                writel(0xffffffff, &korg1212->sharedBufferPtr->cardCommand);
                snd_korg1212_WaitForCardStopAck(korg1212);
                korg1212->idleMonitorOn = 0;
        }
}

static void snd_korg1212_setCardState(korg1212_t * korg1212, CardState csState)
{
        switch (csState) {
                case K1212_STATE_READY:
                        snd_korg1212_TurnOnIdleMonitor(korg1212);
                        break;

                case K1212_STATE_OPEN:
                        snd_korg1212_TurnOffIdleMonitor(korg1212);
                        break;

                default:
                        break;
        }

        korg1212->cardState = csState;
}

static int snd_korg1212_OpenCard(korg1212_t * korg1212)
{
#ifdef DEBUG
	PRINTK("DEBUG: OpenCard [%s]\n", stateName[korg1212->cardState]);
#endif
        snd_korg1212_setCardState(korg1212, K1212_STATE_OPEN);
        return 1;
}

static int snd_korg1212_CloseCard(korg1212_t * korg1212)
{
#ifdef DEBUG
	PRINTK("DEBUG: CloseCard [%s]\n", stateName[korg1212->cardState]);
#endif

        if (korg1212->cardState == K1212_STATE_SETUP) {
                rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SelectPlayMode,
                                K1212_MODE_StopPlay, 0, 0, 0);
#ifdef DEBUG
	if (rc) PRINTK("DEBUG: CloseCard - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

                if (rc != K1212_CMDRET_Success)
                        return 0;
        } else if (korg1212->cardState > K1212_STATE_SETUP) {
                writel(0xffffffff, &korg1212->sharedBufferPtr->cardCommand);
                snd_korg1212_WaitForCardStopAck(korg1212);
        }

        if (korg1212->cardState > K1212_STATE_READY)
                snd_korg1212_setCardState(korg1212, K1212_STATE_READY);

        return 0;
}

static int snd_korg1212_SetupForPlay(korg1212_t * korg1212)
{
#ifdef DEBUG
	PRINTK("DEBUG: SetupForPlay [%s]\n", stateName[korg1212->cardState]);
#endif

        snd_korg1212_setCardState(korg1212, K1212_STATE_SETUP);
        rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SelectPlayMode,
                                        K1212_MODE_SetupPlay, 0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: SetupForPlay - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif
        if (rc != K1212_CMDRET_Success) {
                return 0;
        }
        return 1;
}

static int snd_korg1212_TriggerPlay(korg1212_t * korg1212)
{
#ifdef DEBUG
	PRINTK("DEBUG: TriggerPlay [%s]\n", stateName[korg1212->cardState]);
#endif

        snd_korg1212_setCardState(korg1212, K1212_STATE_PLAYING);
        rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_TriggerPlay, 0, 0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: TriggerPlay - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        if (rc != K1212_CMDRET_Success) {
                return 0;
        }
        return 1;
}

static int snd_korg1212_StopPlay(korg1212_t * korg1212)
{
#ifdef DEBUG
	PRINTK("DEBUG: StopPlay [%s]\n", stateName[korg1212->cardState]);
#endif

        if (korg1212->cardState != K1212_STATE_ERRORSTOP) {
                writel(0xffffffff, &korg1212->sharedBufferPtr->cardCommand);
                snd_korg1212_WaitForCardStopAck(korg1212);
        }
        snd_korg1212_setCardState(korg1212, K1212_STATE_OPEN);
        return 1;
}

static void snd_korg1212_EnableCardInterrupts(korg1212_t * korg1212)
{
	* korg1212->statusRegPtr = PCI_INT_ENABLE_BIT            |
                                   PCI_DOORBELL_INT_ENABLE_BIT   |
                                   LOCAL_INT_ENABLE_BIT          |
                                   LOCAL_DOORBELL_INT_ENABLE_BIT |
                                   LOCAL_DMA1_INT_ENABLE_BIT;
}

#if 0 /* not used */

static int snd_korg1212_SetMonitorMode(korg1212_t *korg1212, MonitorModeSelector mode)
{
#ifdef DEBUG
	PRINTK("DEBUG: SetMonitorMode [%s]\n", stateName[korg1212->cardState]);
#endif

        switch (mode) {
                case K1212_MONMODE_Off:
                        if (korg1212->cardState != K1212_STATE_MONITOR) {
                                return 0;
                        } else {
                                writel(0xffffffff, &korg1212->sharedBufferPtr->cardCommand);
                                snd_korg1212_WaitForCardStopAck(korg1212);
                                snd_korg1212_setCardState(korg1212, K1212_STATE_OPEN);
                        }
                        break;

                case K1212_MONMODE_On:
                        if (korg1212->cardState != K1212_STATE_OPEN) {
                                return 0;
                        } else {
                                snd_korg1212_setCardState(korg1212, K1212_STATE_MONITOR);
                                rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SelectPlayMode,
                                                        K1212_MODE_MonitorOn, 0, 0, 0);
                                if (rc != K1212_CMDRET_Success) {
                                        return 0;
                                }
                        }
                        break;

                default:
                        return 0;
        }

        return 1;
}

#endif /* not used */

static int snd_korg1212_SetRate(korg1212_t *korg1212, int rate)
{
        static ClockSourceIndex s44[] = { K1212_CLKIDX_AdatAt44_1K,
                                          K1212_CLKIDX_WordAt44_1K,
                                          K1212_CLKIDX_LocalAt44_1K };
        static ClockSourceIndex s48[] = {
                                          K1212_CLKIDX_AdatAt48K,
                                          K1212_CLKIDX_WordAt48K,
                                          K1212_CLKIDX_LocalAt48K };
        int parm;

        switch(rate) {
                case 44100:
                parm = s44[korg1212->clkSource];
                break;

                case 48000:
                parm = s48[korg1212->clkSource];
                break;

                default:
                return -EINVAL;
        }

        korg1212->clkSrcRate = parm;
        korg1212->clkRate = rate;

	TickDelay(INTERCOMMAND_DELAY);
	rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SetClockSourceRate,
					  ClockSourceSelector[korg1212->clkSrcRate],
					  0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Set Clock Source Selector - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        return 0;
}

static int snd_korg1212_SetClockSource(korg1212_t *korg1212, int source)
{

        if (source<0 || source >2)
           return -EINVAL;

        korg1212->clkSource = source;

        snd_korg1212_SetRate(korg1212, korg1212->clkRate);

        return 0;
}

static void snd_korg1212_DisableCardInterrupts(korg1212_t *korg1212)
{
	* korg1212->statusRegPtr = 0;
}

static int snd_korg1212_WriteADCSensitivity(korg1212_t *korg1212)
{
        SensBits  sensVals;
        int       bitPosition;
        int       channel;
        int       clkIs48K;
        int       monModeSet;
        u16       controlValue;    // this keeps the current value to be written to
                                   //  the card's eeprom control register.
        u16       count;

#ifdef DEBUG
	PRINTK("DEBUG: WriteADCSensivity [%s]\n", stateName[korg1212->cardState]);
#endif

        // ----------------------------------------------------------------------------
        // initialize things.  The local init bit is always set when writing to the
        // card's control register.
        // ----------------------------------------------------------------------------
        controlValue = 0;
        SetBitInWord(&controlValue, SET_SENS_LOCALINIT_BITPOS);    // init the control value

        // ----------------------------------------------------------------------------
        // make sure the card is not in monitor mode when we do this update.
        // ----------------------------------------------------------------------------
        if (korg1212->cardState == K1212_STATE_MONITOR || korg1212->idleMonitorOn) {
                writel(0xffffffff, &korg1212->sharedBufferPtr->cardCommand);
                monModeSet = 1;
                snd_korg1212_WaitForCardStopAck(korg1212);
        } else
                monModeSet = 0;

        // ----------------------------------------------------------------------------
        // we are about to send new values to the card, so clear the new values queued
        // flag.  Also, clear out mailbox 3, so we don't lockup.
        // ----------------------------------------------------------------------------
        writel(0, korg1212->mailbox3Ptr);
        TickDelay(LOADSHIFT_DELAY);

        // ----------------------------------------------------------------------------
        // determine whether we are running a 48K or 44.1K clock.  This info is used
        // later when setting the SPDIF FF after the volume has been shifted in.
        // ----------------------------------------------------------------------------
        switch (korg1212->clkSrcRate) {
                case K1212_CLKIDX_AdatAt44_1K:
                case K1212_CLKIDX_WordAt44_1K:
                case K1212_CLKIDX_LocalAt44_1K:
                        clkIs48K = 0;
                        break;

                case K1212_CLKIDX_WordAt48K:
                case K1212_CLKIDX_AdatAt48K:
                case K1212_CLKIDX_LocalAt48K:
                default:
                        clkIs48K = 1;
                        break;
        }

        // ----------------------------------------------------------------------------
        // start the update.  Setup the bit structure and then shift the bits.
        // ----------------------------------------------------------------------------
        sensVals.l.v.leftChanId   = SET_SENS_LEFTCHANID;
        sensVals.r.v.rightChanId  = SET_SENS_RIGHTCHANID;
        sensVals.l.v.leftChanVal  = korg1212->leftADCInSens;
        sensVals.r.v.rightChanVal = korg1212->rightADCInSens;

        // ----------------------------------------------------------------------------
        // now start shifting the bits in.  Start with the left channel then the right.
        // ----------------------------------------------------------------------------
        for (channel = 0; channel < 2; channel++) {

                // ----------------------------------------------------------------------------
                // Bring the load/shift line low, then wait - the spec says >150ns from load/
                // shift low to the first rising edge of the clock.
                // ----------------------------------------------------------------------------
                ClearBitInWord(&controlValue, SET_SENS_LOADSHIFT_BITPOS);
                ClearBitInWord(&controlValue, SET_SENS_DATA_BITPOS);
                writew(controlValue, korg1212->sensRegPtr);                          // load/shift goes low
                TickDelay(LOADSHIFT_DELAY);

                for (bitPosition = 15; bitPosition >= 0; bitPosition--) {       // for all the bits
                        if (channel == 0) {
                                if (sensVals.l.leftSensBits & (0x0001 << bitPosition)) {
                                        SetBitInWord(&controlValue, SET_SENS_DATA_BITPOS);     // data bit set high
                                } else {
                                        ClearBitInWord(&controlValue, SET_SENS_DATA_BITPOS);   // data bit set low
                                }
                        } else {
                                if (sensVals.r.rightSensBits & (0x0001 << bitPosition)) {
                                SetBitInWord(&controlValue, SET_SENS_DATA_BITPOS);     // data bit set high
                                } else {
                                ClearBitInWord(&controlValue, SET_SENS_DATA_BITPOS);   // data bit set low
                                }
                        }

                        ClearBitInWord(&controlValue, SET_SENS_CLOCK_BITPOS);
                        writew(controlValue, korg1212->sensRegPtr);                       // clock goes low
                        TickDelay(SENSCLKPULSE_WIDTH);
                        SetBitInWord(&controlValue, SET_SENS_CLOCK_BITPOS);
                        writew(controlValue, korg1212->sensRegPtr);                       // clock goes high
                        TickDelay(SENSCLKPULSE_WIDTH);
                }

                // ----------------------------------------------------------------------------
                // finish up SPDIF for left.  Bring the load/shift line high, then write a one
                // bit if the clock rate is 48K otherwise write 0.
                // ----------------------------------------------------------------------------
                ClearBitInWord(&controlValue, SET_SENS_DATA_BITPOS);
                ClearBitInWord(&controlValue, SET_SENS_CLOCK_BITPOS);
                SetBitInWord(&controlValue, SET_SENS_LOADSHIFT_BITPOS);
                writew(controlValue, korg1212->sensRegPtr);                   // load shift goes high - clk low
                TickDelay(SENSCLKPULSE_WIDTH);

                if (clkIs48K)
                        SetBitInWord(&controlValue, SET_SENS_DATA_BITPOS);

                writew(controlValue, korg1212->sensRegPtr);                   // set/clear data bit
                TickDelay(ONE_RTC_TICK);
                SetBitInWord(&controlValue, SET_SENS_CLOCK_BITPOS);
                writew(controlValue, korg1212->sensRegPtr);                   // clock goes high
                TickDelay(SENSCLKPULSE_WIDTH);
                ClearBitInWord(&controlValue, SET_SENS_CLOCK_BITPOS);
                writew(controlValue, korg1212->sensRegPtr);                   // clock goes low
                TickDelay(SENSCLKPULSE_WIDTH);
        }

        // ----------------------------------------------------------------------------
        // The update is complete.  Set a timeout.  This is the inter-update delay.
        // Also, if the card was in monitor mode, restore it.
        // ----------------------------------------------------------------------------
        for (count = 0; count < 10; count++)
                TickDelay(SENSCLKPULSE_WIDTH);

        if (monModeSet) {
                rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SelectPlayMode,
                                K1212_MODE_MonitorOn, 0, 0, 0);
#ifdef DEBUG
	        if (rc) PRINTK("DEBUG: WriteADCSensivity - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        }

        return 1;
}

static void snd_korg1212_OnDSPDownloadComplete(korg1212_t *korg1212)
{
        int channel;

#ifdef DEBUG
        PRINTK("DEBUG: DSP download is complete. [%s]\n", stateName[korg1212->cardState]);
#endif

        // ----------------------------------------------------
        // tell the card to boot
        // ----------------------------------------------------
        rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_BootFromDSPPage4, 0, 0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Boot from Page 4 - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif
	mdelay(DSP_BOOT_DELAY_IN_MS);

        // --------------------------------------------------------------------------------
        // Let the card know where all the buffers are.
        // --------------------------------------------------------------------------------
        rc = snd_korg1212_Send1212Command(korg1212,
                        K1212_DB_ConfigureBufferMemory,
                        LowerWordSwap(korg1212->PlayDataPhy),
                        LowerWordSwap(korg1212->RecDataPhy),
                        ((kNumBuffers * kPlayBufferFrames) / 2),   // size given to the card
                                                                   // is based on 2 buffers
                        0
        );

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Configure Buffer Memory - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        TickDelay(INTERCOMMAND_DELAY);

        rc = snd_korg1212_Send1212Command(korg1212,
                        K1212_DB_ConfigureMiscMemory,
                        LowerWordSwap(korg1212->VolumeTablePhy),
                        LowerWordSwap(korg1212->RoutingTablePhy),
                        LowerWordSwap(korg1212->AdatTimeCodePhy),
                        0
        );

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Configure Misc Memory - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif


        // --------------------------------------------------------------------------------
        // Initialize the routing and volume tables, then update the card's state.
        // --------------------------------------------------------------------------------
        TickDelay(INTERCOMMAND_DELAY);

        for (channel = 0; channel < kAudioChannels; channel++) {
                korg1212->sharedBufferPtr->volumeData[channel] = k1212MaxVolume;
                //korg1212->sharedBufferPtr->routeData[channel] = channel;
                korg1212->sharedBufferPtr->routeData[channel] = 8 + (channel & 1);
        }

        snd_korg1212_WriteADCSensitivity(korg1212);

	TickDelay(INTERCOMMAND_DELAY);
	rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_SetClockSourceRate,
					  ClockSourceSelector[korg1212->clkSrcRate],
					  0, 0, 0);
#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Set Clock Source Selector - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

	snd_korg1212_setCardState(korg1212, K1212_STATE_READY);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Set Monitor On - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        wake_up_interruptible(&korg1212->wait);
}


static void snd_korg1212_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        u32 doorbellValue;
        korg1212_t *korg1212 = snd_magic_cast(korg1212_t, dev_id, return);

	if(irq != korg1212->irq)
		return;

        doorbellValue = readl(korg1212->inDoorbellPtr);

        if (!doorbellValue)
		return;

	writel(doorbellValue, korg1212->inDoorbellPtr);

        korg1212->irqcount++;

	korg1212->inIRQ++;


        switch (doorbellValue) {
                case K1212_DB_DSPDownloadDone:
#ifdef DEBUG
                        PRINTK("DEBUG: IRQ DNLD count - %ld, %x, [%s].\n", korg1212->irqcount, doorbellValue, stateName[korg1212->cardState]);
#endif
                        if (korg1212->cardState == K1212_STATE_DSP_IN_PROCESS) {
                                        snd_korg1212_setCardState(korg1212, K1212_STATE_DSP_COMPLETE);
                                        snd_korg1212_OnDSPDownloadComplete(korg1212);
                        }
                        break;

                // ------------------------------------------------------------------------
                // an error occurred - stop the card
                // ------------------------------------------------------------------------
                case K1212_ISRCODE_DMAERROR:
#ifdef DEBUG
                        PRINTK("DEBUG: IRQ DMAE count - %ld, %x, [%s].\n", korg1212->irqcount, doorbellValue, stateName[korg1212->cardState]);
#endif
                        writel(0, &korg1212->sharedBufferPtr->cardCommand);
                        break;

                // ------------------------------------------------------------------------
                // the card has stopped by our request.  Clear the command word and signal
                // the semaphore in case someone is waiting for this.
                // ------------------------------------------------------------------------
                case K1212_ISRCODE_CARDSTOPPED:
#ifdef DEBUG
                        PRINTK("DEBUG: IRQ CSTP count - %ld, %x, [%s].\n", korg1212->irqcount, doorbellValue, stateName[korg1212->cardState]);
#endif
                        writel(0, &korg1212->sharedBufferPtr->cardCommand);
                        break;

                default:
#ifdef XDEBUG
                        PRINTK("DEBUG: IRQ DFLT count - %ld, %x, cpos=%d [%s].\n", korg1212->irqcount, doorbellValue, 
				korg1212->currentBuffer, stateName[korg1212->cardState]);
#endif
                        if ((korg1212->cardState > K1212_STATE_SETUP) || korg1212->idleMonitorOn) {
                                korg1212->currentBuffer++;

                                if (korg1212->currentBuffer >= kNumBuffers)
                                        korg1212->currentBuffer = 0;

                                if (!korg1212->running)
                                        break;

                                if (korg1212->capture_substream) {
                                        snd_pcm_period_elapsed(korg1212->capture_substream);
                                }

                                if (korg1212->playback_substream) {
                                        snd_pcm_period_elapsed(korg1212->playback_substream);
                                }
                        }
                        break;
        }

	korg1212->inIRQ--;
}

static int snd_korg1212_downloadDSPCode(korg1212_t *korg1212)
{

#ifdef DEBUG
        PRINTK("DEBUG: DSP download is starting... [%s]\n", stateName[korg1212->cardState]);
#endif

        // ---------------------------------------------------------------
        // verify the state of the card before proceeding.
        // ---------------------------------------------------------------
        if (korg1212->cardState >= K1212_STATE_DSP_IN_PROCESS) {
                return 1;
        }

        snd_korg1212_setCardState(korg1212, K1212_STATE_DSP_IN_PROCESS);

        memcpy(korg1212->dspMemPtr, dspCode, korg1212->dspCodeSize);

        rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_StartDSPDownload,
                                     UpperWordSwap(korg1212->dspMemPhy),
                                     0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Start DSP Download RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

        interruptible_sleep_on_timeout(&korg1212->wait, HZ * 4);

        return 0;
}

static snd_pcm_hardware_t snd_korg1212_playback_info =
{
        info:                (SNDRV_PCM_INFO_MMAP |
                              SNDRV_PCM_INFO_MMAP_VALID |
                              SNDRV_PCM_INFO_INTERLEAVED),
        formats:             SNDRV_PCM_FMTBIT_S16_LE,
        rates:               (SNDRV_PCM_RATE_44100 |
                              SNDRV_PCM_RATE_48000),
        rate_min:            44100,
        rate_max:            48000,
        channels_min:        K1212_CHANNELS,
        channels_max:        K1212_CHANNELS,
        buffer_bytes_max:    K1212_BUF_SIZE,
        period_bytes_min:    K1212_PERIOD_BYTES,
        period_bytes_max:    K1212_PERIOD_BYTES,
        periods_min:         K1212_PERIODS,
        periods_max:         K1212_PERIODS,
        fifo_size:           0,
};

static snd_pcm_hardware_t snd_korg1212_capture_info =
{
        info:                (SNDRV_PCM_INFO_MMAP |
                              SNDRV_PCM_INFO_MMAP_VALID |
                              SNDRV_PCM_INFO_INTERLEAVED),
        formats:             SNDRV_PCM_FMTBIT_S16_LE,
        rates:               (SNDRV_PCM_RATE_44100 |
                              SNDRV_PCM_RATE_48000),
        rate_min:            44100,
        rate_max:            48000,
        channels_min:        K1212_CHANNELS,
        channels_max:        K1212_CHANNELS,
        buffer_bytes_max:    K1212_BUF_SIZE,
        period_bytes_min:    K1212_PERIOD_BYTES,
        period_bytes_max:    K1212_PERIOD_BYTES,
        periods_min:         K1212_PERIODS,
        periods_max:         K1212_PERIODS,
        fifo_size:           0,
};

static void snd_korg1212_free_pcm(snd_pcm_t *pcm)
{
        korg1212_t *korg1212 = (korg1212_t *) pcm->private_data;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_free_pcm [%s]\n", stateName[korg1212->cardState]);
#endif

        korg1212->pcm16 = NULL;
}

static unsigned int period_bytes[] = { K1212_PERIOD_BYTES };

#define PERIOD_BYTES sizeof(period_bytes) / sizeof(period_bytes[0])

static snd_pcm_hw_constraint_list_t hw_constraints_period_bytes = {
        count: PERIOD_BYTES,
        list: period_bytes,
        mask: 0
};

static int snd_korg1212_playback_open(snd_pcm_substream_t *substream)
{
        unsigned long flags;
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
        snd_pcm_runtime_t *runtime = substream->runtime;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_playback_open [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);

        snd_korg1212_OpenCard(korg1212);

        snd_pcm_set_sync(substream);    // ???

        runtime->hw = snd_korg1212_playback_info;
	runtime->dma_area = (char *) korg1212->playDataBufsPtr;
	runtime->dma_bytes = K1212_BUF_SIZE;

        korg1212->playback_substream = substream;
        korg1212->periodsize = K1212_PERIODS;

        spin_unlock_irqrestore(&korg1212->lock, flags);

        snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, K1212_BUF_SIZE, K1212_BUF_SIZE);
        snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);
        return 0;
}

static int snd_korg1212_capture_open(snd_pcm_substream_t *substream)
{
        unsigned long flags;
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
        snd_pcm_runtime_t *runtime = substream->runtime;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_capture_open [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);

        snd_korg1212_OpenCard(korg1212);

        snd_pcm_set_sync(substream);    // ???

        runtime->hw = snd_korg1212_capture_info;
	runtime->dma_area = (char *) korg1212->recordDataBufsPtr;
	runtime->dma_bytes = K1212_BUF_SIZE;

        korg1212->capture_substream = substream;
        korg1212->periodsize = K1212_PERIODS;

        spin_unlock_irqrestore(&korg1212->lock, flags);

        snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, K1212_BUF_SIZE, K1212_BUF_SIZE);
        snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_bytes);
        return 0;
}

static int snd_korg1212_playback_close(snd_pcm_substream_t *substream)
{
        unsigned long flags;
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_playback_close [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);

        korg1212->playback_substream = NULL;
        korg1212->periodsize = 0;

        snd_korg1212_CloseCard(korg1212);

        spin_unlock_irqrestore(&korg1212->lock, flags);
        return 0;
}

static int snd_korg1212_capture_close(snd_pcm_substream_t *substream)
{
        unsigned long flags;
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_capture_close [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);

        korg1212->capture_substream = NULL;
        korg1212->periodsize = 0;

        snd_korg1212_CloseCard(korg1212);

        spin_unlock_irqrestore(&korg1212->lock, flags);
        return 0;
}

static int snd_korg1212_channel_info(snd_pcm_substream_t *substream,
				    snd_pcm_channel_info_t *info)
{
	int chn = info->channel;

	// snd_assert(info->channel < kAudioChannels + 1, return -EINVAL);

        info->offset = 0;
        // if (chn < k16BitChannels) {
                info->first = chn * 16;
        // } else {
        //         info->first = k16BitChannels * 16 + (chn - k16BitChannels - 1) * 32;
        // }
        info->step = sizeof(KorgAudioFrame) * 8;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_channel_info %d:, offset=%ld, first=%d, step=%d\n", chn, info->offset, info->first, info->step);
#endif

	return 0;
}

static int snd_korg1212_ioctl(snd_pcm_substream_t *substream,
			     unsigned int cmd, void *arg)
{
#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_ioctl: cmd=%d\n", cmd);
#endif
	if (cmd == SNDRV_PCM_IOCTL1_CHANNEL_INFO ) {
		snd_pcm_channel_info_t *info = arg;
		return snd_korg1212_channel_info(substream, info);
	}

        return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_korg1212_hw_params(snd_pcm_substream_t *substream,
                             snd_pcm_hw_params_t *params)
{
        unsigned long flags;
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
        int err;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_hw_params [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);
        if ((err = snd_korg1212_SetRate(korg1212, params_rate(params))) < 0) {
                spin_unlock_irqrestore(&korg1212->lock, flags);
                return err;
        }

        if (params_format(params) != SNDRV_PCM_FORMAT_S16_LE) {
                spin_unlock_irqrestore(&korg1212->lock, flags);
                return -EINVAL;
        }

        korg1212->periodsize = K1212_BLOCK_SIZE;

        spin_unlock_irqrestore(&korg1212->lock, flags);

        return 0;
}

static int snd_korg1212_prepare(snd_pcm_substream_t *substream)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
        unsigned long flags;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_prepare [%s]\n", stateName[korg1212->cardState]);
#endif

        spin_lock_irqsave(&korg1212->lock, flags);

        snd_korg1212_SetupForPlay(korg1212);

        korg1212->currentBuffer = -1;

        spin_unlock_irqrestore(&korg1212->lock, flags);
        return 0;
}

static int snd_korg1212_trigger(snd_pcm_substream_t *substream,
                           int cmd)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_trigger [%s] cmd=%d\n", stateName[korg1212->cardState], cmd);
#endif

        switch (cmd) {
                case SNDRV_PCM_TRIGGER_START:
                        korg1212->running = 1;
                        snd_korg1212_TriggerPlay(korg1212);
                        break;

                case SNDRV_PCM_TRIGGER_STOP:
                        korg1212->running = 0;
                        snd_korg1212_StopPlay(korg1212);
                        break;

                default:
                        return -EINVAL;
        }
        return 0;
}

static snd_pcm_uframes_t snd_korg1212_pointer(snd_pcm_substream_t *substream)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
        snd_pcm_uframes_t pos;

	if (korg1212->currentBuffer < 0)
		return 0;

	pos = korg1212->currentBuffer * kPlayBufferFrames;

#ifdef XDEBUG
		PRINTK("DEBUG: snd_korg1212_pointer [%s] %ld\n", stateName[korg1212->cardState], pos);
#endif

        return pos;
}

static int snd_korg1212_playback_copy(snd_pcm_substream_t *substream,
                        int channel, /* not used (interleaved data) */
                        snd_pcm_uframes_t pos,
                        void *src,
                        snd_pcm_uframes_t count)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
	KorgAudioFrame * dst = korg1212->playDataBufsPtr[0].bufferData + pos;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_playback_copy [%s] %ld %ld\n", stateName[korg1212->cardState], pos, count);
#endif
 
	snd_assert(pos + count <= K1212_MAX_SAMPLES, return -EINVAL);

        copy_from_user(dst, src, count * K1212_FRAME_SIZE);

        return 0;
}

static int snd_korg1212_capture_copy(snd_pcm_substream_t *substream,
                        int channel, /* not used (interleaved data) */
                        snd_pcm_uframes_t pos,
                        void *dst,
                        snd_pcm_uframes_t count)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
	KorgAudioFrame * src = korg1212->recordDataBufsPtr[0].bufferData + pos;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_capture_copy [%s] %ld %ld\n", stateName[korg1212->cardState], pos, count);
#endif

	snd_assert(pos + count <= K1212_MAX_SAMPLES, return -EINVAL);

        copy_to_user(dst, src, count * K1212_FRAME_SIZE);

        return 0;
}

static int snd_korg1212_playback_silence(snd_pcm_substream_t *substream,
                           int channel, /* not used (interleaved data) */
                           snd_pcm_uframes_t pos,
                           snd_pcm_uframes_t count)
{
        korg1212_t *korg1212 = _snd_pcm_substream_chip(substream);
	KorgAudioFrame * dst = korg1212->playDataBufsPtr[0].bufferData + pos;

#ifdef DEBUG
		PRINTK("DEBUG: snd_korg1212_playback_silence [%s]\n", stateName[korg1212->cardState]);
#endif

	snd_assert(pos + count <= K1212_MAX_SAMPLES, return -EINVAL);

        memset(dst, 0, count * K1212_FRAME_SIZE);

        return 0;
}

static snd_pcm_ops_t snd_korg1212_playback_ops = {
        open:           snd_korg1212_playback_open,
        close:          snd_korg1212_playback_close,
        ioctl:          snd_korg1212_ioctl,
        hw_params:      snd_korg1212_hw_params,
        prepare:        snd_korg1212_prepare,
        trigger:        snd_korg1212_trigger,
        pointer:        snd_korg1212_pointer,
        copy:           snd_korg1212_playback_copy,
        silence:        snd_korg1212_playback_silence,
};

static snd_pcm_ops_t snd_korg1212_capture_ops = {
	open:		snd_korg1212_capture_open,
	close:		snd_korg1212_capture_close,
	ioctl:		snd_korg1212_ioctl,
	hw_params:	snd_korg1212_hw_params,
	prepare:	snd_korg1212_prepare,
	trigger:	snd_korg1212_trigger,
	pointer:	snd_korg1212_pointer,
	copy:		snd_korg1212_capture_copy,
};

/*
 * Control Interface
 */

static int snd_korg1212_control_phase_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = (kcontrol->private_value >= 8) ? 2 : 1;
	return 0;
}

static int snd_korg1212_control_phase_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int i = kcontrol->private_value;

	spin_lock_irqsave(&korg1212->lock, flags);

        u->value.integer.value[0] = korg1212->volumePhase[i];

	if (i >= 8) 
        	u->value.integer.value[1] = korg1212->volumePhase[i+1];

        spin_unlock_irqrestore(&korg1212->lock, flags);

        return 0;
}

static int snd_korg1212_control_phase_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int change = 0;
        int i, val;

	spin_lock_irqsave(&korg1212->lock, flags);

	i = kcontrol->private_value;

	korg1212->volumePhase[i] = u->value.integer.value[0];

	val = korg1212->sharedBufferPtr->volumeData[kcontrol->private_value];

	if ((u->value.integer.value[0] > 0) != (val < 0)) {
		val = abs(val) * (korg1212->volumePhase[i] > 0 ? -1 : 1);
		korg1212->sharedBufferPtr->volumeData[i] = val;
		change = 1;
	}

	if (i >= 8) {
		korg1212->volumePhase[i+1] = u->value.integer.value[1];

		val = korg1212->sharedBufferPtr->volumeData[kcontrol->private_value+1];

		if ((u->value.integer.value[1] > 0) != (val < 0)) {
			val = abs(val) * (korg1212->volumePhase[i+1] > 0 ? -1 : 1);
			korg1212->sharedBufferPtr->volumeData[i+1] = val;
			change = 1;
		}
	}

	spin_unlock_irqrestore(&korg1212->lock, flags);

        return change;
}

static int snd_korg1212_control_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = (kcontrol->private_value >= 8) ? 2 : 1;
        uinfo->value.integer.min = k1212MinVolume;
	uinfo->value.integer.max = k1212MaxVolume;
        return 0;
}

static int snd_korg1212_control_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int i;

	spin_lock_irqsave(&korg1212->lock, flags);

	i = kcontrol->private_value;
        u->value.integer.value[0] = abs(korg1212->sharedBufferPtr->volumeData[i]);

	if (i >= 8) 
                u->value.integer.value[1] = abs(korg1212->sharedBufferPtr->volumeData[i+1]);

        spin_unlock_irqrestore(&korg1212->lock, flags);

        return 0;
}

static int snd_korg1212_control_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int change = 0;
        int i;
	int val;

	spin_lock_irqsave(&korg1212->lock, flags);

	i = kcontrol->private_value;

	if (u->value.integer.value[0] != abs(korg1212->sharedBufferPtr->volumeData[i])) {
		val = korg1212->volumePhase[i] > 0 ? -1 : 1;
		val *= u->value.integer.value[0];
		korg1212->sharedBufferPtr->volumeData[i] = val;
		change = 1;
	}

	if (i >= 8) {
		if (u->value.integer.value[1] != abs(korg1212->sharedBufferPtr->volumeData[i+1])) {
			val = korg1212->volumePhase[i+1] > 0 ? -1 : 1;
			val *= u->value.integer.value[1];
			korg1212->sharedBufferPtr->volumeData[i+1] = val;
			change = 1;
		}
	}

	spin_unlock_irqrestore(&korg1212->lock, flags);

        return change;
}

static int snd_korg1212_control_route_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = (kcontrol->private_value >= 8) ? 2 : 1;
	uinfo->value.enumerated.items = kAudioChannels;
	if (uinfo->value.enumerated.item > kAudioChannels-1) {
		uinfo->value.enumerated.item = kAudioChannels-1;
	}
	strcpy(uinfo->value.enumerated.name, channelName[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_korg1212_control_route_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int i;

	spin_lock_irqsave(&korg1212->lock, flags);

	i = kcontrol->private_value;
	u->value.enumerated.item[0] = korg1212->sharedBufferPtr->routeData[i];

	if (i >= 8) 
		u->value.enumerated.item[1] = korg1212->sharedBufferPtr->routeData[i+1];

        spin_unlock_irqrestore(&korg1212->lock, flags);

        return 0;
}

static int snd_korg1212_control_route_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int change = 0, i;

	spin_lock_irqsave(&korg1212->lock, flags);

	i = kcontrol->private_value;

	if (u->value.enumerated.item[0] != korg1212->sharedBufferPtr->volumeData[i]) {
		korg1212->sharedBufferPtr->routeData[i] = u->value.enumerated.item[0];
		change = 1;
	}

	if (i >= 8) {
		if (u->value.enumerated.item[1] != korg1212->sharedBufferPtr->volumeData[i+1]) {
			korg1212->sharedBufferPtr->routeData[i+1] = u->value.enumerated.item[1];
			change = 1;
		}
	}

	spin_unlock_irqrestore(&korg1212->lock, flags);

        return change;
}

static int snd_korg1212_control_analog_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = k1212MaxADCSens;
	uinfo->value.integer.max = k1212MinADCSens;
        return 0;
}

static int snd_korg1212_control_analog_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&korg1212->lock, flags);

        u->value.integer.value[0] = korg1212->leftADCInSens;
        u->value.integer.value[1] = korg1212->rightADCInSens;

        spin_unlock_irqrestore(&korg1212->lock, flags);

        return 0;
}

static int snd_korg1212_control_analog_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *u)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
        int change = 0;

	spin_lock_irqsave(&korg1212->lock, flags);

        if (u->value.integer.value[0] != korg1212->leftADCInSens) {
                korg1212->leftADCInSens = u->value.integer.value[0];
                change = 1;
        }
        if (u->value.integer.value[1] != korg1212->rightADCInSens) {
                korg1212->rightADCInSens = u->value.integer.value[1];
                change = 1;
        }

        if (change)
                snd_korg1212_WriteADCSensitivity(korg1212);

	spin_unlock_irqrestore(&korg1212->lock, flags);

        return change;
}

static int snd_korg1212_control_sync_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2) {
		uinfo->value.enumerated.item = 2;
	}
	strcpy(uinfo->value.enumerated.name, clockSourceTypeName[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_korg1212_control_sync_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&korg1212->lock, flags);

	ucontrol->value.enumerated.item[0] = korg1212->clkSource;

	spin_unlock_irqrestore(&korg1212->lock, flags);
	return 0;
}

static int snd_korg1212_control_sync_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	korg1212_t *korg1212 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irqsave(&korg1212->lock, flags);
	change = val != korg1212->clkSource;
        snd_korg1212_SetClockSource(korg1212, val);
	spin_unlock_irqrestore(&korg1212->lock, flags);
	return change;
}

#define MON_MIXER(ord,c_name)									\
        {											\
                access:		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE,	\
                iface:          SNDRV_CTL_ELEM_IFACE_MIXER,					\
                name:		c_name " Monitor Volume",						\
                info:		snd_korg1212_control_volume_info,				\
                get:		snd_korg1212_control_volume_get,				\
                put:		snd_korg1212_control_volume_put,				\
		private_value:	ord,								\
        },                                                                                      \
        {											\
                access:		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE,	\
                iface:          SNDRV_CTL_ELEM_IFACE_MIXER,					\
                name:		c_name " Monitor Route",						\
                info:		snd_korg1212_control_route_info,				\
                get:		snd_korg1212_control_route_get,					\
                put:		snd_korg1212_control_route_put,					\
		private_value:	ord,								\
        },                                                                                      \
        {											\
                access:		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE,	\
                iface:          SNDRV_CTL_ELEM_IFACE_PCM,					\
                name:		c_name " Monitor Phase Invert",						\
                info:		snd_korg1212_control_phase_info,				\
                get:		snd_korg1212_control_phase_get,					\
                put:		snd_korg1212_control_phase_put,					\
		private_value:	ord,								\
        }

static snd_kcontrol_new_t snd_korg1212_controls[] = {
        MON_MIXER(8, "Analog"),
	MON_MIXER(10, "SPDIF"), 
        MON_MIXER(0, "ADAT-1"), MON_MIXER(1, "ADAT-2"), MON_MIXER(2, "ADAT-3"), MON_MIXER(3, "ADAT-4"),
        MON_MIXER(4, "ADAT-5"), MON_MIXER(5, "ADAT-6"), MON_MIXER(6, "ADAT-7"), MON_MIXER(7, "ADAT-8"),
	{
                access:		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE,
                iface:          SNDRV_CTL_ELEM_IFACE_PCM,
                name:		"Sync Source",
                info:		snd_korg1212_control_sync_info,
                get:		snd_korg1212_control_sync_get,
                put:		snd_korg1212_control_sync_put,
        },
        {
                access:		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE,
                iface:          SNDRV_CTL_ELEM_IFACE_MIXER,
                name:		"ADC Attenuation",
                info:		snd_korg1212_control_analog_info,
                get:		snd_korg1212_control_analog_get,
                put:		snd_korg1212_control_analog_put,
        }
};

#define K1212_CONTROL_ELEMENTS (sizeof(snd_korg1212_controls) / sizeof(snd_korg1212_controls[0]))

/*
 * proc interface
 */

static void snd_korg1212_proc_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	int n;
	korg1212_t *korg1212 = (korg1212_t *)entry->private_data;

	snd_iprintf(buffer, korg1212->card->longname);
	snd_iprintf(buffer, " (index #%d)\n", korg1212->card->number + 1);
	snd_iprintf(buffer, "\nGeneral settings\n");
	snd_iprintf(buffer, "    period size: %d bytes\n", K1212_BLOCK_SIZE);
	snd_iprintf(buffer, "     clock mode: %s\n", clockSourceName[korg1212->clkSrcRate] );
	snd_iprintf(buffer, "  left ADC Sens: %d\n", korg1212->leftADCInSens );
	snd_iprintf(buffer, " right ADC Sens: %d\n", korg1212->rightADCInSens );
        snd_iprintf(buffer, "    Volume Info:\n");
        for (n=0; n<kAudioChannels; n++)
                snd_iprintf(buffer, " Channel %d: %s -> %s [%d]\n", n,
                                    channelName[n],
                                    channelName[korg1212->sharedBufferPtr->routeData[n]],
                                    korg1212->sharedBufferPtr->volumeData[n]);
	snd_iprintf(buffer, "\nGeneral status\n");
        snd_iprintf(buffer, " ADAT Time Code: %d\n", korg1212->sharedBufferPtr->AdatTimeCode);
        snd_iprintf(buffer, "     Card State: %s\n", stateName[korg1212->cardState]);
        snd_iprintf(buffer, "Idle mon. State: %d\n", korg1212->idleMonitorOn);
        snd_iprintf(buffer, "Cmd retry count: %d\n", korg1212->cmdRetryCount);
        snd_iprintf(buffer, "      Irq count: %ld\n", korg1212->irqcount);
}

static void __init snd_korg1212_proc_init(korg1212_t *korg1212)
{
	snd_info_entry_t *entry;

	if ((entry = snd_info_create_card_entry(korg1212->card, "korg1212", korg1212->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = korg1212;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_korg1212_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	korg1212->proc_entry = entry;
}

static void snd_korg1212_proc_done(korg1212_t * korg1212)
{
	if (korg1212->proc_entry) {
		snd_info_unregister(korg1212->proc_entry);
		korg1212->proc_entry = NULL;
	}
}

static int __init snd_korg1212_create(korg1212_t *korg1212)
{
        struct pci_dev *pci = korg1212->pci;
        int err;
        int i;
	unsigned ioport_size, iomem_size, iomem2_size;
	dma_addr_t phys_addr;

        korg1212->irq = -1;
        korg1212->clkSource = K1212_CLKIDX_Local;
        korg1212->clkRate = 44100;
        korg1212->inIRQ = 0;
        korg1212->running = 0;
        snd_korg1212_setCardState(korg1212, K1212_STATE_UNINITIALIZED);
        korg1212->idleMonitorOn = 0;
        korg1212->clkSrcRate = K1212_CLKIDX_LocalAt44_1K;
        korg1212->leftADCInSens = k1212MaxADCSens;
        korg1212->rightADCInSens = k1212MaxADCSens;

        for (i=0; i<kAudioChannels; i++)
                korg1212->volumePhase[i] = 0;

        if ((err = pci_enable_device(pci)) < 0)
                return err;

        korg1212->iomem = pci_resource_start(korg1212->pci, 0);
        korg1212->ioport = pci_resource_start(korg1212->pci, 1);
        korg1212->iomem2 = pci_resource_start(korg1212->pci, 2);

	iomem_size = pci_resource_len(korg1212->pci, 0);
	ioport_size = pci_resource_len(korg1212->pci, 1);
	iomem2_size = pci_resource_len(korg1212->pci, 2);

#ifdef DEBUG
        PRINTK("DEBUG: resources:\n"
                   "    iomem = 0x%lx (%d)\n"
		   "    ioport  = 0x%lx (%d)\n"
                   "    iomem = 0x%lx (%d)\n"
		   "    [%s]\n",
		   korg1212->iomem, iomem_size,
		   korg1212->ioport, ioport_size,
		   korg1212->iomem2, iomem2_size,
		   stateName[korg1212->cardState]);
#endif

        korg1212->res_iomem = request_mem_region(korg1212->iomem, iomem_size, "korg1212");
        if (korg1212->res_iomem == NULL) {
		snd_printk("unable to grab region 0x%lx-0x%lx\n",
                           korg1212->iomem, korg1212->iomem + iomem_size - 1);
                return -EBUSY;
        }

        korg1212->res_ioport = request_region(korg1212->ioport, ioport_size, "korg1212");
        if (korg1212->res_ioport == NULL) {
		snd_printk("unable to grab region 0x%lx-0x%lx\n",
                           korg1212->ioport, korg1212->ioport + ioport_size - 1);
                return -EBUSY;
        }

        korg1212->res_iomem2 = request_mem_region(korg1212->iomem2, iomem2_size, "korg1212");
        if (korg1212->res_iomem2 == NULL) {
		snd_printk("unable to grab region 0x%lx-0x%lx\n",
                           korg1212->iomem2, korg1212->iomem2 + iomem2_size - 1);
                return -EBUSY;
        }

        if ((korg1212->iobase = (unsigned long) ioremap(korg1212->iomem, iomem_size)) == 0) {
		snd_printk("unable to remap memory region 0x%lx-0x%lx\n", korg1212->iobase,
                           korg1212->iobase + iomem_size - 1);
                return -EBUSY;
        }

        err = request_irq(pci->irq, snd_korg1212_interrupt,
                          SA_INTERRUPT|SA_SHIRQ,
                          "korg1212", (void *) korg1212);

        if (err) {
		snd_printk("unable to grab IRQ %d\n", pci->irq);
                return -EBUSY;
        }

        korg1212->irq = pci->irq;

        init_waitqueue_head(&korg1212->wait);
        spin_lock_init(&korg1212->lock);
	pci_set_master(korg1212->pci);

        korg1212->statusRegPtr = (u32 *) (korg1212->iobase + STATUS_REG_OFFSET);
        korg1212->outDoorbellPtr = (u32 *) (korg1212->iobase + OUT_DOORBELL_OFFSET);
        korg1212->inDoorbellPtr = (u32 *) (korg1212->iobase + IN_DOORBELL_OFFSET);
        korg1212->mailbox0Ptr = (u32 *) (korg1212->iobase + MAILBOX0_OFFSET);
        korg1212->mailbox1Ptr = (u32 *) (korg1212->iobase + MAILBOX1_OFFSET);
        korg1212->mailbox2Ptr = (u32 *) (korg1212->iobase + MAILBOX2_OFFSET);
        korg1212->mailbox3Ptr = (u32 *) (korg1212->iobase + MAILBOX3_OFFSET);
        korg1212->controlRegPtr = (u32 *) (korg1212->iobase + PCI_CONTROL_OFFSET);
        korg1212->sensRegPtr = (u16 *) (korg1212->iobase + SENS_CONTROL_OFFSET);
        korg1212->idRegPtr = (u32 *) (korg1212->iobase + DEV_VEND_ID_OFFSET);

#ifdef DEBUG
        PRINTK("DEBUG: card registers:\n"
                   "    Status register = 0x%p\n"
                   "    OutDoorbell     = 0x%p\n"
                   "    InDoorbell      = 0x%p\n"
                   "    Mailbox0        = 0x%p\n"
                   "    Mailbox1        = 0x%p\n"
                   "    Mailbox2        = 0x%p\n"
                   "    Mailbox3        = 0x%p\n"
                   "    ControlReg      = 0x%p\n"
                   "    SensReg         = 0x%p\n"
                   "    IDReg           = 0x%p\n"
		   "    [%s]\n",
                   korg1212->statusRegPtr,
		   korg1212->outDoorbellPtr,
		   korg1212->inDoorbellPtr,
                   korg1212->mailbox0Ptr,
                   korg1212->mailbox1Ptr,
                   korg1212->mailbox2Ptr,
                   korg1212->mailbox3Ptr,
                   korg1212->controlRegPtr,
                   korg1212->sensRegPtr,
                   korg1212->idRegPtr,
		   stateName[korg1212->cardState]);
#endif

	korg1212->sharedBufferPtr = (KorgSharedBuffer *) snd_malloc_pci_pages(korg1212->pci, sizeof(KorgSharedBuffer), &phys_addr);
	korg1212->sharedBufferPhy = (unsigned long)phys_addr;

        if (korg1212->sharedBufferPtr == NULL) {
		snd_printk("can not allocate shared buffer memory (%d bytes)\n", sizeof(KorgSharedBuffer));
                return -ENOMEM;
        }

#ifdef DEBUG
        PRINTK("DEBUG: Shared Buffer Area = 0x%p (0x%08lx), %d bytes\n", korg1212->sharedBufferPtr, korg1212->sharedBufferPhy, sizeof(KorgSharedBuffer));
#endif

#ifndef LARGEALLOC

        korg1212->DataBufsSize = sizeof(KorgAudioBuffer) * kNumBuffers;

	korg1212->playDataBufsPtr = (KorgAudioBuffer *) snd_malloc_pci_pages(korg1212->pci, korg1212->DataBufsSize, &phys_addr);
	korg1212->PlayDataPhy = (u32)phys_addr;

        if (korg1212->playDataBufsPtr == NULL) {
		snd_printk("can not allocate play data buffer memory (%d bytes)\n", korg1212->DataBufsSize);
                return -ENOMEM;
        }

#ifdef DEBUG
        PRINTK("DEBUG: Play Data Area = 0x%p (0x%08x), %d bytes\n",
		korg1212->playDataBufsPtr, korg1212->PlayDataPhy, korg1212->DataBufsSize);
#endif

	korg1212->recordDataBufsPtr = (KorgAudioBuffer *) snd_malloc_pci_pages(korg1212->pci, korg1212->DataBufsSize, &phys_addr);
	korg1212->RecDataPhy = (u32)phys_addr;

        if (korg1212->recordDataBufsPtr == NULL) {
		snd_printk("can not allocate record data buffer memory (%d bytes)\n", korg1212->DataBufsSize);
                return -ENOMEM;
        }

#ifdef DEBUG
        PRINTK("DEBUG: Record Data Area = 0x%p (0x%08x), %d bytes\n",
		korg1212->recordDataBufsPtr, korg1212->RecDataPhy, korg1212->DataBufsSize);
#endif

#else // LARGEALLOC

        korg1212->recordDataBufsPtr = korg1212->sharedBufferPtr->recordDataBufs;
        korg1212->playDataBufsPtr = korg1212->sharedBufferPtr->playDataBufs;
        korg1212->PlayDataPhy = (u32) &((KorgSharedBuffer *) korg1212->sharedBufferPhy)->playDataBufs;
        korg1212->RecDataPhy  = (u32) &((KorgSharedBuffer *) korg1212->sharedBufferPhy)->recordDataBufs;

#endif // LARGEALLOC

        korg1212->dspCodeSize = sizeof (dspCode);

        korg1212->VolumeTablePhy = (u32) &((KorgSharedBuffer *) korg1212->sharedBufferPhy)->volumeData;
        korg1212->RoutingTablePhy = (u32) &((KorgSharedBuffer *) korg1212->sharedBufferPhy)->routeData;
        korg1212->AdatTimeCodePhy = (u32) &((KorgSharedBuffer *) korg1212->sharedBufferPhy)->AdatTimeCode;

        korg1212->dspMemPtr = snd_malloc_pci_pages(korg1212->pci, korg1212->dspCodeSize, &phys_addr);
	korg1212->dspMemPhy = (u32)phys_addr;

        if (korg1212->dspMemPtr == NULL) {
		snd_printk("can not allocate dsp code memory (%d bytes)\n", korg1212->dspCodeSize);
                return -ENOMEM;
        }

#ifdef DEBUG
        PRINTK("DEBUG: DSP Code area = 0x%p (0x%08x) %d bytes [%s]\n",
		   korg1212->dspMemPtr, korg1212->dspMemPhy, korg1212->dspCodeSize,
		   stateName[korg1212->cardState]);
#endif

	rc = snd_korg1212_Send1212Command(korg1212, K1212_DB_RebootCard, 0, 0, 0, 0);

#ifdef DEBUG
	if (rc) PRINTK("DEBUG: Reboot Card - RC = %d [%s]\n", rc, stateName[korg1212->cardState]);
#endif

	snd_korg1212_EnableCardInterrupts(korg1212);

	mdelay(CARD_BOOT_DELAY_IN_MS);

        if (snd_korg1212_downloadDSPCode(korg1212))
        	return -EBUSY;

	printk(KERN_INFO "dspMemPhy       = %08x U[%08x]\n"
               "PlayDataPhy     = %08x L[%08x]\n"
               "RecDataPhy      = %08x L[%08x]\n"
               "VolumeTablePhy  = %08x L[%08x]\n"
               "RoutingTablePhy = %08x L[%08x]\n"
               "AdatTimeCodePhy = %08x L[%08x]\n",
	       korg1212->dspMemPhy,       UpperWordSwap(korg1212->dspMemPhy),
               korg1212->PlayDataPhy,     LowerWordSwap(korg1212->PlayDataPhy),
               korg1212->RecDataPhy,      LowerWordSwap(korg1212->RecDataPhy),
               korg1212->VolumeTablePhy,  LowerWordSwap(korg1212->VolumeTablePhy),
               korg1212->RoutingTablePhy, LowerWordSwap(korg1212->RoutingTablePhy),
               korg1212->AdatTimeCodePhy, LowerWordSwap(korg1212->AdatTimeCodePhy));

        if ((err = snd_pcm_new(korg1212->card, "korg1212", 0, 1, 1, &korg1212->pcm16)) < 0)
                return err;

        korg1212->pcm16->private_data = korg1212;
        korg1212->pcm16->private_free = snd_korg1212_free_pcm;
        strcpy(korg1212->pcm16->name, "korg1212");

        snd_pcm_set_ops(korg1212->pcm16, SNDRV_PCM_STREAM_PLAYBACK, &snd_korg1212_playback_ops);
        snd_pcm_set_ops(korg1212->pcm16, SNDRV_PCM_STREAM_CAPTURE, &snd_korg1212_capture_ops);

	korg1212->pcm16->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

        for (i = 0; i < K1212_CONTROL_ELEMENTS; i++) {
                err = snd_ctl_add(korg1212->card, snd_ctl_new1(&snd_korg1212_controls[i], korg1212));
                if (err < 0)
                        return err;
        }

        snd_korg1212_proc_init(korg1212);

	return 0;

}

static void
snd_korg1212_free(void *private_data)
{
        korg1212_t *korg1212 = (korg1212_t *)private_data;

        if (korg1212 == NULL) {
                return;
        }

        snd_korg1212_proc_done(korg1212);

        snd_korg1212_TurnOffIdleMonitor(korg1212);

        snd_korg1212_DisableCardInterrupts(korg1212);

        if (korg1212->irq >= 0) {
                free_irq(korg1212->irq, (void *)korg1212);
                korg1212->irq = -1;
        }
        if (korg1212->iobase != 0) {
                iounmap((void *)korg1212->iobase);
                korg1212->iobase = 0;
        }
        if (korg1212->res_iomem != NULL) {
                release_resource(korg1212->res_iomem);
                kfree_nocheck(korg1212->res_iomem);
                korg1212->res_iomem = NULL;
        }
        if (korg1212->res_ioport != NULL) {
                release_resource(korg1212->res_ioport);
                kfree_nocheck(korg1212->res_ioport);
                korg1212->res_ioport = NULL;
        }
        if (korg1212->res_iomem2 != NULL) {
                release_resource(korg1212->res_iomem2);
                kfree_nocheck(korg1212->res_iomem2);
                korg1212->res_iomem2 = NULL;
        }

        // ----------------------------------------------------
        // free up memory resources used for the DSP download.
        // ----------------------------------------------------
        if (korg1212->dspMemPtr) {
                snd_free_pci_pages(korg1212->pci, korg1212->dspCodeSize,
                                   korg1212->dspMemPtr, (dma_addr_t)korg1212->dspMemPhy);
                korg1212->dspMemPhy = 0;
                korg1212->dspMemPtr = 0;
                korg1212->dspCodeSize = 0;
        }

#ifndef LARGEALLOC

        // ------------------------------------------------------
        // free up memory resources used for the Play/Rec Buffers
        // ------------------------------------------------------
	if (korg1212->playDataBufsPtr) {
                snd_free_pci_pages(korg1212->pci, korg1212->DataBufsSize,
                                   korg1212->playDataBufsPtr, (dma_addr_t)korg1212->PlayDataPhy);
		korg1212->PlayDataPhy = 0;
		korg1212->playDataBufsPtr = NULL;
        }

	if (korg1212->recordDataBufsPtr) {
                snd_free_pci_pages(korg1212->pci, korg1212->DataBufsSize,
                                   korg1212->recordDataBufsPtr, (dma_addr_t)korg1212->RecDataPhy);
		korg1212->RecDataPhy = 0;
		korg1212->recordDataBufsPtr = NULL;
        }

#endif

        // ----------------------------------------------------
        // free up memory resources used for the Shared Buffers
        // ----------------------------------------------------
	if (korg1212->sharedBufferPtr) {
                snd_free_pci_pages(korg1212->pci, (u32) sizeof(KorgSharedBuffer),
                                   korg1212->sharedBufferPtr, (dma_addr_t)korg1212->sharedBufferPhy);
		korg1212->sharedBufferPhy = 0;
		korg1212->sharedBufferPtr = NULL;
        }
}

/*
 * Card initialisation
 */

static void snd_korg1212_card_free(snd_card_t *card)
{
#ifdef DEBUG
        PRINTK("DEBUG: Freeing card\n");
#endif
	snd_korg1212_free(card->private_data);
}

static int __devinit
snd_korg1212_probe(struct pci_dev *pci,
		const struct pci_device_id *id)
{
	static int dev = 0;
	korg1212_t *korg1212;
	snd_card_t *card;
	int err;

	if (dev >= SNDRV_CARDS) {
		return -ENODEV;
	}
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}
	if ((card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
				 sizeof(korg1212_t))) == NULL)
		return -ENOMEM;

	card->private_free = snd_korg1212_card_free;
	korg1212 = (korg1212_t *)card->private_data;
	korg1212->card = card;
	korg1212->pci = pci;

	if ((err = snd_korg1212_create(korg1212)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "korg1212");
	strcpy(card->shortname, "korg1212");
	sprintf(card->longname, "%s at 0x%lx, irq %d", card->shortname,
		korg1212->iomem, korg1212->irq);

#ifdef DEBUG
        PRINTK("DEBUG: %s\n", card->longname);
#endif

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_korg1212_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	name: "korg1212",
	id_table: snd_korg1212_ids,
	probe: snd_korg1212_probe,
	remove: __devexit_p(snd_korg1212_remove),
};

static int __init alsa_card_korg1212_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "No Korg 1212IO cards found\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_korg1212_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_korg1212_init)
module_exit(alsa_card_korg1212_exit)

#ifndef MODULE

/* format is: snd-korg1212=snd_enable,snd_index,snd_id */

static int __init alsa_card_korg1212_setup(char *str)
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

__setup("snd-korg1212=", alsa_card_korg1212_setup);

#endif /* ifndef MODULE */

