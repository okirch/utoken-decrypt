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


#include "scard.h"
#include "bufparser.h"
#include "util.h"

#define YKPIV_INS_VERIFY		0x20
#define YKPIV_INS_CHANGE_REFERENCE	0x24
#define YKPIV_INS_RESET_RETRY		0x2c
#define YKPIV_INS_GENERATE_ASYMMETRIC	0x47
#define YKPIV_INS_AUTHENTICATE		0x87
#define YKPIV_INS_GET_DATA		0xcb
#define YKPIV_INS_PUT_DATA		0xdb
#define YKPIV_INS_SELECT_APPLICATION	0xa4
#define YKPIV_INS_GET_RESPONSE_APDU	0xc0

/* APDU response status */
#define YKPIV_SUCCESS			0x9000
#define YKPIV_ERR_SECURITY_STATUS	0x6982
#define YKPIV_ERR_AUTH_BLOCKED		0x6983
#define YKPIV_ERR_CONDITIONS_OF_USE	0x6985
#define YKPIV_ERR_INCORRECT_PARAM	0x6a80
#define YKPIV_ERR_FILE_NOT_FOUND	0x6a82
#define YKPIV_ERR_REFERENCE_NOT_FOUND	0x6a88
#define YKPIV_ERR_INCORRECT_SLOT	0x6b00
#define YKPIV_ERR_NOT_SUPPORTED		0x6d00

#define YKPIV_ALGO_RSA1024		0x06
#define YKPIV_ALGO_RSA2048		0x07
#define YKPIV_ALGO_ECCP256		0x11
#define YKPIV_ALGO_ECCP384		0x14

#define MAKE_ATR(s)	{ .len = sizeof(s) - 1, .data = s }

static ifd_atrbuf_t	atr_neo_r3 = MAKE_ATR("\x3b\xfc\x13\x00\x00\x81\x31\xfe\x15\x59\x75\x62\x69\x6b\x65\x79\x4e\x45\x4f\x72\x33\xe1");
static ifd_atrbuf_t	atr_yubikey4 = MAKE_ATR("\x3b\xf8\x13\x00\x00\x81\x31\xfe\x15\x59\x75\x62\x69\x6b\x65\x79\x34\xd4");
static ifd_atrbuf_t	atr_yubikey5 = MAKE_ATR("\x3b\xfd\x13\x00\x00\x81\x31\xfe\x15\x80\x73\xc0\x21\xc0\x57\x59\x75\x62\x69\x4b\x65\x79\x40");
static ifd_atrbuf_t	atr_yubikey5_p1 = MAKE_ATR("\x3b\xf8\x13\x00\x00\x81\x31\xfe\x15\x01\x59\x75\x62\x69\x4b\x65\x79\xc1");

enum {
	YK_VARIANT_NEO_R3,
	YK_VARIANT_YUBIKEY_4,
	YK_VARIANT_YUBIKEY_5,
	YK_VARIANT_YUBIKEY_5_P1,
};

static bool		yubikey_connect(ifd_card_t *card);
static bool		yubikey_verify(ifd_card_t *card, const char *pin, size_t pin_len, unsigned int *tries_left);
static buffer_t *	yubikey_decipher(ifd_card_t *card, buffer_t *ciphertext);

static ifd_card_driver_t	yubikey_driver = {
	.connect	= yubikey_connect,
	.verify		= yubikey_verify,
	.decipher	= yubikey_decipher,
};

void
yubikey_init(void)
{
	ifd_register_card_driver(&atr_neo_r3, "YubiKey Neo R3", &yubikey_driver, YK_VARIANT_NEO_R3);
	ifd_register_card_driver(&atr_yubikey4, "YubiKey 4", &yubikey_driver, YK_VARIANT_YUBIKEY_4);
	ifd_register_card_driver(&atr_yubikey5, "YubiKey 5", &yubikey_driver, YK_VARIANT_YUBIKEY_5);
	ifd_register_card_driver(&atr_yubikey5_p1, "YubiKey 5", &yubikey_driver, YK_VARIANT_YUBIKEY_5_P1);
}

