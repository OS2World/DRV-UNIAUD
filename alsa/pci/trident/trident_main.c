/*
 *  Maintained by Jaroslav Kysela <perex@suse.cz>
 *  Originated by audio@tridentmicro.com
 *  Fri Feb 19 15:55:28 MST 1999
 *  Routines for control of Trident 4DWave (DX and NX) chip
 *
 *  BUGS:
 *
 *  TODO:
 *    ---
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
#include <sound/trident.h>

#define chip_t trident_t

static int snd_trident_pcm_mixer_build(trident_t *trident, snd_trident_voice_t * voice, snd_pcm_substream_t *substream);
static int snd_trident_pcm_mixer_free(trident_t *trident, snd_trident_voice_t * voice, snd_pcm_substream_t *substream);
static void snd_trident_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/*
 *  common I/O routines
 */


#if 0
static void snd_trident_print_voice_regs(trident_t *trident, int voice)
{
	unsigned int val, tmp;

	printk("Trident voice %i:\n", voice);
	outb(voice, TRID_REG(trident, T4D_LFO_GC_CIR));
	val = inl(TRID_REG(trident, CH_LBA));
	printk("LBA: 0x%x\n", val);
	val = inl(TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC));
	printk("GVSel: %i\n", val >> 31);
	printk("Pan: 0x%x\n", (val >> 24) & 0x7f);
	printk("Vol: 0x%x\n", (val >> 16) & 0xff);
	printk("CTRL: 0x%x\n", (val >> 12) & 0x0f);
	printk("EC: 0x%x\n", val & 0x0fff);
	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		val = inl(TRID_REG(trident, CH_DX_CSO_ALPHA_FMS));
		printk("CSO: 0x%x\n", val >> 16);
		printk("Alpha: 0x%x\n", (val >> 4) & 0x0fff);
		printk("FMS: 0x%x\n", val & 0x0f);
		val = inl(TRID_REG(trident, CH_DX_ESO_DELTA));
		printk("ESO: 0x%x\n", val >> 16);
		printk("Delta: 0x%x\n", val & 0xffff);
		val = inl(TRID_REG(trident, CH_DX_FMC_RVOL_CVOL));
	} else {		// TRIDENT_DEVICE_ID_NX
		val = inl(TRID_REG(trident, CH_NX_DELTA_CSO));
		tmp = (val >> 24) & 0xff;
		printk("CSO: 0x%x\n", val & 0x00ffffff);
		val = inl(TRID_REG(trident, CH_NX_DELTA_ESO));
		tmp |= (val >> 16) & 0xff00;
		printk("Delta: 0x%x\n", tmp);
		printk("ESO: 0x%x\n", val & 0x00ffffff);
		val = inl(TRID_REG(trident, CH_NX_ALPHA_FMS_FMC_RVOL_CVOL));
		printk("Alpha: 0x%x\n", val >> 20);
		printk("FMS: 0x%x\n", (val >> 16) & 0x0f);
	}
	printk("FMC: 0x%x\n", (val >> 14) & 3);
	printk("RVol: 0x%x\n", (val >> 7) & 0x7f);
	printk("CVol: 0x%x\n", val & 0x7f);
}
#endif

/*---------------------------------------------------------------------------
   unsigned short snd_trident_codec_read(ac97_t *ac97, unsigned short reg)
  
   Description: This routine will do all of the reading from the external
                CODEC (AC97).
  
   Parameters:  ac97 - ac97 codec structure
                reg - CODEC register index, from AC97 Hal.
 
   returns:     16 bit value read from the AC97.
  
  ---------------------------------------------------------------------------*/
static unsigned short snd_trident_codec_read(ac97_t *ac97, unsigned short reg)
{
	unsigned int data = 0, treg;
	unsigned short count = 0xffff;
	unsigned long flags;
	trident_t *trident = snd_magic_cast(trident_t, ac97->private_data, return -ENXIO);

	spin_lock_irqsave(&trident->reg_lock, flags);
	if (trident->device == TRIDENT_DEVICE_ID_DX) {
		data = (DX_AC97_BUSY_READ | (reg & 0x000000ff));
		outl(data, TRID_REG(trident, DX_ACR1_AC97_R));
		do {
			data = inl(TRID_REG(trident, DX_ACR1_AC97_R));
			if ((data & DX_AC97_BUSY_READ) == 0)
				break;
		} while (--count);
	} else if (trident->device == TRIDENT_DEVICE_ID_NX) {
		data = (NX_AC97_BUSY_READ | (reg & 0x000000ff));
		treg = ac97->num == 0 ? NX_ACR2_AC97_R_PRIMARY : NX_ACR3_AC97_R_SECONDARY;
		outl(data, TRID_REG(trident, treg));
		do {
			data = inl(TRID_REG(trident, treg));
			if ((data & 0x00000C00) == 0)
				break;
		} while (--count);
	} else if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		data = (SI_AC97_BUSY_READ | (reg & 0x000000ff));
		if (ac97->num == 1)
			data |= SI_AC97_SECONDARY;
		outl(data, TRID_REG(trident, SI_AC97_READ));
		do {
			data = inl(TRID_REG(trident, SI_AC97_READ));
			if ((data & (SI_AC97_BUSY_READ|SI_AC97_AUDIO_BUSY)) == 0)
				break;
		} while (--count);
	}

	if (count == 0) {
		snd_printk("ac97 codec read TIMEOUT [0x%x/0x%x]!!!\n", reg, data);
		data = 0;
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return ((unsigned short) (data >> 16));
}

/*---------------------------------------------------------------------------
   void snd_trident_codec_write(ac97_t *ac97, unsigned short reg, unsigned short wdata)
  
   Description: This routine will do all of the writing to the external
                CODEC (AC97).
  
   Parameters:	ac97 - ac97 codec structure
   	        reg - CODEC register index, from AC97 Hal.
                data  - Lower 16 bits are the data to write to CODEC.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/
static void snd_trident_codec_write(ac97_t *ac97, unsigned short reg, unsigned short wdata)
{
	unsigned int address, data;
	unsigned short count = 0xffff;
	unsigned long flags;
	trident_t *trident = snd_magic_cast(trident_t, ac97->private_data, return);

	data = ((unsigned long) wdata) << 16;

	spin_lock_irqsave(&trident->reg_lock, flags);
	if (trident->device == TRIDENT_DEVICE_ID_DX) {
		address = DX_ACR0_AC97_W;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & DX_AC97_BUSY_WRITE) == 0)
				break;
		} while (--count);

		data |= (DX_AC97_BUSY_WRITE | (reg & 0x000000ff));
	} else if (trident->device == TRIDENT_DEVICE_ID_NX) {
		address = NX_ACR1_AC97_W;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & NX_AC97_BUSY_WRITE) == 0)
				break;
		} while (--count);

		data |= (NX_AC97_BUSY_WRITE | (ac97->num << 8) | (reg & 0x000000ff));
	} else if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		address = SI_AC97_WRITE;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & (SI_AC97_BUSY_WRITE|SI_AC97_AUDIO_BUSY)) == 0)
				break;
		} while (--count);

		data |= (SI_AC97_BUSY_WRITE | (reg & 0x000000ff));
		if (ac97->num == 1)
			data |= SI_AC97_SECONDARY;
	} else {
		address = 0;	/* keep GCC happy */
		count = 0;	/* return */
	}

	if (count == 0) {
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		return;
	}
	outl(data, TRID_REG(trident, address));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
}

/*---------------------------------------------------------------------------
   void snd_trident_enable_eso(trident_t *trident)
  
   Description: This routine will enable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
                Also enables middle of loop interrupts.
  
   Parameters:  trident - pointer to target device class for 4DWave.
  
  ---------------------------------------------------------------------------*/

static void snd_trident_enable_eso(trident_t * trident)
{
	unsigned int val;

	val = inl(TRID_REG(trident, T4D_LFO_GC_CIR));
	val |= ENDLP_IE;
	val |= MIDLP_IE;
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		val |= BANK_B_EN;
	outl(val, TRID_REG(trident, T4D_LFO_GC_CIR));
}

/*---------------------------------------------------------------------------
   void snd_trident_disable_eso(trident_t *trident)
  
   Description: This routine will disable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
                Also disables middle of loop interrupts.
  
   Parameters:  
                trident - pointer to target device class for 4DWave.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/

static void snd_trident_disable_eso(trident_t * trident)
{
	unsigned int tmp;

	tmp = inl(TRID_REG(trident, T4D_LFO_GC_CIR));
	tmp &= ~ENDLP_IE;
	tmp &= ~MIDLP_IE;
	outl(tmp, TRID_REG(trident, T4D_LFO_GC_CIR));
}

/*---------------------------------------------------------------------------
   void snd_trident_start_voice(trident_t * trident, unsigned int voice)

    Description: Start a voice, any channel 0 thru 63.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.

    Parameters : voice - Voice number 0 thru n.
                 trident - pointer to target device class for 4DWave.

    Return Value: None.

  ---------------------------------------------------------------------------*/

void snd_trident_start_voice(trident_t * trident, unsigned int voice)
{
	unsigned int mask = 1 << (voice & 0x1f);
	unsigned int reg = (voice & 0x20) ? T4D_START_B : T4D_START_A;

	outl(mask, TRID_REG(trident, reg));
}

/*---------------------------------------------------------------------------
   void snd_trident_stop_voice(trident_t * trident, unsigned int voice)

    Description: Stop a voice, any channel 0 thru 63.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.

    Parameters : voice - Voice number 0 thru n.
                 trident - pointer to target device class for 4DWave.

    Return Value: None.

  ---------------------------------------------------------------------------*/

void snd_trident_stop_voice(trident_t * trident, unsigned int voice)
{
	unsigned int mask = 1 << (voice & 0x1f);
	unsigned int reg = (voice & 0x20) ? T4D_STOP_B : T4D_STOP_A;

	outl(mask, TRID_REG(trident, reg));
}

/*---------------------------------------------------------------------------
    int snd_trident_allocate_pcm_channel(trident_t *trident)
  
    Description: Allocate hardware channel in Bank B (32-63).
  
    Parameters :  trident - pointer to target device class for 4DWave.
  
    Return Value: hardware channel - 32-63 or -1 when no channel is available
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_pcm_channel(trident_t * trident)
{
	int idx;

	if (trident->ChanPCMcnt >= trident->ChanPCM)
		return -1;
	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[T4D_BANK_B] & (1 << idx))) {
			trident->ChanMap[T4D_BANK_B] |= 1 << idx;
			trident->ChanPCMcnt++;
			return idx + 32;
		}
	}
	return -1;
}

/*---------------------------------------------------------------------------
    void snd_trident_free_pcm_channel(int channel)
  
    Description: Free hardware channel in Bank B (32-63)
  
    Parameters :  trident - pointer to target device class for 4DWave.
	          channel - hardware channel number 0-63
  
    Return Value: none
  
  ---------------------------------------------------------------------------*/

static void snd_trident_free_pcm_channel(trident_t *trident, int channel)
{
	if (channel < 32 || channel > 63)
		return;
	channel &= 0x1f;
	if (trident->ChanMap[T4D_BANK_B] & (1 << channel)) {
		trident->ChanMap[T4D_BANK_B] &= ~(1 << channel);
		trident->ChanPCMcnt--;
	}
}

