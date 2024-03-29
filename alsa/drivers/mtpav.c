/*
 *      MOTU Midi Timepiece ALSA Main routines
 *      Copyright by Michael T. Mayers (c) Jan 09, 2000
 *      mail: tweakoz@pacbell.net
 *      Thanks to John Galbraith
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *      This driver is for the 'Mark Of The Unicorn' (MOTU)
 *      MidiTimePiece AV multiport MIDI interface 
 *
 *      IOPORTS
 *      -------
 *      8 MIDI Ins and 8 MIDI outs
 *      Video Sync In (BNC), Word Sync Out (BNC), 
 *      ADAT Sync Out (DB9)
 *      SMPTE in/out (1/4")
 *      2 programmable pedal/footswitch inputs and 4 programmable MIDI controller knobs.
 *      Macintosh RS422 serial port
 *      RS422 "network" port for ganging multiple MTP's
 *      PC Parallel Port ( which this driver currently uses )
 *
 *      MISC FEATURES
 *      -------------
 *      Hardware MIDI routing, merging, and filtering   
 *      MIDI Synchronization to Video, ADAT, SMPTE and other Clock sources
 *      128 'scene' memories, recallable from MIDI program change
 *
 *
 * ChangeLog
 * Jun 11 2001	Takashi Iwai <tiwai@suse.de>
 *      - Recoded & debugged
 *      - Added timer interrupt for midi outputs
 *      - snd_hwports is between 1 and 8, which specifies the number of hardware ports.
 *        The three global ports, computer, adat and broadcast ports, are created
 *        always after h/w and remote ports.
 *
 */

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/rawmidi.h>

/*
 *      globals
 */
EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("MOTU MidiTimePiece AV multiport MIDI");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{MOTU,MidiTimePiece AV multiport MIDI}}");

// io resources
#define MTPAV_IOBASE		0x378
#define MTPAV_IRQ		7
#define MTPAV_MAX_PORTS		8

static int snd_index = SNDRV_DEFAULT_IDX1;
static char *snd_id = SNDRV_DEFAULT_STR1;
static long snd_port = MTPAV_IOBASE;	/* 0x378, 0x278 */
static int snd_irq = MTPAV_IRQ;		/* 7, 5 */
static int snd_hwports = MTPAV_MAX_PORTS;	/* use hardware ports 1-8 */