bool
yubikey_select_application(ifd_card_t *card, const void *aid, size_t aid_len)
{
	buffer_t *apdu, *rapdu = NULL;
	bool rv = false;
	uint16_t sw;

	apdu = ifd_build_apdu(0x00, YKPIV_INS_SELECT_APPLICATION, 0x04, 0x00, aid, aid_len);
	if (!apdu) {
		error("failed to build APDU\n");
		return false;
	}

	rapdu = ifd_card_xfer(card, apdu, &sw);
	if (rapdu == NULL) {
		error("Failed to select application: communication error\n");
		goto done;
	} else if (sw != YKPIV_SUCCESS) {
		error("Failed to select application: card reports status %04x\n", sw);
		goto done;
	}

	rv = true;

done:
	buffer_free(apdu);
	if (rapdu)
		buffer_free(rapdu);
	return rv;
}

bool
yubikey_connect(ifd_card_t *card)
{
	static unsigned const char piv_aid[] = { 0xa0, 0x00, 0x00, 0x03, 0x08 };

	debug("%s()\n", __func__);

	if (!yubikey_select_application(card, piv_aid, sizeof(piv_aid)))
		return false;

	infomsg("Successfully selected PIV application\n");

#if 0
	/* try to select t=1 */
	ccid_reader_select_protocol(card->reader, card->slot, 1);
#endif

	debug("Trying PIN password to see whether a PIN is required\n");
	if (yubikey_verify(card, NULL, 0, NULL))
		card->pin_required = false;
	else
		debug("This card requires a PIN\n");

	return true;
}

bool
yubikey_verify(ifd_card_t *card, const char *pin, size_t pin_len, unsigned int *tries_left)
{
	unsigned char padded_pin[8];
	buffer_t *apdu, *rapdu = NULL;
	bool rv = false;
	uint16_t sw;

	if (pin == NULL) {
		apdu = ifd_build_apdu(0x00, YKPIV_INS_VERIFY, 0x00, 0x80, NULL, 0);
	} else if (pin_len > sizeof(padded_pin)) {
		error("PIN too long\n");
		return false;
	} else {
		memset(padded_pin, 0xFF, sizeof(padded_pin));
		memcpy(padded_pin, pin, pin_len);

		apdu = ifd_build_apdu(0x00, YKPIV_INS_VERIFY, 0x00, 0x80,
				padded_pin, sizeof(padded_pin));
	}

	if (!apdu) {
		error("failed to build APDU\n");
		return false;
	}

	rapdu = ifd_card_xfer(card, apdu, &sw);
	if (rapdu == NULL) {
		error("Failed to select application: communication error\n");
		goto done;
	} else if ((sw & 0xFF00) == 0x6300) {
		unsigned int nleft = sw & 0x000F;

		if (tries_left)
			*tries_left = nleft;
		debug("Incorrect password, %u tries left\n", nleft);
		goto done;
	} else if (sw != YKPIV_SUCCESS) {
		error("Failed to select application: card reports status %04x\n", sw);
		goto done;
	}

	rv = true;

done:
	buffer_free(apdu);
	if (rapdu)
		buffer_free(rapdu);
	return rv;
}

static inline unsigned int
enc_push_byte(unsigned char *encoded, unsigned int pos, uint8_t byte)
{
	encoded[--pos] = byte;
	return pos;
}

static inline unsigned int
enc_push_length(unsigned char *encoded, unsigned int pos, unsigned int len)
{
	pos = enc_push_byte(encoded, pos, len);
	if (len < 0x80) {
		/* done */
	} else if (len < 0x100) {
		pos = enc_push_byte(encoded, pos, 0x81);
	} else {
		pos = enc_push_byte(encoded, pos, len >> 8);
		pos = enc_push_byte(encoded, pos, 0x82);
	}

	return pos;
}

static inline int
decode_length(const unsigned char *encoded, unsigned int *pos_p)
{
	unsigned int pos = *pos_p;
	unsigned int len;

	len = encoded[pos++];
	if (len == 0x81) {
		len = encoded[pos++];
	} else if (len == 0x82) {
		len = encoded[pos++];
		len = (len << 8) | encoded[pos++];
	}
	*pos_p = pos;
	return len;
}

static inline unsigned int
enc_push(unsigned char *encoded, unsigned int pos, const void *data, unsigned int count)
{
	if (count > pos)
		return 0;

	pos -= count;
	memcpy(encoded + pos, data, count);
	return pos;
}

/*
 * Encode this thing back to front. The weird length encoding makes things easier this way.
 */