/*---------------------------------------------------------------------------
    unsigned int snd_trident_allocate_synth_channel(void)
  
    Description: Allocate hardware channel in Bank A (0-31).
  
    Parameters :  trident - pointer to target device class for 4DWave.
  
    Return Value: hardware channel - 0-31 or -1 when no channel is available
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_synth_channel(trident_t * trident)
{
	int idx;

	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[T4D_BANK_A] & (1 << idx))) {
			trident->ChanMap[T4D_BANK_A] |= 1 << idx;
			trident->synth.ChanSynthCount++;
			return idx;
		}
	}
	return -1;
}

/*---------------------------------------------------------------------------
    void snd_trident_free_synth_channel( int channel )
  
    Description: Free hardware channel in Bank B (0-31).
  
    Parameters :  trident - pointer to target device class for 4DWave.
	          channel - hardware channel number 0-63
  
    Return Value: none
  
  ---------------------------------------------------------------------------*/

static void snd_trident_free_synth_channel(trident_t *trident, int channel)
{
	if (channel < 0 || channel > 31)
		return;
	channel &= 0x1f;
	if (trident->ChanMap[T4D_BANK_A] & (1 << channel)) {
		trident->ChanMap[T4D_BANK_A] &= ~(1 << channel);
		trident->synth.ChanSynthCount--;
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_voice_regs
  
   Description: This routine will complete and write the 5 hardware channel
                registers to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Each register field.
  
  ---------------------------------------------------------------------------*/

void snd_trident_write_voice_regs(trident_t * trident,
				  snd_trident_voice_t * voice)
{
	unsigned int FmcRvolCvol;
	unsigned int regs[5];

	regs[1] = voice->LBA;
	regs[4] = (voice->GVSel << 31) |
		  ((voice->Pan & 0x0000007f) << 24) |
		  ((voice->CTRL & 0x0000000f) << 12);
	FmcRvolCvol = ((voice->FMC & 3) << 14) |
	              ((voice->RVol & 0x7f) << 7) |
	              (voice->CVol & 0x7f);

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_SI7018:
		regs[4] |= voice->number > 31 ?
				(voice->Vol & 0x000003ff) :
				((voice->Vol & 0x00003fc) << (16-2)) |
				(voice->EC & 0x00000fff);
		regs[0] = (voice->CSO << 16) | ((voice->Alpha & 0x00000fff) << 4) | (voice->FMS & 0x0000000f);
		regs[2] = (voice->ESO << 16) | (voice->Delta & 0x0ffff);
		regs[3] = (voice->Attribute << 16) | FmcRvolCvol;
		break;
	case TRIDENT_DEVICE_ID_DX:
		regs[4] |= ((voice->Vol & 0x000003fc) << (16-2)) |
			   (voice->EC & 0x00000fff);
		regs[0] = (voice->CSO << 16) | ((voice->Alpha & 0x00000fff) << 4) | (voice->FMS & 0x0000000f);
		regs[2] = (voice->ESO << 16) | (voice->Delta & 0x0ffff);
		regs[3] = FmcRvolCvol;
		break;
	case TRIDENT_DEVICE_ID_NX:
		regs[4] |= ((voice->Vol & 0x000003fc) << (16-2)) |
			   (voice->EC & 0x00000fff);
		regs[0] = (voice->Delta << 24) | (voice->CSO & 0x00ffffff);
		regs[2] = ((voice->Delta << 16) & 0xff000000) | (voice->ESO & 0x00ffffff);
		regs[3] = (voice->Alpha << 20) | ((voice->FMS & 0x0000000f) << 16) | FmcRvolCvol;
		break;
	default:
		snd_BUG();
	}

	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outl(regs[0], TRID_REG(trident, CH_START + 0));
	outl(regs[1], TRID_REG(trident, CH_START + 4));
	outl(regs[2], TRID_REG(trident, CH_START + 8));
	outl(regs[3], TRID_REG(trident, CH_START + 12));
	outl(regs[4], TRID_REG(trident, CH_START + 16));
}

/*---------------------------------------------------------------------------
   snd_trident_write_cso_reg
  
   Description: This routine will write the new CSO offset
                register to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                CSO - new CSO value
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_cso_reg(trident_t * trident, snd_trident_voice_t * voice, unsigned int CSO)
{
	voice->CSO = CSO;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		outw(voice->CSO, TRID_REG(trident, CH_DX_CSO_ALPHA_FMS) + 2);
	} else {
		outl((voice->Delta << 24) | (voice->CSO & 0x00ffffff), TRID_REG(trident, CH_NX_DELTA_CSO));
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_vol_reg
  
   Description: This routine will write the new voice volume
                register to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Vol - new voice volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_vol_reg(trident_t * trident, snd_trident_voice_t * voice, unsigned int Vol)
{
	voice->Vol = Vol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
	case TRIDENT_DEVICE_ID_NX:
		outb(voice->Vol >> 2, TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC + 2));
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		printk("voice->Vol = 0x%x\n", voice->Vol);
		outw((voice->CTRL << 12) | voice->Vol, TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC));
		break;
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_pan_reg
  
   Description: This routine will write the new voice pan
                register to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Pan - new pan value
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_pan_reg(trident_t * trident, snd_trident_voice_t * voice, unsigned int Pan)
{
	voice->Pan = Pan;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outb(((voice->GVSel & 0x01) << 7) | (voice->Pan & 0x7f), TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC + 3));
}

/*---------------------------------------------------------------------------
   snd_trident_write_rvol_reg
  
   Description: This routine will write the new reverb volume
                register to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                RVol - new reverb volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_rvol_reg(trident_t * trident, snd_trident_voice_t * voice, unsigned int RVol)
{
	voice->RVol = RVol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outw(((voice->FMC & 0x0003) << 14) | ((voice->RVol & 0x007f) << 7) | (voice->CVol & 0x007f),
	     TRID_REG(trident, trident->device == TRIDENT_DEVICE_ID_NX ? CH_NX_ALPHA_FMS_FMC_RVOL_CVOL : CH_DX_FMC_RVOL_CVOL));
}

/*---------------------------------------------------------------------------
   snd_trident_write_cvol_reg
  
   Description: This routine will write the new chorus volume
                register to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                CVol - new chorus volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_cvol_reg(trident_t * trident, snd_trident_voice_t * voice, unsigned int CVol)
{
	voice->CVol = CVol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outw(((voice->FMC & 0x0003) << 14) | ((voice->RVol & 0x007f) << 7) | (voice->CVol & 0x007f),
	     TRID_REG(trident, trident->device == TRIDENT_DEVICE_ID_NX ? CH_NX_ALPHA_FMS_FMC_RVOL_CVOL : CH_DX_FMC_RVOL_CVOL));
}

/*---------------------------------------------------------------------------
   snd_trident_convert_rate

   Description: This routine converts rate in HZ to hardware delta value.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                rate - Real or Virtual channel number.
  
   Returns:     Delta value.
  
  ---------------------------------------------------------------------------*/
unsigned int snd_trident_convert_rate(unsigned int rate)
{
	unsigned int delta;

	// We special case 44100 and 8000 since rounding with the equation
	// does not give us an accurate enough value. For 11025 and 22050
	// the equation gives us the best answer. All other frequencies will
	// also use the equation. JDW
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + 24000) / 48000) & 0x0000ffff;
	return delta;
}

/*---------------------------------------------------------------------------
   snd_trident_convert_adc_rate

   Description: This routine converts rate in HZ to hardware delta value.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                rate - Real or Virtual channel number.
  
   Returns:     Delta value.
  
  ---------------------------------------------------------------------------*/
static unsigned int snd_trident_convert_adc_rate(unsigned int rate)
{
	unsigned int delta;

	// We special case 44100 and 8000 since rounding with the equation
	// does not give us an accurate enough value. For 11025 and 22050
	// the equation gives us the best answer. All other frequencies will
	// also use the equation. JDW
	if (rate == 44100)
		delta = 0x116a;
	else if (rate == 8000)
		delta = 0x6000;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = ((48000 << 12) / rate) & 0x0000ffff;
	return delta;
}

/*---------------------------------------------------------------------------
   snd_trident_control_mode

   Description: This routine returns a control mode for a PCM channel.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                substream  - PCM substream
  
   Returns:     Control value.
  
  ---------------------------------------------------------------------------*/
unsigned int snd_trident_control_mode(snd_pcm_substream_t *substream)
{
	unsigned int CTRL;
	snd_pcm_runtime_t *runtime = substream->runtime;

	/* set ctrl mode
	   CTRL default: 8-bit (unsigned) mono, loop mode enabled
	 */
	CTRL = 0x00000001;
	if (snd_pcm_format_width(runtime->format) == 16)
		CTRL |= 0x00000008;	// 16-bit data
	if (snd_pcm_format_signed(runtime->format))
		CTRL |= 0x00000002;	// signed data
	if (runtime->channels > 1)
		CTRL |= 0x00000004;	// stereo data
	return CTRL;
}

/*
 *  PCM part
 */

/*---------------------------------------------------------------------------
   snd_trident_ioctl
  
   Description: Device I/O control handler for playback/capture parameters.
  
   Paramters:   substream  - PCM substream class
                cmd     - what ioctl message to process
                arg     - additional message infoarg     
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_ioctl(snd_pcm_substream_t * substream,
			     unsigned int cmd,
			     void *arg)
{
	/* FIXME: it seems that with small periods the behaviour of
	          trident hardware is unpredictable and interrupt generator
	          is broken */
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

