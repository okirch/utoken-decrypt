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


#ifndef CCID_IMPL_H
#define CCID_IMPL_H

#include <stdint.h>
#include "ccid.h"

struct ccid_descriptor {
	uint16_t	bcdCCID;
	uint8_t		bMaxSlotIndex;
	uint8_t		bVoltageSupport;
	uint32_t	dwProtocols;
	uint32_t	dwDefaultClock;
	uint32_t	dwMaximumClock;
	uint8_t		bNumClockRatesSupported;
	uint32_t	dwDataRate;
	uint32_t	dwMaxDataRate;
	uint8_t		bNumDataRatesSupported;
	uint32_t	dwMaxIFSD;
	uint32_t	dwSynchProtocols;
	uint32_t	dwMechanical;
	uint32_t	dwFeatures;
	uint32_t	dwMaxCCIDMessageLength;
	uint8_t		bClassGetResponse;
	uint8_t		bClassEnvelope;
	uint16_t	wLcdLayout;
	uint8_t		bPINSupport;
	uint8_t		bMaxCCIDBusySlots;
};

#define CCID_PROTO_T0_MASK	0x01
#define CCID_PROTO_T1_MASK	0x02

#endif /* CCID_IMPL_H */


