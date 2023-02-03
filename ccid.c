/*
 *   Copyright (C) 2023 SUSE LLC
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
 * Written by Olaf Kirch <okir@suse.com>
 */


#include "ccid_impl.h"
#include "uusb_const.h"


bool
ccid_parse_usb_descriptor(ccid_descriptor_t *d, const unsigned char *data, unsigned int len)
{
	uusb_dt_parser_t dt;

	uusb_dt_parser_init(&dt, data, len);

	return uusb_dt_get_word16(&dt, &d->bcdCCID)
	    && uusb_dt_get_byte(&dt, &d->bMaxSlotIndex)
	    && uusb_dt_get_byte(&dt, &d->bVoltageSupport)
	    && uusb_dt_get_word32(&dt, &d->dwProtocols)
	    && uusb_dt_get_word32(&dt, &d->dwDefaultClock)
	    && uusb_dt_get_word32(&dt, &d->dwMaximumClock)
	    && uusb_dt_get_byte(&dt, &d->bNumClockRatesSupported)
	    && uusb_dt_get_word32(&dt, &d->dwDataRate)
	    && uusb_dt_get_word32(&dt, &d->dwMaxDataRate)
	    && uusb_dt_get_byte(&dt, &d->bNumDataRatesSupported)
	    && uusb_dt_get_word32(&dt, &d->dwMaxIFSD)
	    && uusb_dt_get_word32(&dt, &d->dwSynchProtocols)
	    && uusb_dt_get_word32(&dt, &d->dwMechanical)
	    && uusb_dt_get_word32(&dt, &d->dwFeatures)
	    && uusb_dt_get_word32(&dt, &d->dwMaxCCIDMessageLength)
	    && uusb_dt_get_byte(&dt, &d->bClassGetResponse)
	    && uusb_dt_get_byte(&dt, &d->bClassEnvelope)
	    && uusb_dt_get_word16(&dt, &d->wLcdLayout)
	    && uusb_dt_get_byte(&dt, &d->bPINSupport)
	    && uusb_dt_get_byte(&dt, &d->bMaxCCIDBusySlots);
}

