/*
 *   Copyright (C) 2022, 2023 SUSE LLC
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
#include <ctype.h>
#include <stdbool.h>
#include <iconv.h>

#include "util.h"

bool
parse_hexdigit(const char **pos, unsigned char *ret)
{
	char cc = *(*pos)++;
	unsigned int octet;

	if (isdigit(cc))
		octet = cc - '0';
	else if ('a' <= cc && cc <= 'f')
		octet = cc - 'a' + 10;
	else if ('A' <= cc && cc <= 'F')
		octet = cc - 'A' + 10;
	else
		return false;

	*ret = (*ret << 4) | octet;
	return true;
}

bool
parse_octet(const char **pos, unsigned char *ret)
{
	return parse_hexdigit(pos, ret) && parse_hexdigit(pos, ret);
}

unsigned int
parse_octet_string(const char *string, unsigned char *buffer, size_t bufsz)
{
	const char *orig_string = string;
	unsigned int i;

	for (i = 0; *string; ++i) {
		if (i >= bufsz) {
			debug("%s: octet string too long for buffer: \"%s\"\n", __func__, orig_string);
			return 0;
		}
		if (!parse_octet(&string, &buffer[i])) {
			debug("%s: bad octet near offset %d \"%s\"\n", __func__, 2 * i, orig_string);
			return 0;
		}
	}

	return i;
}

const char *
print_octet_string(const unsigned char *data, unsigned int len)
{
	static char buffer[3 * 64 + 1];

	if (len < 32) {
		unsigned int i;
		char *s;

		s = buffer;
		for (i = 0; i < len; ++i) {
			if (i)
				*s++ = ':';
			sprintf(s, "%02x", data[i]);
			s += 2;
		}
		*s = '\0';
	} else {
		snprintf(buffer, sizeof(buffer), "<%u bytes of data>", len);
	}

	return buffer;

}

void
hexdump(const void *data, size_t size, void (*print_fn)(const char *, ...), unsigned int indent)
{
	const unsigned char *bytes = data;
	unsigned int i, j, bytes_per_line;
	char octets[32 * 3 + 1];
	char ascii[32 + 1];

	for (i = 0; i < size; i += 32) {
		char *pos;

		if ((bytes_per_line = size - i) > 32)
			bytes_per_line = 32;

		pos = octets;
		for (j = 0; j < 32; ++j) {
			if (j < bytes_per_line)
				sprintf(pos, " %02x", bytes[i + j]);
			else
				sprintf(pos, "   ");
			pos += 3;
		}

		pos = ascii;
		for (j = 0; j < bytes_per_line; ++j) {
			unsigned char cc = bytes[i + j];

			if (isalnum(cc) || ispunct(cc))
				*pos++ = cc;
			else
				*pos++ = '.';

			*pos = '\0';
		}

		print_fn("%*.*s%04x %-96s %-s\n",
				(int) indent, (int) indent, "",
				i, octets, ascii);
	}
}
