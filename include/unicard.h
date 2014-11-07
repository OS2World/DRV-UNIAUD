/* $Id: unicard.h,v 1.1 2002/04/18 13:07:54 sandervl Exp $ */
/*
 * Header for debug functions
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

#ifndef __UNICARD_H__
#define __UNICARD_H__

#define CARD_NONE	-1
#define CARD_SBLIVE	0
#define CARD_CMEDIA	1
#define CARD_ALS4000	2
#define CARD_CS4281	3
#define CARD_ICH 	4

#define CARD_STRING_SBLIVE	"SBLIVE"
#define CARD_STRING_CMEDIA	"CMEDIA"
#define CARD_STRING_ALS4000	"ALS4000"
#define CARD_STRING_CS4281	"CS4281"
#define CARD_STRING_ICH 	"ICH"

#define CARD_MAX_LEN            16

extern int ForceCard;

#endif //__UNICARD_H__