static buffer_t *
yubikey_encode_decipher_args(uint8_t algorithm, const void *ciphertext, unsigned int in_len)
{
	unsigned char encoded[1024];
	unsigned int pos, count;
	buffer_t *result;

	memset(encoded, 0, sizeof(encoded));
	pos = sizeof(encoded);
	if (in_len + 32 > pos)
		return NULL;

	pos = enc_push(encoded, pos, ciphertext, in_len);
	pos = enc_push_length(encoded, pos, in_len);

	pos = enc_push_byte(encoded, pos, 0x81); /* ECC decipher would be 0x85 */
	pos = enc_push_byte(encoded, pos, 0x00);
	pos = enc_push_byte(encoded, pos, 0x82);
	pos = enc_push_length(encoded, pos, sizeof(encoded) - pos);
	pos = enc_push_byte(encoded, pos, 0x7c);

	count = sizeof(encoded) - pos;

	result = buffer_alloc_write(1024);
	buffer_put(result, encoded + pos, count);
	return result;
}

static bool
yubikey_decode_decipher_resp(buffer_t *resp)
{
	const unsigned char *data = buffer_read_pointer(resp);
	unsigned int i = 0;

	if (data[i++] != 0x7c)
		return false;
	(void) decode_length(data, &i);

	if (data[i++] != 0x82)
		return false;

	(void) decode_length(data, &i);
	return buffer_skip(resp, i);
}

static bool
pkcs1_type2_padding_remove(buffer_t *bp)
{
	const unsigned char *data = buffer_read_pointer(bp);
	unsigned int len = buffer_available(bp);
	unsigned int i;

	debug("%s: %02x %02x ...\n", __func__, data[0], data[1]);
	if (data[0] != 0x00 || data[1] != 0x02)
		return false;

	for (i = 2; i < len; ++i) {
		/* Bleichenbacher? Never heard that name. */
		if (data[i] == 0) {
			buffer_skip(bp, i + 1);
			return true;
		}
	}

	return false;
}

static buffer_t *
yubikey_decipher(ifd_card_t *card, buffer_t *ciphertext)
{
	unsigned int key = 0x9a;	/* for now, assume we're always using slot 9a. */
	unsigned int in_len, key_len;
	uint8_t algorithm;
	buffer_t *data, *apdu = NULL, *rapdu = NULL, *cleartext = NULL;

	in_len = buffer_available(ciphertext);

	if (opt_debug > 1) {
		debug("Trying to decipher %u bytes of data\n", in_len);
		hexdump(buffer_read_pointer(ciphertext), in_len, debug2, 4);
	}

	/* For now, assume it's always RSA */
	switch (in_len) {
	case 128:
		algorithm = YKPIV_ALGO_RSA1024;
		key_len = 128;
		break;

	case 256:
		algorithm = YKPIV_ALGO_RSA2048;
		key_len = 256;
		break;

	default:
		error("Unexpected ciphertext size, unable to determine public key algorithm\n");
		return NULL;
	}

	data = yubikey_encode_decipher_args(algorithm, buffer_read_pointer(ciphertext), in_len);

	while (buffer_available(data)) {
		unsigned int len = buffer_available(data);
		uint8_t cla = 0x00;
		uint16_t sw;

		if (len > 0xFF) {
			len = 0xFF;
			cla |= 0x10;
		}

		apdu = ifd_build_apdu(cla, YKPIV_INS_AUTHENTICATE, algorithm, key,
				buffer_read_pointer(data),
				len);

		rapdu = ifd_card_xfer(card, apdu, &sw);
		buffer_free(apdu);

		if (rapdu == NULL) {
			error("Failed to decipher: communication error\n");
			goto done;
		} else if (sw != YKPIV_SUCCESS) {
			error("Failed to decipher: card reports status %04x\n", sw);
			goto done;
		}

		buffer_skip(data, len);
	}

	if (!yubikey_decode_decipher_resp(rapdu))
		goto done;

	/* The response APDU should now contain the padded secret. We expect pkcs1 2 padding */
	if (pkcs1_type2_padding_remove(rapdu)) {
		cleartext = rapdu;
		rapdu = NULL;

		debug("Returning cleartext\n");
		hexdump(buffer_read_pointer(cleartext), buffer_available(cleartext), debug, 4);
	}

done:
	if (data)
		buffer_free(data);
	if (rapdu)
		buffer_free(rapdu);
	return cleartext;
}
