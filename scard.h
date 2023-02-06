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

#ifndef SCARD_H
#define SCARD_H

#include <sys/types.h>
#include <stdbool.h>
#include "uusb.h"

#define IFD_MAX_ATR_LEN		64

typedef struct ifd_atrbuf {
	unsigned int		len;
	unsigned char		data[IFD_MAX_ATR_LEN];
} ifd_atrbuf_t;

typedef struct ifd_card_driver {
	bool			(*set_option)(ifd_card_t *, const char *key, const char *value);
	bool			(*connect)(ifd_card_t *);
	bool			(*verify)(ifd_card_t *, const char *pin, size_t pin_len, unsigned int *tries_left);
	buffer_t * 		(*decipher)(ifd_card_t *, buffer_t *ciphertext);
} ifd_card_driver_t;

typedef struct ifd_card {
	const char *		name;
	ifd_atrbuf_t		atr;
	const ifd_card_driver_t *driver;
	unsigned int		variant;

	ccid_reader_t *		reader;
	unsigned int		slot;
	bool			pin_required;

	union {
		struct {
			unsigned char	key_slot;
		} yubikey;
	};
} ifd_card_t;

extern void		yubikey_init(void);

extern void		ifd_atrbuf_set(ifd_atrbuf_t *, const void *, size_t len);
extern void		ifd_register_card_driver(const ifd_atrbuf_t *, const char *name, ifd_card_driver_t *, int variant);
extern ifd_card_t *	ifd_create_card(const ifd_atrbuf_t *, ccid_reader_t *, unsigned int slot);
extern bool		ifd_card_set_option(ifd_card_t *, const char *);
extern bool		ifd_card_connect(ifd_card_t *);
extern buffer_t *	ifd_card_xfer(ifd_card_t *card, buffer_t *apdu, uint16_t *sw);
extern bool		ifd_card_verify(ifd_card_t *, const char *pin, size_t pin_len, unsigned int *tries_left);
extern buffer_t *	ifd_card_decipher(ifd_card_t *card, buffer_t *ciphertext);

extern buffer_t *	ifd_build_apdu(uint8_t, uint8_t, uint8_t, uint8_t, const void *, unsigned int);

#endif /* SCARD_H */