MODULE_PARM(snd_index, "i");
MODULE_PARM_DESC(snd_index, "Index value for MotuMTPAV MIDI.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "s");
MODULE_PARM_DESC(snd_id, "ID string for MotuMTPAV MIDI.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_port, "l");
MODULE_PARM_DESC(snd_port, "Parallel port # for MotuMTPAV MIDI.");
MODULE_PARM_SYNTAX(snd_port, "allows:{{0x378},{0x278}},dialog:list");
MODULE_PARM(snd_irq, "i");
MODULE_PARM_DESC(snd_irq, "Parallel IRQ # for MotuMTPAV MIDI.");
MODULE_PARM_SYNTAX(snd_irq,  "allows:{{7},{5}},dialog:list");
MODULE_PARM(snd_hwports, "i");
MODULE_PARM_DESC(snd_hwports, "Hardware ports # for MotuMTPAV MIDI.");
MODULE_PARM_SYNTAX(snd_hwports, "allows:{{1,8}},dialog:list");

/*
 *      defines
 */
//#define USE_FAKE_MTP //       dont actually read/write to MTP device (for debugging without an actual unit) (does not work yet)

// parallel port usage masks
#define SIGS_BYTE 0x08
#define SIGS_RFD 0x80
#define SIGS_IRQ 0x40
#define SIGS_IN0 0x10
#define SIGS_IN1 0x20

#define SIGC_WRITE 0x04
#define SIGC_READ 0x08
#define SIGC_INTEN 0x10

#define DREG 0
#define SREG 1
#define CREG 2

//
#define MTPAV_MODE_INPUT_OPENED		0x01
#define MTPAV_MODE_OUTPUT_OPENED	0x02
#define MTPAV_MODE_INPUT_TRIGGERED	0x04
#define MTPAV_MODE_OUTPUT_TRIGGERED	0x08

#define NUMPORTS (0x12+1)


/*
 */

typedef struct mtpav_port {
	u8 number;
	u8 hwport;
	u8 mode;
	snd_rawmidi_substream_t *input;
	snd_rawmidi_substream_t *output;
} mtpav_port_t;

typedef struct mtpav {
	snd_card_t *card;
	unsigned long port;
	struct resource *res_port;
	int irq;			/* interrupt (for inputs) */
	spinlock_t spinlock;
	int share_irq;			/* number of accesses to input interrupts */
	int istimer;			/* number of accesses to timer interrupts */
	struct timer_list timer;	/* timer interrupts for outputs */
	snd_rawmidi_t *rmidi;
	int num_ports;		/* number of hw ports (1-8) */
	mtpav_port_t ports[NUMPORTS];	/* all ports including computer, adat and bc */

	u32 inmidiport;		/* selected input midi port */
	u32 inmidistate;	/* during midi command 0xf5 */

	u32 outmidihwport;	/* selected output midi hw port */
} mtpav_t;


/*
 * global instance
 * hey, we handle at most only one card..
 */
static mtpav_t *mtp_card;

/*
 * possible hardware ports (selected by 0xf5 port message)
 *      0x00		all ports
 *      0x01 .. 0x08    this MTP's ports 1..8
 *      0x09 .. 0x10    networked MTP's ports (9..16)
 *      0x11            networked MTP's computer port
 *      0x63            to ADAT
 *
 * mappig:
 *  subdevice 0 - (X-1)    ports
 *            X - (2*X-1)  networked ports
 *            X            computer
 *            X+1          ADAT
 *            X+2          all ports
 *
 *  where X = chip->num_ports
 */

#define MTPAV_PIDX_COMPUTER	0
#define MTPAV_PIDX_ADAT		1
#define MTPAV_PIDX_BROADCAST	2


static int translate_subdevice_to_hwport(mtpav_t *chip, int subdev)
{
	if (subdev < 0)
		return 0x01; /* invalid - use port 0 as default */
	else if (subdev < chip->num_ports)
		return subdev + 1; /* single mtp port */
	else if (subdev < chip->num_ports * 2)
		return subdev - chip->num_ports + 0x09; /* remote port */
	else if (subdev == chip->num_ports * 2 + MTPAV_PIDX_COMPUTER)
		return 0x11; /* computer port */
	else if (subdev == chip->num_ports + MTPAV_PIDX_ADAT)
		return 0x63;		/* ADAT */
	return 0; /* all ports */
}

static int translate_hwport_to_subdevice(mtpav_t *chip, int hwport)
{
	int port;
	if (hwport <= 0x00) /* all ports */
		return chip->num_ports + MTPAV_PIDX_BROADCAST;
	else if (hwport <= 0x08) { /* single port */
		port = hwport - 1;
		if (port >= chip->num_ports)
			port = 0;
		return port;
	} else if (hwport <= 0x10) { /* remote port */
		port = hwport - 0x09 + chip->num_ports;
		if (port >= chip->num_ports * 2)
			port = chip->num_ports;
		return port;
	} else if (hwport == 0x11)  /* computer port */
		return chip->num_ports + MTPAV_PIDX_COMPUTER;
	else  /* ADAT */
		return chip->num_ports + MTPAV_PIDX_ADAT;
}


/*
 */

static u8 snd_mtpav_getreg(mtpav_t *chip, u16 reg)
{
	u8 rval = 0;

	if (reg == SREG) {
		rval = inb(chip->port + SREG);
		rval = (rval & 0xf8);
	} else if (reg == CREG) {
		rval = inb(chip->port + CREG);
		rval = (rval & 0x1c);
	}

	return rval;
}

/*
 */

static void snd_mtpav_mputreg(mtpav_t *chip, u16 reg, u8 val)
{
	if (reg == DREG) {
		outb(val, chip->port + DREG);
	} else if (reg == CREG) {
		outb(val, chip->port + CREG);
	}
}

/*
 */

static void snd_mtpav_wait_rfdhi(mtpav_t *chip)
{
	int counts = 10000;
	u8 sbyte;

	sbyte = snd_mtpav_getreg(chip, SREG);
	while (!(sbyte & SIGS_RFD) && counts--) {
		sbyte = snd_mtpav_getreg(chip, SREG);
		udelay(10);
	}
}

static void snd_mtpav_send_byte(mtpav_t *chip, u8 byte)
{
	u8 tcbyt;
	u8 clrwrite;
	u8 setwrite;

	snd_mtpav_wait_rfdhi(chip);

	/////////////////

	tcbyt = snd_mtpav_getreg(chip, CREG);
	clrwrite = tcbyt & (SIGC_WRITE ^ 0xff);
	setwrite = tcbyt | SIGC_WRITE;

	snd_mtpav_mputreg(chip, DREG, byte);
	snd_mtpav_mputreg(chip, CREG, clrwrite);	// clear write bit

	snd_mtpav_mputreg(chip, CREG, setwrite);	// set write bit

}


/*
 */

/* call this with spin lock held */
static void snd_mtpav_output_port_write(mtpav_port_t *port,
					snd_rawmidi_substream_t *substream)
{
	u8 outbyte;

	// send port change command if necessary

	if (port->hwport != mtp_card->outmidihwport) {
		mtp_card->outmidihwport = port->hwport;

		snd_mtpav_send_byte(mtp_card, 0xf5);
		snd_mtpav_send_byte(mtp_card, port->hwport);
		//snd_printk("new outport: 0x%x\n", (unsigned int) port->hwport);

	}

	// send data

	while (snd_rawmidi_transmit(substream, &outbyte, 1) == 1)
		snd_mtpav_send_byte(mtp_card, outbyte);
}

static void snd_mtpav_output_write(snd_rawmidi_substream_t * substream)
{
	mtpav_port_t *port = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	snd_mtpav_output_port_write(port, substream);
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
}


/*
 *      mtpav control
 */

static void snd_mtpav_portscan(mtpav_t *chip)	// put mtp into smart routing mode
{
	u8 port;

	for (port = 0; port < 8; port++) {
		snd_mtpav_send_byte(chip, 0xf5);
		snd_mtpav_send_byte(chip, port);
		snd_mtpav_send_byte(chip, 0xfe);
	}
}

/*
 */

static int snd_mtpav_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	//printk("mtpav port: %d opened\n", (int) substream->number);
	spin_lock_irqsave(&mtp_card->spinlock, flags);
	port->mode |= MTPAV_MODE_INPUT_OPENED;
	port->input = substream;
	if (mtp_card->share_irq++ == 0)
		snd_mtpav_mputreg(mtp_card, CREG, (SIGC_INTEN | SIGC_WRITE));	// enable pport interrupts
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
}

/*
 */

static int snd_mtpav_input_close(snd_rawmidi_substream_t *substream)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	//printk("mtpav port: %d closed\n", (int) port);

	spin_lock_irqsave(&mtp_card->spinlock, flags);

	port->mode &= (~MTPAV_MODE_INPUT_OPENED);
	port->input = NULL;
	if (--mtp_card->share_irq == 0)
		snd_mtpav_mputreg(mtp_card, CREG, 0);	// disable pport interrupts

	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
}