/*---------------------------------------------------------------------------
   snd_trident_playback_hw_params
  
   Description: Set the hardware parameters for the playback device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_playback_hw_params(snd_pcm_substream_t * substream,
					  snd_pcm_hw_params_t * hw_params)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	unsigned long flags;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0 && trident->tlb.entries) {
		if (voice->memblk)
			snd_trident_free_pages(trident, voice->memblk);
		spin_lock_irqsave(&trident->reg_lock, flags);
		voice->memblk = snd_trident_alloc_pages(trident, runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		if (voice->memblk == NULL)
			return -ENOMEM;
	}

	/* voice management */

	if (params_buffer_size(hw_params) / 2 != params_period_size(hw_params)) {
		if (evoice == NULL) {
			evoice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
			if (evoice == NULL)
				return -ENOMEM;
			voice->extra = evoice;
			evoice->substream = substream;
		}
	} else {
		if (evoice != NULL) {
			snd_trident_free_voice(trident, evoice);
			voice->extra = evoice = NULL;
		}
	}

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_hw_free
  
   Description: Release the hardware resources for the playback device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_playback_hw_free(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;

	if (trident->tlb.entries) {
		if (voice->memblk) {
			snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = NULL;
		}
	}
	snd_pcm_lib_free_pages(substream);
	if (evoice != NULL) {
		snd_trident_free_voice(trident, evoice);
		voice->extra = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_prepare
  
   Description: Prepare playback device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_playback_prepare(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	snd_trident_pcm_mixer_t *mix = &trident->pcm_mixer[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);	

	/* set delta (rate) value */
	voice->Delta = snd_trident_convert_rate(runtime->rate);

	/* set Loop Begin Address */
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;
	else
		voice->LBA = runtime->dma_addr;
 
	voice->CSO = 0;
	voice->ESO = runtime->buffer_size - 1;	/* in samples */
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->GVSel = 1;
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Vol = mix->vol;
	voice->RVol = mix->rvol;
	voice->CVol = mix->cvol;
	voice->Pan = mix->pan;
	voice->Attribute = 0;

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = voice->Delta;
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) - 1; /* in samples */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = trident->device == TRIDENT_DEVICE_ID_SI7018 ? 0 : 1;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
#if 0
		evoice->Attribute = (1<<(30-16))|(2<<(26-16))|
				    (1<<(24-16))|(0x1f<<(19-16));
#else
		evoice->Attribute = 0;
#endif
		snd_trident_write_voice_regs(trident, evoice);
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_hw_params
  
   Description: Set the hardware parameters for the capture device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_capture_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned long flags;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0 && trident->tlb.entries) {
		if (voice->memblk)
			snd_trident_free_pages(trident, voice->memblk);
		spin_lock_irqsave(&trident->reg_lock, flags);
		voice->memblk = snd_trident_alloc_pages(trident, runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		if (voice->memblk == NULL)
			return -ENOMEM;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_hw_free
  
   Description: Release the hardware resources for the capture device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_capture_hw_free(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;

	if (trident->tlb.entries) {
		if (voice->memblk) {
			snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = NULL;
		}
	}
	snd_pcm_lib_free_pages(substream);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_prepare
  
   Description: Prepare capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_capture_prepare(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned int val, ESO_bytes;
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);

	// Initilize the channel and set channel Mode
	outb(0, TRID_REG(trident, LEGACY_DMAR15));

	// Set DMA channel operation mode register
	outb(0x54, TRID_REG(trident, LEGACY_DMAR11));

	// Set channel buffer Address
	voice->LBA = runtime->dma_addr;
	outl(voice->LBA, TRID_REG(trident, LEGACY_DMAR0));
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;

	// set ESO
	ESO_bytes = snd_pcm_lib_buffer_bytes(substream) - 1;
	outb((ESO_bytes & 0x00ff0000) >> 16, TRID_REG(trident, LEGACY_DMAR6));
	outw((ESO_bytes & 0x0000ffff), TRID_REG(trident, LEGACY_DMAR4));
	ESO_bytes++;

	// Set channel sample rate, 4.12 format
	val = ((unsigned int) 48000L << 12) / runtime->rate;
	outw(val, TRID_REG(trident, T4D_SBDELTA_DELTA_R));

	// Set channel interrupt blk length
	if (snd_pcm_format_width(runtime->format) == 16) {
		val = (unsigned short) ((ESO_bytes >> 1) - 1);
	} else {
		val = (unsigned short) (ESO_bytes - 1);
	}

	outl((val << 16) | val, TRID_REG(trident, T4D_SBBL_SBCL));

	// Right now, set format and start to run captureing, 
	// continuous run loop enable.
	trident->bDMAStart = 0x19;	// 0001 1001b

	if (snd_pcm_format_width(runtime->format) == 16)
		trident->bDMAStart |= 0x80;
	if (snd_pcm_format_signed(runtime->format))
		trident->bDMAStart |= 0x20;
	if (runtime->channels > 1)
		trident->bDMAStart |= 0x40;

	// Prepare capture intr channel

	voice->Delta = snd_trident_convert_rate(runtime->rate);
	voice->Delta--;	/* hack, make the duration a bit slower */

	// Set voice parameters
	voice->CSO = 0;
	voice->ESO = (runtime->period_size * 2) - 1;	/* in samples */
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->RVol = 0x7f;
	voice->CVol = 0x7f;
	voice->GVSel = 1;
	voice->Pan = 0x7f;		/* mute */
	voice->Vol = 0x3ff;		/* mute */
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Attribute = 0;

	snd_trident_write_voice_regs(trident, voice);

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_hw_params
  
   Description: Set the hardware parameters for the capture device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_hw_params(snd_pcm_substream_t * substream,
						snd_pcm_hw_params_t * hw_params)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;

	/* voice management */

	if (params_buffer_size(hw_params) / 2 != params_buffer_size(hw_params)) {
		if (evoice == NULL) {
			evoice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
			if (evoice == NULL)
				return -ENOMEM;
			voice->extra = evoice;
			evoice->substream = substream;
		}
	} else {
		if (evoice != NULL) {
			snd_trident_free_voice(trident, evoice);
			voice->extra = evoice = NULL;
		}
	}

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_hw_free
  
   Description: Release the hardware resources for the capture device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_hw_free(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;

	snd_pcm_lib_free_pages(substream);
	if (evoice != NULL) {
		snd_trident_free_voice(trident, evoice);
		voice->extra = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_prepare
  
   Description: Prepare capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_prepare(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);

	voice->LBA = runtime->dma_addr;
	voice->Delta = snd_trident_convert_adc_rate(runtime->rate);

	// Set voice parameters
	voice->CSO = 0;
	voice->ESO = runtime->buffer_size - 1;		/* in samples */
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 0;
	voice->RVol = 0;
	voice->CVol = 0;
	voice->GVSel = 1;
	voice->Pan = T4D_DEFAULT_PCM_PAN;
	voice->Vol = 0;
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;

	voice->Attribute = (2 << (30-16)) |
			   (2 << (26-16)) |
			   (2 << (24-16)) |
			   (1 << (23-16));

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = snd_trident_convert_rate(runtime->rate);
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) - 1; /* in samples */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = 0;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
		evoice->Attribute = 0;
		snd_trident_write_voice_regs(trident, evoice);
	}
	
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_hw_params
  
   Description: Set the hardware parameters for the foldback device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_hw_params(snd_pcm_substream_t * substream,
					  snd_pcm_hw_params_t * hw_params)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	unsigned long flags;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0 && trident->tlb.entries) {
		if (voice->memblk)
			snd_trident_free_pages(trident, voice->memblk);
		spin_lock_irqsave(&trident->reg_lock, flags);
		voice->memblk = snd_trident_alloc_pages(trident, runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		if (voice->memblk == NULL)
			return -ENOMEM;
	}

	/* voice management */

	if (params_buffer_size(hw_params) / 2 != params_buffer_size(hw_params)) {
		if (evoice == NULL) {
			evoice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
			if (evoice == NULL)
				return -ENOMEM;
			voice->extra = evoice;
			evoice->substream = substream;
		}
	} else {
		if (evoice != NULL) {
			snd_trident_free_voice(trident, evoice);
			voice->extra = evoice = NULL;
		}
	}

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_hw_free
  
   Description: Release the hardware resources for the foldback device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_hw_free(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;

	if (trident->tlb.entries) {
		if (voice->memblk) {
			snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = NULL;
		}
	}
	snd_pcm_lib_free_pages(substream);
	if (evoice != NULL) {
		snd_trident_free_voice(trident, evoice);
		voice->extra = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_prepare
  
   Description: Prepare foldback capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_prepare(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	snd_trident_voice_t *evoice = voice->extra;
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);

	/* Set channel buffer Address */
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;
	else
		voice->LBA = runtime->dma_addr;

	/* set target ESO for channel */
	voice->ESO = runtime->buffer_size - 1;	/* in samples */

	/* set sample rate */
	voice->Delta = 0x1000;

	voice->CSO = 0;
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->RVol = 0x7f;
	voice->CVol = 0x7f;
	voice->GVSel = 1;
	voice->Pan = 0x7f;	/* mute */
	voice->Vol = 0x3ff;	/* mute */
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Attribute = 0;

	/* set up capture channel */
	outb(((voice->number & 0x3f) | 0x80), TRID_REG(trident, T4D_RCI + voice->foldback_chan));

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = voice->Delta;
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) - 1; /* in samples */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = trident->device == TRIDENT_DEVICE_ID_SI7018 ? 0 : 1;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
		evoice->Attribute = 0;
		snd_trident_write_voice_regs(trident, evoice);
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_hw_params
  
   Description: Set the hardware parameters for the spdif device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_hw_params(snd_pcm_substream_t * substream,
				       snd_pcm_hw_params_t * hw_params)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned long flags;
	unsigned int old_bits = 0, change = 0;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0 && trident->tlb.entries) {
		if (voice->memblk)
			snd_trident_free_pages(trident, voice->memblk);
		spin_lock_irqsave(&trident->reg_lock, flags);
		voice->memblk = snd_trident_alloc_pages(trident, runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		if (voice->memblk == NULL)
			return -ENOMEM;
	}

	/* prepare SPDIF channel */
	spin_lock_irqsave(&trident->reg_lock, flags);
	old_bits = trident->spdif_pcm_bits;
	if (old_bits & IEC958_AES0_PROFESSIONAL)
		trident->spdif_pcm_bits &= ~IEC958_AES0_PRO_FS;
	else
		trident->spdif_pcm_bits &= ~(IEC958_AES3_CON_FS << 24);
	if (params_rate(hw_params) >= 48000) {
		trident->spdif_pcm_ctrl = 0x3c;	// 48000 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_48000 :
				(IEC958_AES3_CON_FS_48000 << 24);
	}
	else if (params_rate(hw_params) >= 44100) {
		trident->spdif_pcm_ctrl = 0x3e;	// 44100 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_44100 :
				(IEC958_AES3_CON_FS_44100 << 24);
	}
	else {
		trident->spdif_pcm_ctrl = 0x3d;	// 32000 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_32000 :
				(IEC958_AES3_CON_FS_32000 << 24);
	}
	change = old_bits != trident->spdif_pcm_bits;
	spin_unlock_irqrestore(&trident->reg_lock, flags);

	if (change)
		snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE, &trident->spdif_pcm_ctl->id);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_hw_free
  
   Description: Release the hardware resources for the spdif device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_hw_free(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;

	if (trident->tlb.entries) {
		if (voice->memblk) {
			snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = NULL;
		}
	}
	snd_pcm_lib_free_pages(substream);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_prepare
  
   Description: Prepare SPDIF device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_prepare(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned int RESO, LBAO;
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);	

	/* set delta (rate) value */
	voice->Delta = snd_trident_convert_rate(runtime->rate);

	/* surrogate IRQ voice may get out of sync with output stream
	 * at 32000 Hz so make it fractionally slower */
	voice->Delta--;

	/* set Loop Back Address */
	LBAO = runtime->dma_addr;
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;
	else
		voice->LBA = LBAO;

	/* set target ESO for channel */
	RESO = runtime->buffer_size - 1;
	voice->ESO = (runtime->period_size * 2) - 1;

	/* set ctrl mode */
	voice->CTRL = snd_trident_control_mode(substream);

	voice->FMC = 3;
	voice->RVol = 0x7f;
	voice->CVol = 0x7f;
	voice->GVSel = 1;
	voice->Pan = 0x7f;
	voice->Vol = 0x3ff;
	voice->EC = 0;
	voice->CSO = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Attribute = 0;

	/* prepare surrogate IRQ channel */
	snd_trident_write_voice_regs(trident, voice);

	outw((RESO & 0xffff), TRID_REG(trident, NX_SPESO));
	outb((RESO >> 16), TRID_REG(trident, NX_SPESO + 2));
	outl((LBAO & 0xfffffffc), TRID_REG(trident, NX_SPLBA));
	outw((voice->CSO & 0xffff), TRID_REG(trident, NX_SPCTRL_SPCSO));
	outb((voice->CSO >> 16), TRID_REG(trident, NX_SPCTRL_SPCSO + 2));

	// set SPDIF setting
	outb(trident->spdif_pcm_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));

	spin_unlock_irqrestore(&trident->reg_lock, flags);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_trigger
  
   Description: Start/stop devices
  
   Parameters:  substream  - PCM substream class
   		cmd	- trigger command (STOP, GO)
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_trigger(snd_pcm_substream_t *substream,
			       int cmd)
				    
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_substream_t *s;
	unsigned int what, whati, capture_flag, spdif_flag;
	snd_trident_voice_t *voice, *evoice;
	unsigned int val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		what = whati = capture_flag = spdif_flag = 0;
		s = substream;
		val = inl(TRID_REG(trident, T4D_STIMER)) & 0x00ffffff;
		do {
			if ((trident_t *) _snd_pcm_chip(s->pcm) == trident) {
				voice = (snd_trident_voice_t *) s->runtime->private_data;
				evoice = voice->extra;
				what |= 1 << (voice->number & 0x1f);
				if (evoice == NULL) {
					whati |= 1 << (voice->number & 0x1f);
				} else {
					what |= 1 << (evoice->number & 0x1f);
					whati |= 1 << (evoice->number & 0x1f);
				}
				if ((voice->running = (cmd == SNDRV_PCM_TRIGGER_START)) != 0)
					voice->stimer = val;
				snd_pcm_trigger_done(s, substream);
				if (voice->capture)
					capture_flag = 1;
				if (voice->spdif)
					spdif_flag = 1;
			}
			s = s->link_next;
		} while (s != substream);
		spin_lock(&trident->reg_lock);
		if (spdif_flag) {
			outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));
			outb(trident->spdif_pcm_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
		}
		if (cmd == SNDRV_PCM_TRIGGER_STOP)
			outl(what, TRID_REG(trident, T4D_STOP_B));
		val = inl(TRID_REG(trident, T4D_AINTEN_B));
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			val |= whati;
		} else {
			val &= ~whati;
		}
		outl(val, TRID_REG(trident, T4D_AINTEN_B));
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			outl(what, TRID_REG(trident, T4D_START_B));
		
			if (capture_flag && trident->device != TRIDENT_DEVICE_ID_SI7018)
				outb(trident->bDMAStart, TRID_REG(trident, T4D_SBCTRL_SBE2R_SBDD));
		} else {
			if (capture_flag && trident->device != TRIDENT_DEVICE_ID_SI7018)
				outb(0x00, TRID_REG(trident, T4D_SBCTRL_SBE2R_SBDD));
		}
		spin_unlock(&trident->reg_lock);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_pointer
  
   Description: This routine return the playback position
                
   Parameters:	substream  - PCM substream class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_playback_pointer(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned int cso;

	if (!voice->running)
		return 0;

	spin_lock(&trident->reg_lock);

	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));

	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		cso = inw(TRID_REG(trident, CH_DX_CSO_ALPHA_FMS + 2));
	} else {		// ID_4DWAVE_NX
		cso = (unsigned int) inl(TRID_REG(trident, CH_NX_DELTA_CSO)) & 0x00ffffff;
	}
	if (++cso > runtime->buffer_size)
		cso = runtime->buffer_size;

	spin_unlock(&trident->reg_lock);

	return cso;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_pointer
  
   Description: This routine return the capture position
                
   Paramters:   pcm1    - PCM device class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_capture_pointer(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned int result, cso;

	if (!voice->running)
		return 0;

	spin_lock(&trident->reg_lock);

	result = inw(TRID_REG(trident, T4D_SBBL_SBCL));
	if (runtime->channels > 1)
		result >>= 1;
	result = cso = runtime->buffer_size - result;

	/* update CSO, because voice & capture DMA is running out of sync */
	cso %= runtime->period_size;
	snd_trident_write_cso_reg(trident, voice, cso);

	spin_unlock(&trident->reg_lock);

	// printk("capture result = 0x%x, cso = 0x%x\n", result, cso);

	return result;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_pointer
  
   Description: This routine return the SPDIF playback position
                
   Parameters:	substream  - PCM substream class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_spdif_pointer(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	unsigned int result, cso;

	if (!voice->running)
		return 0;

	spin_lock(&trident->reg_lock);

	result = cso = inl(TRID_REG(trident, NX_SPCTRL_SPCSO)) & 0x00ffffff;

	/* update CSO, because voice & capture DMA is running out of sync */
	cso %= runtime->period_size;
	snd_trident_stop_voice(trident, voice->number);
	snd_trident_write_cso_reg(trident, voice, cso);
	snd_trident_start_voice(trident, voice->number);

	spin_unlock(&trident->reg_lock);

	return result;
}

/*
 *  Playback support device description
 */

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_trident_playback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(256*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(256*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

/*
 *  Capture support device description
 */

static snd_pcm_hardware_t snd_trident_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(128*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

/*
 *  Foldback capture support device description
 */

static snd_pcm_hardware_t snd_trident_foldback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_48000,
/*	rate_min:	  */	48000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	2,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(128*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

/*
 *  SPDIF playback support device description
 */

static snd_pcm_hardware_t snd_trident_spdif =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000),
/*	rate_min:	  */	32000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	2,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(128*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_trident_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	formats:		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(256*1024),
	period_bytes_min:	64,
	period_bytes_max:	(256*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

/*
 *  Capture support device description
 */

static snd_pcm_hardware_t snd_trident_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	formats:		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

/*
 *  Foldback capture support device description
 */

static snd_pcm_hardware_t snd_trident_foldback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_48000,
	rate_min:		48000,
	rate_max:		48000,
	channels_min:		2,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

/*
 *  SPDIF playback support device description
 */

static snd_pcm_hardware_t snd_trident_spdif =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000),
	rate_min:		32000,
	rate_max:		48000,
	channels_min:		2,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

static void snd_trident_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	unsigned long flags;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;
	trident_t *trident;

	if (voice) {
		trident = voice->trident;
		spin_lock_irqsave(&trident->reg_lock, flags);
		snd_trident_free_voice(trident, voice);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
	}
}

static int snd_trident_playback_open(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice;

	spin_lock_irq(&trident->reg_lock);
	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL) {
		spin_unlock_irq(&trident->reg_lock);
		return -EAGAIN;
	}
	spin_unlock_irq(&trident->reg_lock);
	snd_trident_pcm_mixer_build(trident, voice, substream);
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_playback;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_close
  
   Description: This routine will close the 4DWave playback device. For now 
                we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_playback_close(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_trident_voice_t *voice = (snd_trident_voice_t *) runtime->private_data;

	snd_trident_pcm_mixer_free(trident, voice, substream);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_open
  
   Description: This routine will open the 4DWave SPDIF device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_open(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_trident_voice_t *voice;
	snd_pcm_runtime_t *runtime = substream->runtime;
	
	spin_lock_irq(&trident->reg_lock);
	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL) {
		spin_unlock_irq(&trident->reg_lock);
		return -EAGAIN;
	}
	voice->spdif = 1;
	voice->substream = substream;
	trident->spdif_pcm_bits = trident->spdif_bits;
	spin_unlock_irq(&trident->reg_lock);

	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_spdif;

	trident->spdif_pcm_ctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &trident->spdif_pcm_ctl->id);

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}


/*---------------------------------------------------------------------------
   snd_trident_spdif_close
  
   Description: This routine will close the 4DWave SPDIF device.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_close(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);

	spin_lock_irq(&trident->reg_lock);
	// restore default SPDIF setting
	outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
	spin_unlock_irq(&trident->reg_lock);
	trident->spdif_pcm_ctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &trident->spdif_pcm_ctl->id);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_open
  
   Description: This routine will open the 4DWave capture device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag

  ---------------------------------------------------------------------------*/

static int snd_trident_capture_open(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_trident_voice_t *voice;
	snd_pcm_runtime_t *runtime = substream->runtime;

	spin_lock_irq(&trident->reg_lock);
	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL) {
		spin_unlock_irq(&trident->reg_lock);
		return -EAGAIN;
	}
	voice->capture = 1;
	spin_unlock_irq(&trident->reg_lock);
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_capture;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_close
  
   Description: This routine will close the 4DWave capture device. For now 
                we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_capture_close(snd_pcm_substream_t * substream)
{
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_open
  
   Description: This routine will open the 4DWave foldback capture device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag

  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_open(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_trident_voice_t *voice;
	snd_pcm_runtime_t *runtime = substream->runtime;

	spin_lock_irq(&trident->reg_lock);
	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL) {
		spin_unlock_irq(&trident->reg_lock);
		return -EAGAIN;
	}
	if (trident->tlb.entries) {
		voice->memblk = snd_trident_alloc_pages(trident, runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);
		if (voice->memblk == NULL) {
			snd_trident_free_voice(trident, voice);
			spin_unlock_irq(&trident->reg_lock);
			return -ENOMEM;
		}
	}
	voice->substream = substream;
	voice->foldback_chan = substream->number;
	spin_unlock_irq(&trident->reg_lock);
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_foldback;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_close
  
   Description: This routine will close the 4DWave foldback capture device. 
		For now we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_foldback_close(snd_pcm_substream_t * substream)
{
	trident_t *trident = snd_pcm_substream_chip(substream);
	snd_trident_voice_t *voice;
	snd_pcm_runtime_t *runtime = substream->runtime;
	voice = (snd_trident_voice_t *) runtime->private_data;
	
	/* stop capture channel */
	spin_lock_irq(&trident->reg_lock);
	outb(0x00, TRID_REG(trident, T4D_RCI + voice->foldback_chan));
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   PCM operations
  ---------------------------------------------------------------------------*/

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_trident_playback_ops = {
	snd_trident_playback_open,
	snd_trident_playback_close,
	snd_trident_ioctl,
	snd_trident_playback_hw_params,
	snd_trident_playback_hw_free,
	snd_trident_playback_prepare,
	snd_trident_trigger,
	snd_trident_playback_pointer,0,0
};

static snd_pcm_ops_t snd_trident_capture_ops = {
	snd_trident_capture_open,
	snd_trident_capture_close,
	snd_trident_ioctl,
	snd_trident_capture_hw_params,
	snd_trident_capture_hw_free,
	snd_trident_capture_prepare,
	snd_trident_trigger,
	snd_trident_capture_pointer,0,0
};

static snd_pcm_ops_t snd_trident_si7018_capture_ops = {
	snd_trident_capture_open,
	snd_trident_capture_close,
	snd_trident_ioctl,
	snd_trident_si7018_capture_hw_params,
	snd_trident_si7018_capture_hw_free,
	snd_trident_si7018_capture_prepare,
	snd_trident_trigger,
	snd_trident_playback_pointer,0,0
};

static snd_pcm_ops_t snd_trident_foldback_ops = {
	snd_trident_foldback_open,
	snd_trident_foldback_close,
	snd_trident_ioctl,
	snd_trident_foldback_hw_params,
	snd_trident_foldback_hw_free,
	snd_trident_foldback_prepare,
	snd_trident_trigger,
	snd_trident_playback_pointer,0,0
};

static snd_pcm_ops_t snd_trident_spdif_ops = {
	snd_trident_spdif_open,
	snd_trident_spdif_close,
	snd_trident_ioctl,
	snd_trident_spdif_hw_params,
	snd_trident_spdif_hw_free,
	snd_trident_spdif_prepare,
	snd_trident_trigger,
	snd_trident_spdif_pointer,0,0
};
#else
static snd_pcm_ops_t snd_trident_playback_ops = {
	open:		snd_trident_playback_open,
	close:		snd_trident_playback_close,
	ioctl:		snd_trident_ioctl,
	hw_params:	snd_trident_playback_hw_params,
	hw_free:	snd_trident_playback_hw_free,
	prepare:	snd_trident_playback_prepare,
	trigger:	snd_trident_trigger,
	pointer:	snd_trident_playback_pointer,
};

static snd_pcm_ops_t snd_trident_capture_ops = {
	open:		snd_trident_capture_open,
	close:		snd_trident_capture_close,
	ioctl:		snd_trident_ioctl,
	hw_params:	snd_trident_capture_hw_params,
	hw_free:	snd_trident_capture_hw_free,
	prepare:	snd_trident_capture_prepare,
	trigger:	snd_trident_trigger,
	pointer:	snd_trident_capture_pointer,
};

static snd_pcm_ops_t snd_trident_si7018_capture_ops = {
	open:		snd_trident_capture_open,
	close:		snd_trident_capture_close,
	ioctl:		snd_trident_ioctl,
	hw_params:	snd_trident_si7018_capture_hw_params,
	hw_free:	snd_trident_si7018_capture_hw_free,
	prepare:	snd_trident_si7018_capture_prepare,
	trigger:	snd_trident_trigger,
	pointer:	snd_trident_playback_pointer,
};

static snd_pcm_ops_t snd_trident_foldback_ops = {
	open:		snd_trident_foldback_open,
	close:		snd_trident_foldback_close,
	ioctl:		snd_trident_ioctl,
	hw_params:	snd_trident_foldback_hw_params,
	hw_free:	snd_trident_foldback_hw_free,
	prepare:	snd_trident_foldback_prepare,
	trigger:	snd_trident_trigger,
	pointer:	snd_trident_playback_pointer,
};

static snd_pcm_ops_t snd_trident_spdif_ops = {
	open:		snd_trident_spdif_open,
	close:		snd_trident_spdif_close,
	ioctl:		snd_trident_ioctl,
	hw_params:	snd_trident_spdif_hw_params,
	hw_free:	snd_trident_spdif_hw_free,
	prepare:	snd_trident_spdif_prepare,
	trigger:	snd_trident_trigger,
	pointer:	snd_trident_spdif_pointer,
};
#endif

/*---------------------------------------------------------------------------
   snd_trident_pcm_free
  
   Description: This routine release the 4DWave private data.
                
   Paramters:   private_data - pointer to 4DWave device info.

   Returns:     None
  
  ---------------------------------------------------------------------------*/
static void snd_trident_pcm_free(snd_pcm_t *pcm)
{
	trident_t *trident = snd_magic_cast(trident_t, pcm->private_data, return);
	trident->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static void snd_trident_foldback_pcm_free(snd_pcm_t *pcm)
{
	trident_t *trident = snd_magic_cast(trident_t, pcm->private_data, return);
	trident->foldback = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static void snd_trident_spdif_pcm_free(snd_pcm_t *pcm)
{
	trident_t *trident = snd_magic_cast(trident_t, pcm->private_data, return);
	trident->spdif = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

/*---------------------------------------------------------------------------
   snd_trident_pcm
  
   Description: This routine registers the 4DWave device for PCM support.
                
   Paramters:   trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_pcm(trident_t * trident, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(trident->card, "trident_dx_nx", device, trident->ChanPCM, 1, &pcm)) < 0)
		return err;

	pcm->private_data = trident;
	pcm->private_free = snd_trident_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				trident->device != TRIDENT_DEVICE_ID_SI7018 ?
					&snd_trident_capture_ops :
					&snd_trident_si7018_capture_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "Trident 4DWave");
	trident->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(trident->pci, pcm, 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_pcm
  
   Description: This routine registers the 4DWave device for foldback PCM support.
                
   Paramters:   trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_foldback_pcm(trident_t * trident, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *foldback;
	int err;
	int num_chan = 3;
	snd_pcm_substream_t *substream;

	if (rpcm)
		*rpcm = NULL;
	if (trident->device == TRIDENT_DEVICE_ID_NX)
		num_chan = 4;
	if ((err = snd_pcm_new(trident->card, "trident_dx_nx", device, 0, num_chan, &foldback)) < 0)
		return err;

	foldback->private_data = trident;
	foldback->private_free = snd_trident_foldback_pcm_free;
	snd_pcm_set_ops(foldback, SNDRV_PCM_STREAM_CAPTURE, &snd_trident_foldback_ops);
	foldback->info_flags = 0;
	strcpy(foldback->name, "Trident 4DWave");
	substream = foldback->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	strcpy(substream->name, "Front Mixer");
	substream = substream->next;
	strcpy(substream->name, "Reverb Mixer");
	substream = substream->next;
	strcpy(substream->name, "Chorus Mixer");
	if (num_chan == 4) {
		substream = substream->next;
		strcpy(substream->name, "Second AC'97 ADC");
	}
	trident->foldback = foldback;

	snd_pcm_lib_preallocate_pci_pages_for_all(trident->pci, foldback, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = foldback;
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif
  
   Description: This routine registers the 4DWave-NX device for SPDIF support.
                
   Paramters:   trident - pointer to target device class for 4DWave-NX.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_spdif_pcm(trident_t * trident, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *spdif;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(trident->card, "trident_dx_nx IEC958", device, 1, 0, &spdif)) < 0)
		return err;

	spdif->private_data = trident;
	spdif->private_free = snd_trident_spdif_pcm_free;
	snd_pcm_set_ops(spdif, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_spdif_ops);
	spdif->info_flags = 0;
	strcpy(spdif->name, "Trident 4DWave IEC958");
	trident->spdif = spdif;

	snd_pcm_lib_preallocate_pci_pages_for_all(trident->pci, spdif, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = spdif;
	return 0;
}

/*
 *  Mixer part
 */


/*---------------------------------------------------------------------------
    snd_trident_spdif_control

    Description: enable/disable S/PDIF out from ac97 mixer
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_trident_spdif_control_get(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&trident->reg_lock, flags);
	val = trident->spdif_ctrl;
	ucontrol->value.integer.value[0] = val == kcontrol->private_value;
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

static int snd_trident_spdif_control_put(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char val;
	int change;

	val = ucontrol->value.integer.value[0] ? (unsigned char) kcontrol->private_value : 0x00;
	spin_lock_irqsave(&trident->reg_lock, flags);
	/* S/PDIF C Channel bits 0-31 : 48khz, SCMS disabled */
	change = trident->spdif_ctrl != val;
	trident->spdif_ctrl = val;
	if ((inb(TRID_REG(trident, NX_SPCTRL_SPCSO + 3)) & 0x10) == 0) {
		outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
		outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	}
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_spdif_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),0,0,
	snd_trident_spdif_control_info,
	snd_trident_spdif_control_get,
	snd_trident_spdif_control_put,
	0x28,
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),
	info:		snd_trident_spdif_control_info,
	get:		snd_trident_spdif_control_get,
	put:		snd_trident_spdif_control_put,
	private_value:  0x28,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_default

    Description: put/get the S/PDIF default settings
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_default_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_default_get(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (trident->spdif_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (trident->spdif_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (trident->spdif_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (trident->spdif_bits >> 24) & 0xff;
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

static int snd_trident_spdif_default_put(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = trident->spdif_bits != val;
	trident->spdif_bits = val;
	if ((inb(TRID_REG(trident, NX_SPCTRL_SPCSO + 3)) & 0x10) == 0)
		outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_spdif_default =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),0,0,
	snd_trident_spdif_default_info,
	snd_trident_spdif_default_get,
	snd_trident_spdif_default_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	info:		snd_trident_spdif_default_info,
	get:		snd_trident_spdif_default_get,
	put:		snd_trident_spdif_default_put
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_mask

    Description: put/get the S/PDIF mask
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_mask_get(snd_kcontrol_t * kcontrol,
				      snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static snd_kcontrol_new_t snd_trident_spdif_mask =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_trident_spdif_mask_info,
	snd_trident_spdif_mask_get,0,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	info:		snd_trident_spdif_mask_info,
	get:		snd_trident_spdif_mask_get,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_stream

    Description: put/get the S/PDIF stream settings
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_stream_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_stream_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (trident->spdif_pcm_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (trident->spdif_pcm_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (trident->spdif_pcm_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (trident->spdif_pcm_bits >> 24) & 0xff;
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

static int snd_trident_spdif_stream_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = trident->spdif_pcm_bits != val;
	trident->spdif_pcm_bits = val;
	if (trident->spdif != NULL)
		outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_spdif_stream =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_trident_spdif_stream_info,
	snd_trident_spdif_stream_get,
	snd_trident_spdif_stream_put,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	info:		snd_trident_spdif_stream_info,
	get:		snd_trident_spdif_stream_get,
	put:		snd_trident_spdif_stream_put
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_ac97_control

    Description: enable/disable rear path for ac97
  ---------------------------------------------------------------------------*/

static int snd_trident_ac97_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_trident_ac97_control_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&trident->reg_lock, flags);
	val = trident->ac97_ctrl = inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	ucontrol->value.integer.value[0] = (val & (1 << kcontrol->private_value)) ? 1 : 0;
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

static int snd_trident_ac97_control_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char val;
	int change = 0;

	spin_lock_irqsave(&trident->reg_lock, flags);
	val = trident->ac97_ctrl = inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	val &= ~(1 << kcontrol->private_value);
	if (ucontrol->value.integer.value[0])
		val |= 1 << kcontrol->private_value;
	change = val != trident->ac97_ctrl;
	trident->ac97_ctrl = val;
	outl(trident->ac97_ctrl = val, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_ac97_rear_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Rear Path",0,0,
	snd_trident_ac97_control_info,
	snd_trident_ac97_control_get,
	snd_trident_ac97_control_put,
	4,
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "Rear Path",
	info:		snd_trident_ac97_control_info,
	get:		snd_trident_ac97_control_get,
	put:		snd_trident_ac97_control_put,
	private_value:  4,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_vol_control

    Description: wave & music volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_vol_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_trident_vol_control_get(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	val = trident->musicvol_wavevol;
	ucontrol->value.integer.value[0] = 255 - ((val >> kcontrol->private_value) & 0xff);
	ucontrol->value.integer.value[1] = 255 - ((val >> (kcontrol->private_value + 8)) & 0xff);
	return 0;
}

static int snd_trident_vol_control_put(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	spin_lock_irqsave(&trident->reg_lock, flags);
	val = trident->musicvol_wavevol;
	val &= ~(0xffff << kcontrol->private_value);
	val |= ((255 - (ucontrol->value.integer.value[0] & 0xff)) |
	        ((255 - (ucontrol->value.integer.value[1] & 0xff)) << 8)) << kcontrol->private_value;
	change = val != trident->musicvol_wavevol;
	outl(trident->musicvol_wavevol = val, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_vol_music_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Music Playback Volume",0,0,
	snd_trident_vol_control_info,
	snd_trident_vol_control_get,
	snd_trident_vol_control_put,
	16,
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "Music Playback Volume",
	info:		snd_trident_vol_control_info,
	get:		snd_trident_vol_control_get,
	put:		snd_trident_vol_control_put,
	private_value:  16,
#endif
};

static snd_kcontrol_new_t snd_trident_vol_wave_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Wave Playback Volume",0,0,
	snd_trident_vol_control_info,
	snd_trident_vol_control_get,
	snd_trident_vol_control_put,
	0,
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "Wave Playback Volume",
	info:		snd_trident_vol_control_info,
	get:		snd_trident_vol_control_get,
	put:		snd_trident_vol_control_put,
	private_value:  0,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_vol_control

    Description: PCM front volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_vol_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		uinfo->value.integer.max = 1023;
	return 0;
}

static int snd_trident_pcm_vol_control_get(snd_kcontrol_t * kcontrol,
					   snd_ctl_elem_value_t * ucontrol)
{
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;

	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		ucontrol->value.integer.value[0] = 1023 - mix->vol;
	} else {
		ucontrol->value.integer.value[0] = 255 - (mix->vol>>2);
	}
	return 0;
}

static int snd_trident_pcm_vol_control_put(snd_kcontrol_t * kcontrol,
					   snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;
	unsigned int val;
	int change = 0;

	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		val = 1023 - (ucontrol->value.integer.value[0] & 1023);
	} else {
		val = (255 - (ucontrol->value.integer.value[0] & 255)) << 2;
	}
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = val != mix->vol;
	mix->vol = val;
	if (mix->voice != NULL)
		snd_trident_write_vol_reg(trident, mix->voice, val);
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_pcm_vol_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"PCM Front Playback Volume",0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_trident_pcm_vol_control_info,
	snd_trident_pcm_vol_control_get,
	snd_trident_pcm_vol_control_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "PCM Front Playback Volume",
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	info:		snd_trident_pcm_vol_control_info,
	get:		snd_trident_pcm_vol_control_get,
	put:		snd_trident_pcm_vol_control_put,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_pan_control

    Description: PCM front pan control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_pan_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_pan_control_get(snd_kcontrol_t * kcontrol,
					   snd_ctl_elem_value_t * ucontrol)
{
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;

	ucontrol->value.integer.value[0] = mix->pan;
	if (ucontrol->value.integer.value[0] & 0x40) {
		ucontrol->value.integer.value[0] = (0x3f - (ucontrol->value.integer.value[0] & 0x3f));
	} else {
		ucontrol->value.integer.value[0] |= 0x40;
	}
	return 0;
}

static int snd_trident_pcm_pan_control_put(snd_kcontrol_t * kcontrol,
					   snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;
	unsigned char val;
	int change = 0;

	if (ucontrol->value.integer.value[0] & 0x40)
		val = ucontrol->value.integer.value[0] & 0x3f;
	else
		val = (0x3f - (ucontrol->value.integer.value[0] & 0x3f)) | 0x40;
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = val != mix->pan;
	mix->pan = val;
	if (mix->voice != NULL)
		snd_trident_write_pan_reg(trident, mix->voice, val);
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_pcm_pan_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"PCM Pan Playback Control",0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_trident_pcm_pan_control_info,
	snd_trident_pcm_pan_control_get,
	snd_trident_pcm_pan_control_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "PCM Pan Playback Control",
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	info:		snd_trident_pcm_pan_control_info,
	get:		snd_trident_pcm_pan_control_get,
	put:		snd_trident_pcm_pan_control_put,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_rvol_control

    Description: PCM reverb volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_rvol_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_rvol_control_get(snd_kcontrol_t * kcontrol,
					    snd_ctl_elem_value_t * ucontrol)
{
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;

	ucontrol->value.integer.value[0] = 127 - mix->rvol;
	return 0;
}

static int snd_trident_pcm_rvol_control_put(snd_kcontrol_t * kcontrol,
					    snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;
	unsigned short val;
	int change = 0;

	val = 0x7f - (ucontrol->value.integer.value[0] & 0x7f);
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = val != mix->rvol;
	mix->rvol = val;
	if (mix->voice != NULL)
		snd_trident_write_rvol_reg(trident, mix->voice, val);
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_pcm_rvol_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0, 
	"PCM Reverb Playback Volume",0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_trident_pcm_rvol_control_info,
	snd_trident_pcm_rvol_control_get,
	snd_trident_pcm_rvol_control_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "PCM Reverb Playback Volume",
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	info:		snd_trident_pcm_rvol_control_info,
	get:		snd_trident_pcm_rvol_control_get,
	put:		snd_trident_pcm_rvol_control_put,
#endif
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_cvol_control

    Description: PCM chorus volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_cvol_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_cvol_control_get(snd_kcontrol_t * kcontrol,
					    snd_ctl_elem_value_t * ucontrol)
{
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;

	ucontrol->value.integer.value[0] = 127 - mix->cvol;
	return 0;
}

static int snd_trident_pcm_cvol_control_put(snd_kcontrol_t * kcontrol,
					    snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	trident_t *trident = snd_kcontrol_chip(kcontrol);
	snd_trident_pcm_mixer_t *mix = (snd_trident_pcm_mixer_t *)kcontrol->private_value;
	unsigned short val;
	int change = 0;

	val = 0x7f - (ucontrol->value.integer.value[0] & 0x7f);
	spin_lock_irqsave(&trident->reg_lock, flags);
	change = val != mix->cvol;
	mix->cvol = val;
	if (mix->voice != NULL)
		snd_trident_write_cvol_reg(trident, mix->voice, val);
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_trident_pcm_cvol_control =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"PCM Chorus Playback Volume",0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_trident_pcm_cvol_control_info,
	snd_trident_pcm_cvol_control_get,
	snd_trident_pcm_cvol_control_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           "PCM Chorus Playback Volume",
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	info:		snd_trident_pcm_cvol_control_info,
	get:		snd_trident_pcm_cvol_control_get,
	put:		snd_trident_pcm_cvol_control_put,
#endif
};

static void snd_trident_notify_pcm_change1(snd_card_t * card, snd_kcontrol_t *kctl, int activate)
{
	snd_runtime_check(kctl != NULL, return);
	if (activate)
		kctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	else
		kctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &kctl->id);
}

static void snd_trident_notify_pcm_change(snd_card_t * card, snd_trident_pcm_mixer_t * tmix, int activate)
{
	snd_trident_notify_pcm_change1(card, tmix->ctl_vol, activate);
	snd_trident_notify_pcm_change1(card, tmix->ctl_pan, activate);
	snd_trident_notify_pcm_change1(card, tmix->ctl_rvol, activate);
	snd_trident_notify_pcm_change1(card, tmix->ctl_cvol, activate);
}

static int snd_trident_pcm_mixer_build(trident_t *trident, snd_trident_voice_t *voice, snd_pcm_substream_t *substream)
{
	snd_trident_pcm_mixer_t *tmix;

	snd_assert(trident != NULL && voice != NULL && substream != NULL, return -EINVAL);
	tmix = &trident->pcm_mixer[substream->number];
	tmix->voice = voice;
	tmix->vol = T4D_DEFAULT_PCM_VOL;
	tmix->pan = T4D_DEFAULT_PCM_PAN;
	tmix->rvol = T4D_DEFAULT_PCM_RVOL;
	tmix->cvol = T4D_DEFAULT_PCM_CVOL;
	snd_trident_notify_pcm_change(trident->card, tmix, 1);
	return 0;
}

static int snd_trident_pcm_mixer_free(trident_t *trident, snd_trident_voice_t *voice, snd_pcm_substream_t *substream)
{
	snd_trident_pcm_mixer_t *tmix;

	snd_assert(trident != NULL && voice != NULL && substream != NULL, return -EINVAL);
	tmix = &trident->pcm_mixer[substream->number];
	tmix->voice = NULL;
	snd_trident_notify_pcm_change(trident->card, tmix, 0);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_mixer
  
   Description: This routine registers the 4DWave device for mixer support.
                
   Paramters:   trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

static int snd_trident_mixer(trident_t * trident, int pcm_spdif_device)
{
	ac97_t _ac97, *ac97;
	snd_card_t * card = trident->card;
	snd_kcontrol_t *kctl;
	snd_ctl_elem_value_t uctl;
	int idx, err;

	memset(&uctl, 0, sizeof(uctl));

	memset(&_ac97, 0, sizeof(_ac97));
	_ac97.write = snd_trident_codec_write;
	_ac97.read = snd_trident_codec_read;
	_ac97.private_data = trident;
	if ((err = snd_ac97_mixer(trident->card, &_ac97, &ac97)) < 0)
		return err;

	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_vol_wave_control, trident))) < 0)
			return err;
		kctl->put(kctl, &uctl);
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_vol_music_control, trident))) < 0)
			return err;
		kctl->put(kctl, &uctl);
		outl(trident->musicvol_wavevol = 0x00000000, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	} else {
		outl(trident->musicvol_wavevol = 0xffff0000, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	}

	for (idx = 0; idx < 32; idx++) {
		snd_trident_pcm_mixer_t *tmix;
		
		tmix = &trident->pcm_mixer[idx];
		tmix->voice = NULL;
		if ((kctl = tmix->ctl_vol = snd_ctl_new1(&snd_trident_pcm_vol_control, trident)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)tmix;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
		
		if ((kctl = tmix->ctl_pan = snd_ctl_new1(&snd_trident_pcm_pan_control, trident)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)tmix;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;

		if ((kctl = tmix->ctl_rvol = snd_ctl_new1(&snd_trident_pcm_rvol_control, trident)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)tmix;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;

		if ((kctl = tmix->ctl_cvol = snd_ctl_new1(&snd_trident_pcm_cvol_control, trident)) == NULL)
			return -ENOMEM;
		kctl->private_value = (long)tmix;
		kctl->id.index = idx;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
	}

	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_ac97_rear_control, trident))) < 0)
			return err;
		kctl->put(kctl, &uctl);
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_spdif_control, trident))) < 0)
			return err;
		kctl->put(kctl, &uctl);
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_spdif_default, trident))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_spdif_mask, trident))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_spdif_stream, trident))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		trident->spdif_pcm_ctl = kctl;
	}

	return 0;
}

