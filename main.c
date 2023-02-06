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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include "uusb.h"
#include "scard.h"
#include "bufparser.h"
#include "util.h"

static struct option	options[] = {
	{ "device",	required_argument,	NULL,	'D' },
	{ "type",	required_argument,	NULL,	'T' },
	{ "pin",	required_argument,	NULL,	'p' },
	{ "output",	required_argument,	NULL,	'o' },
	{ "debug",	no_argument,		NULL,	'd' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL }
};

unsigned int	opt_debug = 0;

static buffer_t *	read_data(const char *filename);
static buffer_t *	doit(uusb_dev_t *dev, const char *pin, buffer_t *secret);

int
main(int argc, char **argv)
{
	char *opt_device = NULL;
	char *opt_type = NULL;
	char *opt_pin = NULL;
	char *opt_output = NULL;
	buffer_t *secret;
	uusb_dev_t *dev;
	buffer_t *cleartext;
	int c;

	while ((c = getopt_long(argc, argv, "dhD:T:p:o:", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			printf("Sorry, no help message. Please refer to the README.\n");
			exit(0);

		case 'd':
			opt_debug++;
			break;

		case 'D':
			opt_device = optarg;
			break;

		case 'T':
			opt_type = optarg;
			break;

		case 'p':
			opt_pin = optarg;
			break;

		case 'o':
			opt_output = optarg;
			break;

		default:
			error("Unknown option %c\n", c);
			return 1;
		}
	}

	if (optind != argc - 1) {
		error("Expected exactly one non-positional argument\n");
		return 1;
	}

	secret = read_data(argv[optind++]);

	(void) opt_device;

	if (opt_type) {
		uusb_type_t type;

		if (!usb_parse_type(opt_type, &type))
			return 1;

		dev = usb_open_type(&type);
	}

	if (dev == NULL) {
		error("Did not find USB device\n");
		return 1;
	}

	yubikey_init();

	if (!(cleartext = doit(dev, opt_pin, secret)))
		return 1;

	if (!buffer_write_file(opt_output, cleartext))
		return 1;

	buffer_free(cleartext);
	return 0;
}

static buffer_t *
read_data(const char *filename)
{
	infomsg("Reading data from \"%s\"\n", filename);
	return buffer_read_file(filename, 0);
}

buffer_t *
doit(uusb_dev_t *dev, const char *pin, buffer_t *ciphertext)
{
	ccid_reader_t *reader;
	ifd_card_t *card;
	buffer_t *cleartext;

	if (!(reader = ccid_reader_create(dev))) {
		error("Unable to create reader for USB device\n");
		return NULL;
	}

	if (!ccid_reader_select_slot(reader, 0))
		return NULL;

	card = ccid_reader_identify_card(reader, 0);
	if (card == NULL)
		return NULL;

	if (!ifd_card_connect(card))
		return NULL;

	if (card->pin_required) {
		unsigned int retries_left;

		if (pin == NULL) {
			error("This card requires a PIN\n");
			return NULL;
		}

		if (!ifd_card_verify(card, pin, strlen(pin), &retries_left)) {
			error("Wrong PIN, %u attempts left\n", retries_left);
			return NULL;
		}

		infomsg("Successfully verified PIN.\n");
	}

	cleartext = ifd_card_decipher(card, ciphertext);
	if (cleartext == NULL) {
		error("Card failed to decrypt secret\n");
		return NULL;
	}

	return cleartext;
}