/*
 */

static void snd_mtpav_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	if (up)
		port->mode |= MTPAV_MODE_INPUT_TRIGGERED;
	else
		port->mode &= ~MTPAV_MODE_INPUT_TRIGGERED;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);

}


/*
 * timer interrupt for outputs
 */

static void snd_mtpav_output_timer(unsigned long data)
{
	mtpav_t *chip = snd_magic_cast(mtpav_t, (void *)data, return);
	int p;

	spin_lock(&chip->spinlock);
	/* reprogram timer */
	chip->timer.expires = 1 + jiffies;
	add_timer(&chip->timer);
	/* process each port */
	for (p = 0; p <= chip->num_ports * 2 + MTPAV_PIDX_BROADCAST; p++) {
		mtpav_port_t *port = &mtp_card->ports[p];
		if ((port->mode & MTPAV_MODE_OUTPUT_TRIGGERED) && port->output)
			snd_mtpav_output_port_write(port, port->output);
	}
	spin_unlock(&chip->spinlock);
}

static void snd_mtpav_add_output_timer(mtpav_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	chip->timer.function = snd_mtpav_output_timer;
	chip->timer.data = (unsigned long) mtp_card;
	chip->timer.expires = 1 + jiffies;
	add_timer(&chip->timer);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

static void snd_mtpav_remove_output_timer(mtpav_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	del_timer(&chip->timer);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

/*
 */

static int snd_mtpav_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	port->mode |= MTPAV_MODE_OUTPUT_OPENED;
	port->output = substream;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
};

/*
 */

static int snd_mtpav_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	port->mode &= (~MTPAV_MODE_OUTPUT_OPENED);
	port->output = NULL;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
};

