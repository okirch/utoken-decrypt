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


#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "scard.h"
#include "bufparser.h"
#include "util.h"

/* Is this card specific or generic? */
#define IFD_INS_GET_RESPONSE_APDU	0xc0

typedef struct ifd_card_driver_registration {
	struct ifd_card_driver_registration *next;
	const ifd_atrbuf_t *	atr;
	const char *		name;
	ifd_card_driver_t *	driver;
	int			variant;
} ifd_card_driver_registration_t;

typedef struct apdu {
	uint8_t		cla;
	uint8_t		ins;
	uint8_t		p1;
	uint8_t		p2;
	uint8_t		lc;
	uint8_t		data[0xff];
} APDU;

static ifd_card_driver_registration_t *ifd_card_drivers, **ifd_card_drivers_tail = &ifd_card_drivers;

void
ifd_atrbuf_set(ifd_atrbuf_t *atr, const void *data, size_t len)
{
	if (len > sizeof(atr->data))
		len = sizeof(atr->data);
	memcpy(atr->data, data, len);
	atr->len = len;
}

static const char *
ifd_atrbuf_to_string(const ifd_atrbuf_t *atr)
{
	static char namebuf[3 * IFD_MAX_ATR_LEN + 1];
	unsigned int i, j;

	if (atr->len > IFD_MAX_ATR_LEN)
		return NULL;

	memset(namebuf, 0, sizeof(namebuf));
	for (i = j = 0; i < atr->len; ++i) {
		if (j)
			namebuf[j++] = ':';
		snprintf(namebuf + j, 3, "%02x", atr->data[i]);
		j += 2;
	}

	return namebuf;
}

bool
ifd_atrbuf_equal(const ifd_atrbuf_t *atr1, const ifd_atrbuf_t *atr2)
{
	return atr1->len == atr2->len && !memcmp(atr1->data, atr2->data, atr1->len);
}

void
ifd_register_card_driver(const ifd_atrbuf_t *atr, const char *name, ifd_card_driver_t *driver, int variant)
{
	ifd_card_driver_registration_t *reg;

	reg = calloc(1, sizeof(*reg));
	reg->atr = atr;
	reg->name = name;
	reg->driver = driver;
	reg->variant = variant;

	*ifd_card_drivers_tail = reg;
	ifd_card_drivers_tail = &reg->next;
}

static ifd_card_t *
ifd_card_alloc(const ifd_atrbuf_t *atr, const char *name, const ifd_card_driver_t *driver, int variant)
{
	ifd_card_t *card;

	card = calloc(1, sizeof(*card));
	card->atr = *atr;
	card->name = name;
	card->driver = driver;
	card->variant = variant;

	/* By default, assume that a PIN is required */
	card->pin_required = true;

	return card;
}

ifd_card_t *
ifd_create_card(const ifd_atrbuf_t *atr, ccid_reader_t *reader, unsigned int slot)
{
	ifd_card_driver_registration_t *reg;
	ifd_card_t *card = NULL;

	debug2("Trying to identify card; atr %s\n", ifd_atrbuf_to_string(atr));
	for (reg = ifd_card_drivers; reg; reg = reg->next) {
		debug("Checking %s; atr %s\n", reg->name, ifd_atrbuf_to_string(reg->atr));
		if (ifd_atrbuf_equal(atr, reg->atr)) {
			card = ifd_card_alloc(atr, reg->name, reg->driver, reg->variant);
			card->reader = reader;
			card->slot = slot;
			break;
		}
	}

	return card;
}

bool
ifd_card_connect(ifd_card_t *card)
{
	if (card->driver->connect == NULL)
		return true;

	debug("Connecting to card\n");
	return card->driver->connect(card);
}

bool
ifd_card_verify(ifd_card_t *card, const char *pin, size_t pin_len, unsigned int *tries_left)
{
	if (card->driver->verify == NULL) {
		debug("Driver does not support PIN verification\n");
		return false;
	}

	debug("Verifying PIN\n");
	return card->driver->verify(card, pin, pin_len, tries_left);
}

buffer_t *
ifd_card_decipher(ifd_card_t *card, buffer_t *ciphertext)
{
	if (card->driver->decipher == NULL) {
		debug("Driver does not support decryption\n");
		return false;
	}

	debug("Decrypting %u bytes of ciphertext\n", buffer_available(ciphertext));
	return card->driver->decipher(card, ciphertext);
}

static buffer_t *
ifd_card_apdu(ifd_card_t *card, buffer_t *apdu, uint16_t *sw_ret)
{
	ccid_reader_t *reader = card->reader;
	unsigned int slot = card->slot;
	unsigned int rlen;
	buffer_t *rapdu;

	if (!(rapdu = ccid_reader_apdu_xfer(reader, slot, apdu)))
		return NULL;

	if ((rlen = buffer_available(rapdu)) < 2) {
		error("Response APDU too short\n");
		return NULL;
	} else {
		const unsigned char *swpos = buffer_write_pointer(rapdu) - 2;

		*sw_ret = (swpos[0] << 8) | swpos[1];
		rapdu->wpos -= 2;
		debug("Received response APDU, sw=%04x\n", *sw_ret);
	}

	return rapdu;
}

buffer_t *
ifd_card_xfer(ifd_card_t *card, buffer_t *apdu, uint16_t *sw_ret)
{
	buffer_t *rapdu;

	if (!(rapdu = ifd_card_apdu(card, apdu, sw_ret)))
		return NULL;

	while ((*sw_ret & 0xFF00) == 0x6100) {
		uint8_t lc = *sw_ret & 0xFF;
		unsigned int len;
		buffer_t *apdu2, *rapdu2;

		len = lc?: 0x100;
		debug2("Card signals %u additional response bytes\n", len);
		apdu2 = ifd_build_apdu(0, IFD_INS_GET_RESPONSE_APDU, 0, 0, NULL, lc);

		rapdu2 = ifd_card_apdu(card, apdu2, sw_ret);
		buffer_free(apdu2);

		if (rapdu2 == NULL)
			goto failed;

		if (buffer_available(rapdu2) != len) {
			error("Card advertised %u more bytes or data, but GET_RESPONSE returned %u\n",
					len, buffer_available(rapdu2));
			buffer_free(rapdu2);
			goto failed;
		}

		if (!buffer_put(rapdu, buffer_read_pointer(rapdu2), buffer_available(rapdu2))) {
			error("Response buffer too small\n");
			buffer_free(rapdu2);
			goto failed;
		}
		buffer_free(rapdu2);
	}

	return rapdu;

failed:
	if (rapdu)
		buffer_free(rapdu);
	return NULL;
}

buffer_t *
ifd_build_apdu(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, const void *data, unsigned int len)
{
	buffer_t *apdu;
	uint8_t lc = len;

	if (len > 0xFF) {
		error("%s called with %u bytes of data\n", __func__, len);
		return NULL;
	}

	apdu = buffer_alloc_write(5 + len);
	if (buffer_put_u8(apdu, &cla)
	 && buffer_put_u8(apdu, &ins)
	 && buffer_put_u8(apdu, &p1)
	 && buffer_put_u8(apdu, &p2)
	 && buffer_put_u8(apdu, &lc)
	 && (data == NULL || buffer_put(apdu, data, len)))
		return apdu;

	return NULL;
}
