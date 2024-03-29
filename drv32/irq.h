/* $Id: irq.h,v 1.2 2002/03/12 11:36:30 achimha Exp $ */
/*
 * IRQ handler header
 *
 * (C) 2000-2002 InnoTek Systemberatung GmbH
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

#ifndef __IRQ_H__
#define __IRQ_H__

#define NUM_IRQLEVELS		16

extern "C" FARPTR16 ISR03;
extern "C" FARPTR16 ISR04;
extern "C" FARPTR16 ISR05;
extern "C" FARPTR16 ISR07;
extern "C" FARPTR16 ISR09;
extern "C" FARPTR16 ISR10;
extern "C" FARPTR16 ISR11;
extern "C" FARPTR16 ISR12;
extern "C" FARPTR16 ISR13;
extern "C" FARPTR16 ISR14;
extern "C" FARPTR16 ISR15;

#endif //__IRQ_H__