/*
 */

static void snd_mtpav_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	mtpav_port_t *port = &mtp_card->ports[substream->number];

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	if (up) {
		if (! (port->mode  & MTPAV_MODE_OUTPUT_TRIGGERED)) {
			if (mtp_card->istimer++ == 0)
				snd_mtpav_add_output_timer(mtp_card);
			port->mode |= MTPAV_MODE_OUTPUT_TRIGGERED;
		}
	} else {
		port->mode &= ~MTPAV_MODE_OUTPUT_TRIGGERED;
		if (--mtp_card->istimer == 0)
			snd_mtpav_remove_output_timer(mtp_card);
	}
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);

	if (up)
		snd_mtpav_output_write(substream);
}

/*
 * midi interrupt for inputs
 */

static void snd_mtpav_inmidi_process(mtpav_t *mcrd, u8 inbyte)
{
	mtpav_port_t *port;

	if (mcrd->inmidiport > mcrd->num_ports * 2 + MTPAV_PIDX_BROADCAST)
		return;

	port = &mcrd->ports[mcrd->inmidiport];
	if (port->mode & MTPAV_MODE_INPUT_TRIGGERED)
		snd_rawmidi_receive(port->input, &inbyte, 1);
}

static void snd_mtpav_inmidi_h(mtpav_t * mcrd, u8 inbyte)
{
#ifndef TARGET_OS2
	snd_assert(mcrd, return);
#endif
	if (inbyte >= 0xf8) {
		/* real-time midi code */
		snd_mtpav_inmidi_process(mcrd, inbyte);
		return;
	}

	if (mcrd->inmidistate == 0) {	// awaiting command
		if (inbyte == 0xf5)	// MTP port #
			mcrd->inmidistate = 1;
		else
			snd_mtpav_inmidi_process(mcrd, inbyte);
	} else if (mcrd->inmidistate) {
		mcrd->inmidiport = translate_hwport_to_subdevice(mcrd, inbyte);
		mcrd->inmidistate = 0;
	}
}

static void snd_mtpav_read_bytes(mtpav_t * mcrd)
{
	u8 clrread, setread;
	u8 mtp_read_byte;
	u8 sr, cbyt;
	int i;

	u8 sbyt = snd_mtpav_getreg(mcrd, SREG);

	//printk("snd_mtpav_read_bytes() sbyt: 0x%x\n", sbyt);

	if (!(sbyt & SIGS_BYTE))
		return;

	cbyt = snd_mtpav_getreg(mcrd, CREG);
	clrread = cbyt & (SIGC_READ ^ 0xff);
	setread = cbyt | SIGC_READ;

	do {

		mtp_read_byte = 0;
		for (i = 0; i < 4; i++) {
			snd_mtpav_mputreg(mcrd, CREG, setread);
			sr = snd_mtpav_getreg(mcrd, SREG);
			snd_mtpav_mputreg(mcrd, CREG, clrread);

			sr &= SIGS_IN0 | SIGS_IN1;
			sr >>= 4;
			mtp_read_byte |= sr << (i * 2);
		}

		snd_mtpav_inmidi_h(mcrd, mtp_read_byte);

		sbyt = snd_mtpav_getreg(mcrd, SREG);

	} while (sbyt & SIGS_BYTE);
}

