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

#ifndef UUSB_CONST_H
#define UUSB_CONST_H

typedef enum {
	USB_INTF_CLASS_ZERO = 0,
	USB_INTF_CLASS_HID = 3,
	USB_INTF_CLASS_STORAGE = 8,
	USB_INTF_CLASS_CCID = 11,
} uusb_intf_class_t;

typedef enum {
	USB_INTF_SUBCLASS_ZERO = 0,
	USB_INTF_SUBCLASS_BOOT = 1,
	USB_INTF_SUBCLASS_SCSI = 6,

	USB_INTF_SUBCLASS_ANY = 0xFF
} uusb_intf_subclass_t;

typedef enum {
	USB_INTF_PROTOCOL_ZERO = 0,
	USB_INTF_PROTOCOL_KEYBOARD = 1,

	USB_INTF_PROTOCOL_ANY = 0xFF
} uusb_intf_protocol_t;

#define UUSB_ENDPOINT_ADDRESS_MASK       0x0f
#define UUSB_ENDPOINT_DIR_MASK           0x80
#define UUSB_ENDPOINT_IN                 0x80
#define UUSB_ENDPOINT_OUT                0x00
#define UUSB_ENDPOINT_TYPE_MASK          0x03
#define UUSB_ENDPOINT_TYPE_CONTROL       0
#define UUSB_ENDPOINT_TYPE_ISOCHRONOUS   1
#define UUSB_ENDPOINT_TYPE_BULK          2
#define UUSB_ENDPOINT_TYPE_INTERRUPT     3


/* Maybe we should rename this file to uusb_prot.h */
typedef struct uusb_dt_parser {
	const unsigned char *	data;
	unsigned int		left;
	bool			okay;
} uusb_dt_parser_t;

static inline void
uusb_dt_parser_init(uusb_dt_parser_t *dtp, const unsigned char *data, unsigned int len)
{
	dtp->data = data + 2;
	dtp->left = len - 2;
	dtp->okay = true;
}

static inline unsigned long
uusb_dt_get_integer(uusb_dt_parser_t *dtp, unsigned int n)
{
	const unsigned char *p = dtp->data;
	unsigned long result = 0;
	unsigned int i;

	if (n > dtp->left) {
		dtp->okay = false;
		dtp->left = 0;
		return 0;
	}

	for (i = n; i--; )
		result = (result << 8) | p[i];

	dtp->data += n;
	dtp->left -= n;
	return result;
}

static inline bool
uusb_dt_skip(uusb_dt_parser_t *dtp, unsigned int n)
{
	if (n > dtp->left) {
		dtp->okay = false;
		dtp->left = 0;
		return false;
	}

	dtp->data += n;
	dtp->left -= n;
	return true;
}

static inline bool
uusb_dt_get_byte(uusb_dt_parser_t *dtp, uint8_t *vp)
{
	*vp = uusb_dt_get_integer(dtp, 1);
	return dtp->okay;
}

static inline bool
uusb_dt_get_word16(uusb_dt_parser_t *dtp, uint16_t *vp)
{
	*vp = uusb_dt_get_integer(dtp, 2);
	return dtp->okay;
}

static inline bool
uusb_dt_get_word32(uusb_dt_parser_t *dtp, uint32_t *vp)
{
	*vp = uusb_dt_get_integer(dtp, 4);
	return dtp->okay;
}

static inline bool
uusb_dt_skip_byte(uusb_dt_parser_t *dtp)
{
	return uusb_dt_skip(dtp, 1);
}

static inline bool
uusb_dt_skip_word16(uusb_dt_parser_t *dtp)
{
	return uusb_dt_skip(dtp, 2);
}

static inline bool
uusb_dt_skip_word32(uusb_dt_parser_t *dtp)
{
	return uusb_dt_skip(dtp, 4);
}

#endif /* UUSB_CONST_H */
