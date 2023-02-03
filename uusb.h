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


#ifndef UUSB_H
#define UUSB_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct uusb_type {
	uint16_t	idVendor;
	uint16_t	idProduct;
} uusb_type_t;

typedef struct uusb_devaddr {
	uint16_t	bus;
	uint16_t	dev;
} uusb_devaddr_t;

typedef struct uusb_interface	uusb_interface_t;
typedef struct uusb_endpoint	uusb_endpoint_t;
typedef struct uusb_config	uusb_config_t;
typedef struct uusb_dev		uusb_dev_t;
typedef struct ccid_reader	ccid_reader_t;
typedef struct ccid_descriptor	ccid_descriptor_t;
typedef struct buffer		buffer_t;
typedef struct ifd_card		ifd_card_t;

extern bool		usb_parse_type(const char *string, uusb_type_t *type);

/* Find device by vendor/product id */
extern uusb_dev_t *	usb_open_type(const uusb_type_t *);
/* Alternative idea: find device(s) that have a CCID descriptor */

extern bool		uusb_parse_descriptors(uusb_dev_t *dev, const unsigned char *data, size_t len);
extern bool		uusb_dev_select_ccid_interface(uusb_dev_t *, const struct ccid_descriptor **);
extern bool		uusb_send(uusb_dev_t *, buffer_t *);
extern buffer_t *	uusb_recv(uusb_dev_t *, size_t maxlen, long timeout);

extern ccid_reader_t *	ccid_reader_create(uusb_dev_t *);
extern bool		ccid_reader_select_slot(ccid_reader_t *, unsigned int slot);
extern ifd_card_t *	ccid_reader_identify_card(ccid_reader_t *, unsigned int slot);
extern buffer_t *	ccid_reader_apdu_xfer(ccid_reader_t * reader, unsigned int slot, buffer_t *apdu);


#endif /* UUSB_H */

