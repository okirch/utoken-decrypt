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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include "uusb_impl.h"
#include "uusb_const.h"
#include "ccid_impl.h"

static bool	uusb_handle_ccid_descriptor(uusb_interface_t *, const unsigned char *, unsigned int);

#define CLASSPROTO(C, S, P) \
	{ USB_INTF_CLASS_ ## C, USB_INTF_SUBCLASS_ ## S, USB_INTF_PROTOCOL_ ## P }

static uusb_intf_type_t	uusb_intf_type_list[] = {
	{ "keyboard", CLASSPROTO(HID, BOOT, KEYBOARD), },
	{ "ccid", CLASSPROTO(CCID, ZERO, ZERO), .handle_descriptor = uusb_handle_ccid_descriptor },
	{ "storage", CLASSPROTO(STORAGE, ANY, ANY), },
	{ NULL }
};

const char *
uusb_dt_type_string(unsigned int dt_type)
{
	static char name[16];

	switch (dt_type) {
	case USB_DT_DEVICE:
		return "device";
	case USB_DT_CONFIG:
		return "config";
	case USB_DT_STRING:
		return "string";
	case USB_DT_INTERFACE:
		return "intf";
	case USB_DT_ENDPOINT:
		return "ep";
	case USB_DT_HID:
		return "hid";
	default:
		break;
	}

	snprintf(name, sizeof(name), "t%02x", dt_type);
	return name;
}

static inline bool
__uusb_match_interface_id(uint8_t want, uint8_t got)
{
	return want == 0xFF || want == got;
}

static inline bool
__uusb_match_uusb_classproto(const uusb_classproto_t *want, const uusb_classproto_t *got)
{
	return (want->class == 0xFF || want->class == got->class)
	    && (want->subclass == 0xFF || want->subclass == got->subclass)
	    && (want->protocol == 0xFF || want->protocol == got->protocol);
}

static uusb_intf_type_t *
uusb_find_interface_type(const uusb_classproto_t *classproto)
{
	uusb_intf_type_t *intf_type;

	for (intf_type = uusb_intf_type_list; intf_type->name; ++intf_type) {
		if (__uusb_match_uusb_classproto(&intf_type->classproto, classproto))
			return intf_type;
	}

	return NULL;
}

static bool
__uusb_parse_device_descriptor(uusb_dt_parser_t *dtp, uusb_device_descriptor_t *dd)
{
	return uusb_dt_skip_word16(dtp) /* bcdUSB */
	    && uusb_dt_get_byte(dtp, &dd->bDevice.class)
	    && uusb_dt_get_byte(dtp, &dd->bDevice.subclass)
	    && uusb_dt_get_byte(dtp, &dd->bDevice.protocol)
	    && uusb_dt_get_byte(dtp, &dd->bMaxPacketSize0)
	    && uusb_dt_get_word16(dtp, &dd->idVendor)
	    && uusb_dt_get_word16(dtp, &dd->idProduct)
	    && uusb_dt_skip_word16(dtp) /* bcdDevice */
	    && uusb_dt_skip_byte(dtp) /* iManufacturer */
	    && uusb_dt_skip_byte(dtp) /* iProduct */
	    && uusb_dt_skip_byte(dtp) /* iSerialNumber */
	    && uusb_dt_get_byte(dtp, &dd->bNumConfigurations);
}

static bool
__uusb_parse_config_descriptor(uusb_dt_parser_t *dtp, uusb_config_descriptor_t *cd)
{
	return uusb_dt_skip_word16(dtp) /* wTotalLength */
	    && uusb_dt_get_byte(dtp, &cd->bNumInterfaces)
	    && uusb_dt_get_byte(dtp, &cd->bConfigurationValue)
	    && uusb_dt_skip_byte(dtp) /* iConfiguration */
	    && uusb_dt_get_byte(dtp, &cd->bmAttributes)
	    && uusb_dt_get_byte(dtp, &cd->MaxPower);

	return true;
}

static bool
__uusb_parse_interface_descriptor(uusb_dt_parser_t *dtp, uusb_interface_descriptor_t *id)
{
	return uusb_dt_get_byte(dtp, &id->bInterfaceNumber)
	    && uusb_dt_get_byte(dtp, &id->bAlternateSetting)
	    && uusb_dt_get_byte(dtp, &id->bNumEndpoints)
	    && uusb_dt_get_byte(dtp, &id->bInterface.class)
	    && uusb_dt_get_byte(dtp, &id->bInterface.subclass)
	    && uusb_dt_get_byte(dtp, &id->bInterface.protocol)
	    && uusb_dt_skip_byte(dtp); /* iInterface */
}

static bool
__uusb_parse_endpoint_descriptor(uusb_dt_parser_t *dtp, uusb_endpoint_descriptor_t *ed)
{
	return uusb_dt_get_byte(dtp, &ed->bEndpointAddress)
	    && uusb_dt_get_byte(dtp, &ed->bmAttributes)
	    && uusb_dt_skip_word16(dtp) /* wMaxPacketSize */
	    && uusb_dt_skip_byte(dtp); /* bInterval */
}

static bool
uusb_handle_ccid_descriptor(uusb_interface_t *interface, const unsigned char *data, unsigned int len)
{
	ccid_descriptor_t *ccid;

	ccid = calloc(1, sizeof(*ccid));
	if (!ccid_parse_usb_descriptor(ccid, data, len)) {
		free(ccid);
		return false;
	}

	interface->ccid = ccid;
	return true;
}

static bool
uusb_interface_process_descriptor(uusb_interface_t *interface, const unsigned char *data, size_t len)
{
	if (interface->type == NULL) {
		/* printf("Ignoring %s descriptor for unknown interface class\n", uusb_dt_type_string(data[1])); */
		return true;
	}

	if (interface->type->handle_descriptor == NULL) {
		usb_debug("Ignoring %s descriptor for %s interface\n", uusb_dt_type_string(data[1]), interface->type->name);
		return true;
	}

	return interface->type->handle_descriptor(interface, data, len);
}

bool
uusb_parse_descriptors(uusb_dev_t *dev, const unsigned char *data, size_t len)
{
	unsigned int pos;
	uusb_config_t *config = NULL;
	uusb_interface_t *interface = NULL;
	uusb_endpoint_t *endpoint = NULL;

	for (pos = 0; pos + 2 < len; pos += data[pos]) {
		const unsigned char *dt_bytes = data + pos;
		unsigned char dt_len = data[pos];
		unsigned char dt_type = data[pos + 1];
		uusb_dt_parser_t dt;

		if (pos + dt_len > len) {
			error("Bad descriptors (dt at pos %u is too long)\n", pos);
			return false;
		}

		usb_debug("%-8s %3u\n", uusb_dt_type_string(dt_type), dt_len);

		uusb_dt_parser_init(&dt, data + pos, dt_len);
		if (pos == 0) {
			if (dt_type != USB_DT_DEVICE) {
				error("Bad descriptors (first descriptor is type %s)\n", uusb_dt_type_string(dt_type));
				return false;
			}
			if (!__uusb_parse_device_descriptor(&dt, &dev->descriptor))
				return false;

			if (dev->descriptor.bNumConfigurations == 0 || dev->descriptor.bNumConfigurations > UUSB_MAX_CONFIGS) {
				error("Cannot handle device with %u configurations\n", dev->descriptor.bNumConfigurations);
				return false;
			}
		} else {
			switch (dt_type) {
			case USB_DT_DEVICE:
				error("Bad descriptors (duplicate device descriptor)\n");
				return false;
			case USB_DT_CONFIG:
				if (dev->num_configs > dev->descriptor.bNumConfigurations) {
					error("Too many config descriptors\n");
					return false;
				}

				config = &dev->config[dev->num_configs++];
				if (!__uusb_parse_config_descriptor(&dt, &config->descriptor))
					return false;
				interface = NULL;
				break;
			case USB_DT_INTERFACE:
				if (config == NULL) {
					error("Interface descriptor precedes first config descriptor\n");
					return false;
				}

				if (config->num_interfaces > config->descriptor.bNumInterfaces) {
					error("Too many interface descriptors\n");
					return false;
				}

				interface = &config->interface[config->num_interfaces++];
				if (!__uusb_parse_interface_descriptor(&dt, &interface->descriptor))
					return false;

				interface->type = uusb_find_interface_type(&interface->descriptor.bInterface);
				if (interface->type == NULL)
					usb_debug("Interface for unknown class %u/subclass %u/protocol %u\n",
							interface->descriptor.bInterface.class,
							interface->descriptor.bInterface.subclass,
							interface->descriptor.bInterface.protocol);
				break;

			case USB_DT_ENDPOINT:
				if (interface == NULL) {
					error("Endpoint descriptor precedes first interface descriptor\n");
					return false;
				}

				if (interface->num_endpoints > interface->descriptor.bNumEndpoints) {
					error("Too many endpoint descriptors\n");
					return false;
				}

				endpoint = &interface->endpoint[interface->num_endpoints++];
				if (!__uusb_parse_endpoint_descriptor(&dt, &endpoint->descriptor))
					return false;

				break;

			default:
				/* FIXME: should probably also take a dt_parser */
				if (interface && !uusb_interface_process_descriptor(interface, dt_bytes, dt_len))
					return false;
				break;
			}
		}
	}

	return true;
}
