/* $Id: misc.c,v 1.24 2002/06/13 10:08:53 sandervl Exp $ */
/*
 * OS/2 implementation of misc. Linux kernel services
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

#include "linux.h"
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#define CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <linux\ioport.h>
#include <linux\utsname.h>
#include <linux\module.h>
#include <dbgos2.h>

struct new_utsname system_utsname = {0};
struct resource ioport_resource = {NULL, 0, 0, IORESOURCE_IO, NULL, NULL, NULL};
struct resource iomem_resource  = {NULL, 0, 0, IORESOURCE_MEM, NULL, NULL, NULL};
mem_map_t *mem_map = 0;
int this_module[64] = {0};
                
#define CR 0x0d
#define LF 0x0a


#define LEADING_ZEROES          0x8000
#define SIGNIFICANT_FIELD       0x0007

char *HexLongToASCII(char *StrPtr, unsigned long wHexVal, unsigned short Option);
char *DecLongToASCII(char *StrPtr, unsigned long lDecVal, unsigned short Option);


//SvL: Not safe to use in non-KEE driver
int sprintf (char *buffer, const char *format, ...)
{
   char *BuildPtr=buffer;
   char *pStr = (char *) format;
   char *SubStr;
   union {
         void   *VoidPtr;
         unsigned short *WordPtr;
         unsigned long  *LongPtr;
         unsigned long  *StringPtr;
         } Parm;
   int wBuildOption;

   Parm.VoidPtr=(void *) &format;
   Parm.StringPtr++;                            // skip size of string pointer

   while (*pStr)
      {
      switch (*pStr)
         {
         case '%':
            wBuildOption=0;
            pStr++;
            if (*pStr=='0')
               {
               wBuildOption|=LEADING_ZEROES;
               pStr++;
               }
            if (*pStr=='u')                                                         // always unsigned
               pStr++;

            switch(*pStr)
               {
               case 'x':
	       case 'X':
                  BuildPtr=HexLongToASCII(BuildPtr, *Parm.LongPtr++,wBuildOption);
                  pStr++;
                  continue;

               case 'i':
               case 'd':
               case 'D':
                  BuildPtr=DecLongToASCII(BuildPtr, *Parm.LongPtr++,wBuildOption);
                  pStr++;
                  continue;

               case 's':
                  SubStr=(char *)*Parm.StringPtr;
                  while (*BuildPtr++ = *SubStr++);
                  Parm.StringPtr++;
                  BuildPtr--;                      // remove the \0
                  pStr++;
                  continue;

               case 'l':
                  pStr++;
                  switch (*pStr)
                  {
                  case 'x':
                  case 'X':
                  BuildPtr=HexLongToASCII(BuildPtr, *Parm.LongPtr++,wBuildOption);
                  pStr++;
                  continue;

                  case 'd':
                     BuildPtr=DecLongToASCII(BuildPtr, *Parm.LongPtr++,wBuildOption);
                     pStr++;
                     continue;
                  } // end switch
                  continue;                        // dunno what he wants

               case 0:
                  continue;
               } // end switch
            break;

      case '\\':
         pStr++;
         switch (*pStr)
            {
            case 'n':
            *BuildPtr++=LF;
            pStr++;
            continue;

            case 'r':
            *BuildPtr++=CR;
            pStr++;
            continue;

            case 0:
            continue;
            break;
            } // end switch

         break;
         } // end switch

      *BuildPtr++=*pStr++;
      } // end while

   *BuildPtr=0;                                 // cauterize the string
   return 1; //not correct
}

#ifndef DEBUG
int printk(const char * fmt, ...)
{
  return 0;
}
#endif

void schedule(void)
{

}

void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{

}


int __check_region(struct resource *a, unsigned long b, unsigned long c)
{
    DebugInt3();
    return 0;
}

//iodelay is in 500ns units
void iodelay32(unsigned long);
#pragma aux iodelay32 parm nomemory [ecx] modify nomemory exact [eax ecx];

//microsecond delay
void __udelay(unsigned long usecs)
{
  iodelay32(usecs*2);
}

//millisecond delay
void mdelay(unsigned long msecs)
{
    iodelay32(msecs*2*1000);
}


/* --------------------------------------------------------------------- */
/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef hweight32
#undef hweight32
#endif

unsigned int hweight32(unsigned int w)
{
	unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
	res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
	res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
	return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}
//******************************************************************************
//******************************************************************************
struct proc_dir_entry proc_root = {0};
//******************************************************************************
//******************************************************************************
struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
				         struct proc_dir_entry *parent)
{
    struct proc_dir_entry *proc;

    proc = (struct proc_dir_entry *)kmalloc(sizeof(struct proc_dir_entry), 0);
    memset(proc, 0, sizeof(struct proc_dir_entry));

    proc->name   = name;
    proc->mode   = mode;
    proc->parent = parent;

    return proc;
}
//******************************************************************************
//******************************************************************************
void remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
    return; //memory leak
}
//******************************************************************************
//******************************************************************************
int proc_register(struct proc_dir_entry *parent, struct proc_dir_entry *proc)
{
    return 0;
}
//******************************************************************************
//******************************************************************************
int proc_unregister(struct proc_dir_entry *proc, int bla)
{
    return 0;
}
//******************************************************************************
//This is a dirty hack to make some stupid ac97 timeout mechanism work
//******************************************************************************
signed long schedule_timeout(signed long timeout)
{
    mdelay(timeout);
    jiffies += timeout;
    return 0;
}
//******************************************************************************
//******************************************************************************
mem_map_t *virt_to_page(int x)
{
    static mem_map_t map = {0};
    return &map;
}
//******************************************************************************
//******************************************************************************
int fasync_helper(int a, struct file *b, int c, struct fasync_struct **d)
{
    return 0;
}
//******************************************************************************
//******************************************************************************
void kill_fasync(struct fasync_struct *a, int b, int c)
{
}
//******************************************************************************
//******************************************************************************
int request_dma(unsigned int dmanr, const char * device_id)	/* reserve a DMA channel */
{
    DebugInt3();
    return 0;
}
//******************************************************************************
//******************************************************************************
void free_dma(unsigned int dmanr)
{
    DebugInt3();
}
//******************************************************************************
/* enable/disable a specific DMA channel */
//******************************************************************************
void enable_dma(unsigned int dmanr)
{
    DebugInt3();
}
//******************************************************************************
//******************************************************************************
void disable_dma(unsigned int dmanr)
{
    DebugInt3();
}
//******************************************************************************
static struct notifier_block *reboot_notify_list = NULL;
// No need to implement this right now. The ESS Maestro 3 driver uses it
// to call pci_unregister_driver, which is always called from the shutdown
// notification sent by OS2.
// Same goes for es1968 & Yamaha's DS1/DS1E.
//******************************************************************************
int register_reboot_notifier(struct notifier_block *pnblock)
{
    return 0;
}
//******************************************************************************
//******************************************************************************
int unregister_reboot_notifier(struct notifier_block *pnblock)
{
    return 0;
}
//******************************************************************************
//******************************************************************************