static void snd_mtpav_irqh(int irq, void *dev_id, struct pt_regs *regs)
{
	mtpav_t *mcard = snd_magic_cast(mtpav_t, dev_id, return);

	//printk("irqh()\n");
	spin_lock(&mcard->spinlock);
	snd_mtpav_read_bytes(mcard);
	spin_unlock(&mcard->spinlock);
}

/*
 * get ISA resources
 */
static int snd_mtpav_get_ISA(mtpav_t * mcard)
{
	if ((mcard->res_port = request_region(snd_port, 3, "MotuMTPAV MIDI")) == NULL) {
		snd_printk("MTVAP port 0x%lx is busy\n", snd_port);
		return -EBUSY;
	}
	mcard->port = snd_port;
	if (request_irq(snd_irq, snd_mtpav_irqh, SA_INTERRUPT, "MOTU MTPAV", (void *)mcard)) {
		snd_printk("MTVAP IRQ %d busy\n", snd_irq);
		return -EBUSY;
	}
	mcard->irq = snd_irq;
	return 0;
}


/*
 */

#ifdef TARGET_OS2
static snd_rawmidi_ops_t snd_mtpav_output = {
	snd_mtpav_output_open,
	snd_mtpav_output_close,
	snd_mtpav_output_trigger,
        0
};

static snd_rawmidi_ops_t snd_mtpav_input = {
	snd_mtpav_input_open,
	snd_mtpav_input_close,
	snd_mtpav_input_trigger,
        0
};
#else
static snd_rawmidi_ops_t snd_mtpav_output = {
	open:		snd_mtpav_output_open,
	close:		snd_mtpav_output_close,
	trigger:	snd_mtpav_output_trigger,
};

static snd_rawmidi_ops_t snd_mtpav_input = {
	open:		snd_mtpav_input_open,
	close:		snd_mtpav_input_close,
	trigger:	snd_mtpav_input_trigger,
};
#endif

/*
 * get RAWMIDI resources
 */

static void snd_mtpav_set_name(mtpav_t *chip, snd_rawmidi_substream_t *substream)
{
	if (substream->number >= 0 && substream->number < chip->num_ports)
		sprintf(substream->name, "MTP direct %d", (substream->number % chip->num_ports) + 1);
	else if (substream->number >= 8 && substream->number < chip->num_ports * 2)
		sprintf(substream->name, "MTP remote %d", (substream->number % chip->num_ports) + 1);
	else if (substream->number == chip->num_ports * 2)
		strcpy(substream->name, "MTP computer");
	else if (substream->number == chip->num_ports * 2 + 1)
		strcpy(substream->name, "MTP ADAT");
	else
		strcpy(substream->name, "MTP broadcast");
}

static int snd_mtpav_get_RAWMIDI(mtpav_t * mcard)
{
	int rval = 0;
	snd_rawmidi_t *rawmidi;
	snd_rawmidi_substream_t *substream;
	struct list_head *list;

	//printk("entering snd_mtpav_get_RAWMIDI\n");

	if (snd_hwports < 1)
		mcard->num_ports = 1;
	else if (snd_hwports > 8)
		mcard->num_ports = 8;
	else
		mcard->num_ports = snd_hwports;

	if ((rval = snd_rawmidi_new(mcard->card, "MotuMIDI", 0,
				    mcard->num_ports * 2 + MTPAV_PIDX_BROADCAST + 1,
				    mcard->num_ports * 2 + MTPAV_PIDX_BROADCAST + 1,
				    &mcard->rmidi)) < 0)
		return rval;
	rawmidi = mcard->rmidi;

	list_for_each(list, &rawmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		snd_mtpav_set_name(mcard, substream);
		substream->ops = &snd_mtpav_input;
	}
	list_for_each(list, &rawmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		snd_mtpav_set_name(mcard, substream);
		substream->ops = &snd_mtpav_output;
		mcard->ports[substream->number].hwport = translate_subdevice_to_hwport(mcard, substream->number);
	}
	rawmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT |
			       SNDRV_RAWMIDI_INFO_DUPLEX;
	sprintf(rawmidi->name, "MTP AV MIDI");
	//printk("exiting snd_mtpav_get_RAWMIDI() \n");
	return 0;
}