/*  
 *  /proc interface
 */

static void snd_trident_proc_read(snd_info_entry_t *entry, 
				  snd_info_buffer_t * buffer)
{
	trident_t *trident = snd_magic_cast(trident_t, entry->private_data, return);
	char *s;

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_SI7018:
		s = "SiS 7018 Audio";
		break;
	case TRIDENT_DEVICE_ID_DX:
		s = "Trident 4DWave PCI DX";
		break;
	case TRIDENT_DEVICE_ID_NX:
		s = "Trident 4DWave PCI NX";
		break;
	default:
		s = "???";
	}
	snd_iprintf(buffer, "%s\n\n", s);
	snd_iprintf(buffer, "Spurious IRQs    : %d\n", trident->spurious_irq_count);
	snd_iprintf(buffer, "Spurious IRQ dlta: %d\n", trident->spurious_irq_max_delta);
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		snd_iprintf(buffer, "IEC958 Mixer Out : %s\n", trident->spdif_ctrl == 0x28 ? "on" : "off");
		snd_iprintf(buffer, "Rear Speakers    : %s\n", trident->ac97_ctrl & 0x00000010 ? "on" : "off");
		if (trident->tlb.entries) {
			snd_iprintf(buffer,"\nVirtual Memory\n");
			snd_iprintf(buffer, "Memory Maximum : %d\n", trident->tlb.memhdr->size);
			snd_iprintf(buffer, "Memory Used    : %d\n", trident->tlb.memhdr->used);
			snd_iprintf(buffer, "Memory Free    : %d\n", snd_util_mem_avail(trident->tlb.memhdr));
		}
	}
