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


#ifndef UUSB_IMPL_H
#define UUSB_IMPL_H

#include "uusb.h"
#include "ccid.h"
#include "util.h"

#undef USB_DEBUG

/*
 * Descriptor types
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23
#define USB_DT_HUB			0x29

#define USB_DT_DEVICE_SIZE              18
#define USB_DT_CONFIG_SIZE              9
#define USB_DT_INTERFACE_SIZE           9
#define USB_DT_ENDPOINT_SIZE            7

typedef struct uusb_classproto {
	uint8_t		class;
	uint8_t		subclass;
	uint8_t		protocol;
} uusb_classproto_t;

typedef struct uusb_device_descriptor {
	uusb_classproto_t bDevice;
	uint8_t		bMaxPacketSize0;
	uint16_t	idVendor;
	uint16_t	idProduct;
	uint8_t		bNumConfigurations;
} uusb_device_descriptor_t;

typedef struct uusb_config_descriptor {
	uint8_t		bNumInterfaces;
	uint8_t		bConfigurationValue;
	uint8_t		bmAttributes;
	uint8_t		MaxPower;
} uusb_config_descriptor_t;

typedef struct uusb_interface_descriptor {
	uusb_classproto_t bInterface;
	uint8_t		bInterfaceNumber;
	uint8_t		bAlternateSetting;
	uint8_t		bNumEndpoints;
} uusb_interface_descriptor_t;

typedef struct uusb_endpoint_descriptor {
	uint8_t		bEndpointAddress;
	uint8_t		bmAttributes;
} uusb_endpoint_descriptor_t;

#define UUSB_MAX_CONFIGS	8
#define UUSB_MAX_INTERFACES	8
#define UUSB_MAX_ENDPOINTS	4

typedef struct uusb_interface	uusb_interface_t;

typedef struct uusb_endpoint {
	uusb_endpoint_descriptor_t descriptor;
} uusb_endpoint_t;

typedef const struct uusb_intf_type {
	const char *	name;
	uusb_classproto_t classproto;

	bool		(*handle_descriptor)(uusb_interface_t *, const unsigned char *, unsigned int);
} uusb_intf_type_t;

struct uusb_interface {
	uusb_interface_descriptor_t descriptor;

	uusb_intf_type_t *type;

	ccid_descriptor_t *ccid;

	unsigned int	num_endpoints;
	uusb_endpoint_t	endpoint[UUSB_MAX_ENDPOINTS];
};

typedef struct uusb_config {
	uusb_config_descriptor_t descriptor;

	unsigned int	num_interfaces;
	uusb_interface_t interface[UUSB_MAX_INTERFACES];
} uusb_config_t;

typedef struct uusb_dev {
	char *		sysfs_dir;
	char *		dev_path;
	int		fd;

	struct {
		int	ep_o;
		int	ep_i;
		int	ep_intr;
	} endpoints;

	uusb_type_t	type;
	uusb_devaddr_t	devaddr;
	uusb_device_descriptor_t descriptor;

	unsigned int	num_configs;
	uusb_config_t	config[UUSB_MAX_CONFIGS];
} uusb_dev_t;

extern bool		usb_parse_type(const char *string, uusb_type_t *type);

/* Find device by vendor/product id */
extern uusb_dev_t *	usb_open_type(const uusb_type_t *);
/* Alternative idea: find device(s) that have a CCID descriptor */

extern bool		uusb_parse_descriptors(uusb_dev_t *dev, const unsigned char *data, size_t len);


#ifdef USB_DEBUG
# define usb_debug		debug2
#else
# define usb_debug(fmt ...)	do { } while (0)
#endif


#endif /* UUSB_IMPL_H */