/*
 */

static mtpav_t *new_mtpav(void)
{
	mtpav_t *ncrd = (mtpav_t *) snd_kcalloc(sizeof(mtpav_t), GFP_KERNEL);
	if (ncrd != NULL) {
		spin_lock_init(&ncrd->spinlock);

		ncrd->card = NULL;
		ncrd->irq = -1;
		ncrd->share_irq = 0;

		ncrd->inmidiport = 0xffffffff;
		ncrd->inmidistate = 0;
		ncrd->outmidihwport = 0xffffffff;
	}
	return ncrd;
}

/*
 */

static void free_mtpav(mtpav_t * crd)
{
	unsigned long flags;

	spin_lock_irqsave(&crd->spinlock, flags);
	if (crd->istimer > 0)
		snd_mtpav_remove_output_timer(crd);
	spin_unlock_irqrestore(&crd->spinlock, flags);
	if (crd->irq >= 0)
		free_irq(crd->irq, (void *)crd);
	if (crd->res_port)
		release_resource(crd->res_port);
	if (crd != NULL)
		kfree(crd);
}

/*
 */

static int __init alsa_card_mtpav_init(void)
{
	int err = 0;
	char longname_buffer[80];

	mtp_card = new_mtpav();
	if (mtp_card == NULL)
		return -ENOMEM;

	mtp_card->card = snd_card_new(snd_index, snd_id, THIS_MODULE, 0);
	if (mtp_card->card == NULL) {
		free_mtpav(mtp_card);
		return -ENOMEM;
	}

	err = snd_mtpav_get_ISA(mtp_card);
	//printk("snd_mtpav_get_ISA returned: %d\n", err);
	if (err < 0)
		goto __error;

	strcpy(mtp_card->card->driver, "MTPAV");
	strcpy(mtp_card->card->shortname, "MTPAV on parallel port");
	memset(longname_buffer, 0, sizeof(longname_buffer));
	sprintf(longname_buffer, "MTPAV on parallel port at");

	err = snd_mtpav_get_RAWMIDI(mtp_card);
	//snd_printk("snd_mtapv_get_RAWMIDI returned: %d\n", err);
	if (err < 0)
		goto __error;

	err = snd_card_register(mtp_card->card);	// dont snd_card_register until AFTER all cards reources done!

	//printk("snd_card_register returned %d\n", err);
	if (err < 0)
		goto __error;


	snd_mtpav_portscan(mtp_card);

	snd_printk("Motu MidiTimePiece on parallel port irq: %d ioport: 0x%lx\n", snd_irq, snd_port);

	return 0;

      __error:
	snd_card_free(mtp_card->card);
	free_mtpav(mtp_card);
	return err;
}

/*
 */

static void __exit alsa_card_mtpav_exit(void)
{
	if (mtp_card == NULL)
		return;
	if (mtp_card->card)
		snd_card_free(mtp_card->card);
	free_mtpav(mtp_card);
}

/*
 */

module_init(alsa_card_mtpav_init)
module_exit(alsa_card_mtpav_exit)

#ifndef MODULE

/* format is: snd-card-mtpav=snd_enable,snd_index,snd_id,
			     snd_port,snd_irq,snd_hwports */

static int __init alsa_card_mtpav_setup(char *str)
{
        int __attribute__ ((__unused__)) enable = 1;

	(void)(get_option(&str,&enable) == 2 &&
	       get_option(&str,&snd_index) == 2 &&
	       get_id(&str,&snd_id) == 2 &&
	       get_option(&str,(int *)&snd_port) == 2 &&
	       get_option(&str,&snd_irq) == 2 &&
	       get_option(&str,&snd_hwports) == 2);
	return 1;
}

__setup("snd-card-mtpav=", alsa_card_mtpav_setup);

#endif /* ifndef MODULE */