#ifdef CONFIG_SND_SEQUENCER
	snd_iprintf(buffer,"\nWavetable Synth\n");
	snd_iprintf(buffer, "Memory Maximum : %d\n", trident->synth.max_size);
	snd_iprintf(buffer, "Memory Used    : %d\n", trident->synth.current_size);
	snd_iprintf(buffer, "Memory Free    : %d\n", (trident->synth.max_size-trident->synth.current_size));
#endif
}

static void snd_trident_proc_init(trident_t * trident)
{
	snd_info_entry_t *entry;
	char *s = "trident";
	
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		s = "sis7018";
	if ((entry = snd_info_create_card_entry(trident->card, s, trident->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = trident;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_trident_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	trident->proc_entry = entry;
}

static void snd_trident_proc_done(trident_t * trident)
{
	if (trident->proc_entry) {
		snd_info_unregister(trident->proc_entry);
		trident->proc_entry = NULL;
	}
}

static int snd_trident_dev_free(snd_device_t *device)
{
	trident_t *trident = snd_magic_cast(trident_t, device->device_data, return -ENXIO);
	return snd_trident_free(trident);
}

/*---------------------------------------------------------------------------
   snd_trident_create
  
   Description: This routine will create the device specific class for
                the 4DWave card. It will also perform basic initialization.
                
   Paramters:   card  - which card to create
                pci   - interface to PCI bus resource info
                dma1ptr - playback dma buffer
                dma2ptr - capture dma buffer
                irqptr  -  interrupt resource info

   Returns:     4DWave device class private data
  
  ---------------------------------------------------------------------------*/

int snd_trident_create(snd_card_t * card,
		       struct pci_dev *pci,
		       int pcm_streams,
		       int pcm_spdif_device,
		       int max_wavetable_size,
		       trident_t ** rtrident)
{
	trident_t *trident;
	unsigned int i;
	int err;
	signed long end_time;
	snd_trident_voice_t *voice;
	snd_trident_pcm_mixer_t *tmix;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_trident_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_trident_dev_free,
	};
#endif
	*rtrident = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 30 bits */
	if (!pci_dma_supported(pci, 0x3fffffff)) {
		snd_printk("architecture does not support 30bit PCI busmaster DMA\n");
		return -ENXIO;
	}
	pci_set_dma_mask(pci, 0x3fffffff);
	
	trident = snd_magic_kcalloc(trident_t, 0, GFP_KERNEL);
	if (trident == NULL)
		return -ENOMEM;
	trident->device = (pci->vendor << 16) | pci->device;
	trident->card = card;
	trident->pci = pci;
	spin_lock_init(&trident->reg_lock);
	spin_lock_init(&trident->event_lock);
	spin_lock_init(&trident->voice_alloc);
	if (pcm_streams < 1)
		pcm_streams = 1;
	if (pcm_streams > 32)
		pcm_streams = 32;
	trident->ChanPCM = pcm_streams;
	if (max_wavetable_size < 0 )
		max_wavetable_size = 0;
	trident->synth.max_size = max_wavetable_size * 1024;
	trident->port = pci_resource_start(pci, 0);
	trident->irq = -1;

	trident->midi_port = TRID_REG(trident, T4D_MPU401_BASE);
	pci_set_master(pci);
	trident->port = pci_resource_start(pci, 0);

	if ((trident->res_port = request_region(trident->port, 0x100, "Trident Audio")) == NULL) {
		snd_trident_free(trident);
		snd_printk("unable to grab I/O region 0x%lx-0x%lx\n", trident->port, trident->port + 0x100 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_trident_interrupt, SA_INTERRUPT|SA_SHIRQ, "Trident Audio", (void *) trident)) {
		snd_trident_free(trident);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	trident->irq = pci->irq;

	/* allocate 16k-aligned TLB for NX cards */
	trident->tlb.entries = NULL;
	trident->tlb.buffer = NULL;
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		/* allocate and setup TLB page table */
		/* each entry has 4 bytes (physical PCI address) */
		/* TLB array must be aligned to 16kB !!! so we allocate
		   32kB region and correct offset when necessary */
		trident->tlb.buffer = snd_malloc_pci_pages(trident->pci, 2 * SNDRV_TRIDENT_MAX_PAGES * 4, &trident->tlb.buffer_dmaaddr);
		if (trident->tlb.buffer == NULL) {
			snd_trident_free(trident);
			snd_printk("unable to allocate TLB buffer\n");
			return -ENOMEM;
		}
		trident->tlb.entries = (unsigned int*)(((unsigned long)trident->tlb.buffer + SNDRV_TRIDENT_MAX_PAGES * 4 - 1) & ~(SNDRV_TRIDENT_MAX_PAGES * 4 - 1));
		trident->tlb.entries_dmaaddr = (trident->tlb.buffer_dmaaddr + SNDRV_TRIDENT_MAX_PAGES * 4 - 1) & ~(SNDRV_TRIDENT_MAX_PAGES * 4 - 1);
		/* allocate shadow TLB page table (virtual addresses) */
		trident->tlb.shadow_entries = (unsigned long *)vmalloc(SNDRV_TRIDENT_MAX_PAGES*sizeof(unsigned long));
		if (trident->tlb.shadow_entries == NULL) {
			snd_trident_free(trident);
			snd_printk("unable to allocate shadow TLB entries\n");
			return -ENOMEM;
		}
		/* allocate and setup silent page and initialise TLB entries */
		trident->tlb.silent_page = snd_malloc_pci_pages(trident->pci, SNDRV_TRIDENT_PAGE_SIZE, &trident->tlb.silent_page_dmaaddr);
#ifdef TARGET_OS2
		if (trident->tlb.silent_page == 0) {
#else
		if (trident->tlb.silent_page == 0UL) {
#endif
			snd_trident_free(trident);
			snd_printk("unable to allocate silent page\n");
			return -ENOMEM;
		}
		memset(trident->tlb.silent_page, 0, SNDRV_TRIDENT_PAGE_SIZE);
		for (i = 0; i < SNDRV_TRIDENT_MAX_PAGES; i++) {
			trident->tlb.entries[i] = trident->tlb.silent_page_dmaaddr & ~(SNDRV_TRIDENT_PAGE_SIZE-1);
			trident->tlb.shadow_entries[i] = (unsigned long)trident->tlb.silent_page;
		}

		/* use emu memory block manager code to manage tlb page allocation */
		trident->tlb.memhdr = snd_util_memhdr_new(SNDRV_TRIDENT_PAGE_SIZE * SNDRV_TRIDENT_MAX_PAGES);
		if (trident->tlb.memhdr == NULL) {
			snd_trident_free(trident);
			return -ENOMEM;
		}
		trident->tlb.memhdr->block_extra_size = sizeof(snd_trident_memblk_arg_t);
	}

	/* reset the legacy configuration and whole audio/wavetable block */
	if (trident->device == TRIDENT_DEVICE_ID_DX ||
	    trident->device == TRIDENT_DEVICE_ID_NX) {
		pci_write_config_dword(pci, 0x40, 0);	/* DDMA */
		pci_write_config_byte(pci, 0x44, 0);	/* ports */
		pci_write_config_byte(pci, 0x45, 0);	/* Legacy DMA */
		if (trident->device == TRIDENT_DEVICE_ID_DX) {
			pci_write_config_byte(pci, 0x46, 4); /* reset */
			udelay(100);
			pci_write_config_byte(pci, 0x46, 0); /* release reset */
			udelay(100);
		} else /* NX */ {
			pci_write_config_byte(pci, 0x46, 1); /* reset */
			udelay(100);
			pci_write_config_byte(pci, 0x46, 0); /* release reset */
			udelay(100);
		}
	}
	
	/* initialize chip */

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
		/* warm reset of the AC'97 codec */
		outl(0x00000001, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
		udelay(100);
		outl(0x00000000, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
		/* DAC on, disable SB IRQ and try to force ADC valid signal */
		trident->ac97_ctrl = 0x0000004a;
		outl(trident->ac97_ctrl, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
		/* wait, until the codec is ready */
		end_time = jiffies + (HZ * 3) / 4;
		do {
			if ((inl(TRID_REG(trident, DX_ACR2_AC97_COM_STAT)) & 0x0010) != 0)
				goto __dx_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} while (end_time - (signed long)jiffies >= 0);
		snd_printk("AC'97 codec ready error\n");
		snd_trident_free(trident);
		return -EIO;
	      __dx_ok:
		break;
	case TRIDENT_DEVICE_ID_NX:
		/* warm reset of the AC'97 codec */
		outl(0x00000001, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
		udelay(100);
		outl(0x00000000, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
		/* wait, until the codec is ready */
		end_time = jiffies + (HZ * 3) / 4;
		do {
			if ((inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT)) & 0x0008) != 0)
				goto __nx_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} while (end_time - (signed long)jiffies >= 0);
		snd_printk("AC'97 codec ready error\n");
		snd_trident_free(trident);
		return -EIO;
	      __nx_ok:
		/* DAC on */
		trident->ac97_ctrl = 0x00000002;
		outl(trident->ac97_ctrl, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
		/* disable SB IRQ */
		outl(NX_SB_IRQ_DISABLE, TRID_REG(trident, T4D_MISCINT));
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		/* warm reset of the AC'97 codec */
		i = inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		outl(i | 1, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		udelay(100);
		/* release reset (warm & cold) */
		outl(i & ~3, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		/* disable AC97 GPIO interrupt */
		outb(0x00, TRID_REG(trident, SI_AC97_GPIO));
		/* enable 64 channel mode */
		outl(BANK_B_EN, TRID_REG(trident, T4D_LFO_GC_CIR));
		break;
	}

	outl(0xffffffff, TRID_REG(trident, T4D_STOP_A));
	outl(0xffffffff, TRID_REG(trident, T4D_STOP_B));
	outl(0, TRID_REG(trident, T4D_AINTEN_A));
	outl(0, TRID_REG(trident, T4D_AINTEN_B));

	if ((err = snd_trident_mixer(trident, pcm_spdif_device)) < 0) {
		snd_trident_free(trident);
		return err;
	}
	
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		if (trident->tlb.entries != NULL) {
			/* enable virtual addressing via TLB */
			i = trident->tlb.entries_dmaaddr;
			i |= 0x00000001;
			outl(i, TRID_REG(trident, NX_TLBC));
		} else {
			outl(0, TRID_REG(trident, NX_TLBC));
		}
		/* initialize S/PDIF */
		trident->spdif_bits = trident->spdif_pcm_bits = SNDRV_PCM_DEFAULT_CON_SPDIF;
		outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
		outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	}

	/* initialise synth voices */
	for (i = 0; i < 64; i++) {
		voice = &trident->synth.voices[i];
		voice->number = i;
		voice->trident = trident;
	}
	/* initialize pcm mixer entries */
	for (i = 0; i < 32; i++) {
		tmix = &trident->pcm_mixer[i];
		tmix->vol = T4D_DEFAULT_PCM_VOL;
		tmix->pan = T4D_DEFAULT_PCM_PAN;
		tmix->rvol = T4D_DEFAULT_PCM_RVOL;
		tmix->cvol = T4D_DEFAULT_PCM_CVOL;
	}

	snd_trident_enable_eso(trident);

	snd_trident_proc_init(trident);
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, trident, &ops)) < 0) {
		snd_trident_free(trident);
		return err;
	}
	*rtrident = trident;
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_free
  
   Description: This routine will free the device specific class for
                the 4DWave card. 
                
   Paramters:   trident  - device specific private data for 4DWave card

   Returns:     None.
  
  ---------------------------------------------------------------------------*/

int snd_trident_free(trident_t *trident)
{
	snd_trident_disable_eso(trident);
	// Disable S/PDIF out
	if (trident->device == TRIDENT_DEVICE_ID_NX)
		outb(0x00, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	snd_trident_proc_done(trident);
	if (trident->tlb.buffer) {
		outl(0, TRID_REG(trident, NX_TLBC));
		if (trident->tlb.memhdr)
			snd_util_memhdr_free(trident->tlb.memhdr);
		if (trident->tlb.silent_page)
			snd_free_pci_pages(trident->pci, SNDRV_TRIDENT_PAGE_SIZE, trident->tlb.silent_page, trident->tlb.silent_page_dmaaddr);
		if (trident->tlb.shadow_entries)
			vfree(trident->tlb.shadow_entries);
		snd_free_pci_pages(trident->pci, 2 * SNDRV_TRIDENT_MAX_PAGES * 4, trident->tlb.buffer, trident->tlb.buffer_dmaaddr);
	}
	if (trident->irq >= 0)
		free_irq(trident->irq, (void *)trident);
	if (trident->res_port)
		release_resource(trident->res_port);
	snd_magic_kfree(trident);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_interrupt
  
   Description: ISR for Trident 4DWave device
                
   Paramters:   trident  - device specific private data for 4DWave card

   Problems:    It seems that Trident chips generates interrupts more than
                one time in special cases. The spurious interrupts are
                detected via sample timer (T4D_STIMER) and computing
                corresponding delta value. The limits are detected with
                the method try & fail so it is possible that it won't
                work on all computers. [jaroslav]

   Returns:     None.
  
  ---------------------------------------------------------------------------*/

static void snd_trident_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	trident_t *trident = snd_magic_cast(trident_t, dev_id, return);
	unsigned int audio_int, chn_int, stimer, channel, mask;
	int delta;
	snd_trident_voice_t *voice;

	audio_int = inl(TRID_REG(trident, T4D_MISCINT));
	if ((audio_int & (ADDRESS_IRQ|MPU401_IRQ)) == 0)
		return;
	if (audio_int & ADDRESS_IRQ) {
		// get interrupt status for all channels
		spin_lock(&trident->reg_lock);
		stimer = inl(TRID_REG(trident, T4D_STIMER)) & 0x00ffffff;
		chn_int = inl(TRID_REG(trident, T4D_AINT_A));
		if (chn_int == 0)
			goto __skip1;
		outl(chn_int, TRID_REG(trident, T4D_AINT_A));	/* ack */
	      __skip1:
		chn_int = inl(TRID_REG(trident, T4D_AINT_B));
		if (chn_int == 0)
			goto __skip2;
		for (channel = 63; channel >= 32; channel--) {
			mask = 1 << (channel&0x1f);
			if ((chn_int & mask) == 0)
				continue;
			voice = &trident->synth.voices[channel];
			delta = (int)stimer - (int)voice->stimer;
			if (delta > -32 && delta < 32) {
				trident->spurious_irq_count++;
				if (delta < 0)
					delta = -delta;
				if (trident->spurious_irq_max_delta < delta)
					trident->spurious_irq_max_delta = delta;
				if (delta < -20 || delta > 20)
					snd_printk("spurious interrupt detected: stimer = 0x%x, voice->stimer = 0x%x, delta = %i\n", stimer, voice->stimer, delta);
				continue;
			}
			voice->stimer = stimer;
			if (voice->pcm && voice->substream) {
				spin_unlock(&trident->reg_lock);
				snd_pcm_period_elapsed(voice->substream);
				spin_lock(&trident->reg_lock);
			} else {
				outl(mask, TRID_REG(trident, T4D_STOP_B));
			}
		}
		outl(chn_int, TRID_REG(trident, T4D_AINT_B));	/* ack */
	      __skip2:
		spin_unlock(&trident->reg_lock);
	}
	if (audio_int & MPU401_IRQ) {
		if (trident->rmidi) {
			snd_mpu401_uart_interrupt(irq, trident->rmidi->private_data, regs);
		} else {
			inb(TRID_REG(trident, T4D_MPUR0));
		}
	}
	// outl((ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW), TRID_REG(trident, T4D_MISCINT));
}

/*---------------------------------------------------------------------------
   snd_trident_attach_synthesizer, snd_trident_detach_synthesizer
  
   Description: Attach/detach synthesizer hooks
                
   Paramters:   trident  - device specific private data for 4DWave card

   Returns:     None.
  
  ---------------------------------------------------------------------------*/
int snd_trident_attach_synthesizer(trident_t *trident)
{	
#ifdef CONFIG_SND_SEQUENCER
	if (snd_seq_device_new(trident->card, 1, SNDRV_SEQ_DEV_ID_TRIDENT,
			       sizeof(trident_t*), &trident->seq_dev) >= 0) {
		strcpy(trident->seq_dev->name, "4DWave");
		*(trident_t**)SNDRV_SEQ_DEVICE_ARGPTR(trident->seq_dev) = trident;
	}
#endif
	return 0;
}

int snd_trident_detach_synthesizer(trident_t *trident)
{
#ifdef CONFIG_SND_SEQUENCER
	if (trident->seq_dev) {
		snd_device_free(trident->card, trident->seq_dev);
		trident->seq_dev = NULL;
	}
#endif
	return 0;
}

snd_trident_voice_t *snd_trident_alloc_voice(trident_t * trident, int type, int client, int port)
{
	snd_trident_voice_t *pvoice;
	unsigned long flags;
	int idx;

	spin_lock_irqsave(&trident->voice_alloc, flags);
	if (type == SNDRV_TRIDENT_VOICE_TYPE_PCM) {
		idx = snd_trident_allocate_pcm_channel(trident);
		if(idx < 0) {
			spin_unlock_irqrestore(&trident->voice_alloc, flags);
			return NULL;
		}
		pvoice = &trident->synth.voices[idx];
		pvoice->use = 1;
		pvoice->pcm = 1;
		pvoice->capture = 0;
		pvoice->spdif = 0;
		pvoice->memblk = NULL;
		spin_unlock_irqrestore(&trident->voice_alloc, flags);
		return pvoice;
	}
	if (type == SNDRV_TRIDENT_VOICE_TYPE_SYNTH) {
		idx = snd_trident_allocate_synth_channel(trident);
		if(idx < 0) {
			spin_unlock_irqrestore(&trident->voice_alloc, flags);
			return NULL;
		}
		pvoice = &trident->synth.voices[idx];
		pvoice->use = 1;
		pvoice->synth = 1;
		pvoice->client = client;
		pvoice->port = port;
		pvoice->memblk = NULL;
		spin_unlock_irqrestore(&trident->voice_alloc, flags);
		return pvoice;
	}
	if (type == SNDRV_TRIDENT_VOICE_TYPE_MIDI) {
	}
	spin_unlock_irqrestore(&trident->voice_alloc, flags);
	return NULL;
}

void snd_trident_free_voice(trident_t * trident, snd_trident_voice_t *voice)
{
	unsigned long flags;
	void (*private_free)(snd_trident_voice_t *);
	void *private_data;

	if (voice == NULL || !voice->use)
		return;
	snd_trident_clear_voices(trident, voice->number, voice->number);
	spin_lock_irqsave(&trident->voice_alloc, flags);
	private_free = voice->private_free;
	private_data = voice->private_data;
	voice->private_free = NULL;
	voice->private_data = NULL;
	if (voice->pcm)
		snd_trident_free_pcm_channel(trident, voice->number);
	if (voice->synth)
		snd_trident_free_synth_channel(trident, voice->number);
	voice->use = voice->pcm = voice->synth = voice->midi = 0;
	voice->capture = voice->spdif = 0;
	voice->sample_ops = NULL;
	voice->substream = NULL;
	voice->extra = NULL;
	spin_unlock_irqrestore(&trident->voice_alloc, flags);
	if (private_free)
		private_free(voice);
}

void snd_trident_clear_voices(trident_t * trident, unsigned short v_min, unsigned short v_max)
{
	unsigned int i, val, mask[2] = { 0, 0 };

	snd_assert(v_min <= 63, return);
	snd_assert(v_max <= 63, return);
	for (i = v_min; i <= v_max; i++)
		mask[i >> 5] |= 1 << (i & 0x1f);
	if (mask[0]) {
		outl(mask[0], TRID_REG(trident, T4D_STOP_A));
		val = inl(TRID_REG(trident, T4D_AINTEN_A));
		outl(val & ~mask[0], TRID_REG(trident, T4D_AINTEN_A));
	}
	if (mask[1]) {
		outl(mask[1], TRID_REG(trident, T4D_STOP_B));
		val = inl(TRID_REG(trident, T4D_AINTEN_B));
		outl(val & ~mask[1], TRID_REG(trident, T4D_AINTEN_B));
	}
}



EXPORT_SYMBOL(snd_trident_create);
EXPORT_SYMBOL(snd_trident_interrupt);
EXPORT_SYMBOL(snd_trident_pcm);
EXPORT_SYMBOL(snd_trident_foldback_pcm);
EXPORT_SYMBOL(snd_trident_spdif_pcm);
EXPORT_SYMBOL(snd_trident_mixer);
EXPORT_SYMBOL(snd_trident_attach_synthesizer);
EXPORT_SYMBOL(snd_trident_detach_synthesizer);
EXPORT_SYMBOL(snd_trident_alloc_voice);
EXPORT_SYMBOL(snd_trident_free_voice);
EXPORT_SYMBOL(snd_trident_start_voice);
EXPORT_SYMBOL(snd_trident_stop_voice);
EXPORT_SYMBOL(snd_trident_write_voice_regs);
EXPORT_SYMBOL(snd_trident_clear_voices);
/* trident_memory.c symbols */
EXPORT_SYMBOL(snd_trident_synth_alloc);
EXPORT_SYMBOL(snd_trident_synth_free);
EXPORT_SYMBOL(snd_trident_synth_bzero);
EXPORT_SYMBOL(snd_trident_synth_copy_from_user);

/*
 *  INIT part
 */

static int __init alsa_trident_init(void)
{
	return 0;
}

static void __exit alsa_trident_exit(void)
{
}

module_init(alsa_trident_init)
module_exit(alsa_trident_exit)
