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


#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "uusb_impl.h"
#include "uusb_const.h" /* maybe we should move the logic in uusb_set_endpoints to descriptors.c */
#include "bufparser.h"

#define SYSFS_USB_DEVICES	"/sys/bus/usb/devices"

bool
usb_parse_type(const char *string, uusb_type_t *type)
{
	char *copy, *s, *end;

	memset(type, 0, sizeof(*type));
	if (!(copy = strdup(string))) {
		perror("strdup");
		return false;
	}

	s = strchr(copy, ':');
	if (s)
		*s++ = '\0';

	type->idVendor = strtoul(copy, &end, 16);
	if (*end)
		goto bad;

	if (s) {
		type->idProduct = strtoul(s, &end, 16);
		if (*end)
			goto bad;
	}

	free(copy);
	return true;

bad:
	fprintf(stderr, "Cannot parse USB vendor:product string \"%s\"\n", string);
	return false;
}

static char *
usb_find_device(bool (*match_fn)(const char *path, const void *data), const void *data)
{
	char *result = NULL;
	DIR *dir;
	struct dirent *d;

	if (!(dir = opendir(SYSFS_USB_DEVICES))) {
		fprintf(stderr, "Cannot open %s: %m\n", SYSFS_USB_DEVICES);
		return NULL;
	}

	while ((d = readdir(dir)) != NULL) {
		char sysfs_dir[PATH_MAX];

		if (d->d_name[0] == '.')
			continue;

		snprintf(sysfs_dir, sizeof(sysfs_dir), "%s/%s", SYSFS_USB_DEVICES, d->d_name);
		if (match_fn(sysfs_dir, data)) {
			result = strdup(sysfs_dir);
			break;
		}
	}

	closedir(dir);
	return result;
}

static unsigned char *
sysfs_read_buffer(const char *sysfs_dir, const char *name, size_t *size_ret)
{
	char path[PATH_MAX];
	unsigned char *result = NULL;
	struct stat stb;
	int fd = -1, count = 0;

	*size_ret = 0;

	snprintf(path, sizeof(path), "%s/%s", sysfs_dir, name);
	if ((fd = open(path, O_RDONLY)) < 0)
		return NULL;

	if (fstat(fd, &stb) < 0) {
		perror("fstat");
		goto failed;
	}

	result = malloc(stb.st_size);
	while (true) {
		int r;

		r = read(fd, result, stb.st_size - count);
		if (r == 0)
			break;
		if (r < 0) {
			perror("read");
			goto failed;
		}

		count += r;
	}

	*size_ret = count;
	close(fd);
	return result;

failed:
	if (fd >= 0)
		close(fd);
	if (result)
		free(result);
	return NULL;
}

static char *
sysfs_read_line(const char *sysfs_dir, const char *name)
{
	char path[PATH_MAX], linebuf[1024], *result = NULL;
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s", sysfs_dir, name);
	if ((fp = fopen(path, "r")) == NULL)
		return NULL;

	if (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
		linebuf[strcspn(linebuf, "\r\n")] = '\0';
		result = strdup(linebuf);
	}

	fclose(fp);
	return result;
}

static unsigned int
sysfs_read_integer_base(const char *sysfs_dir, const char *name, unsigned int base)
{
	char *line, *end;
	unsigned long result;

	if (!(line = sysfs_read_line(sysfs_dir, name)))
		return ~0U;

	result = strtoul(line, &end, base);
	if (*end || result >= ~0U)
		return ~0U;

	return result;
}

static unsigned int
sysfs_read_decimal(const char *sysfs_dir, const char *name)
{
	return sysfs_read_integer_base(sysfs_dir, name, 10);
}

static unsigned int
sysfs_read_hexadecimal(const char *sysfs_dir, const char *name)
{
	return sysfs_read_integer_base(sysfs_dir, name, 16);
}

static bool
usb_match_type(const char *sysfs_dir, const void *data)
{
	const uusb_type_t *type = data;

	if (type->idVendor && type->idVendor != sysfs_read_hexadecimal(sysfs_dir, "idVendor"))
		return false;
	if (type->idProduct && type->idProduct != sysfs_read_hexadecimal(sysfs_dir, "idProduct"))
		return false;

	return true;
}

static bool
__uusb_process_descriptors(uusb_dev_t *dev)
{
	unsigned char *data;
	size_t len;
	bool rv;

	if (!(data = sysfs_read_buffer(dev->sysfs_dir, "descriptors", &len)))
		return false;

	rv = uusb_parse_descriptors(dev, data, len);
	free(data);
	return rv;
}

static bool
sysfs_get_dev_t(const char *sysfs_dir, const char *name, dev_t *dev_ret)
{
	unsigned int major = 0, minor = 0;
	char *majmin, *w;
	bool ok = false;

	if (!(majmin = sysfs_read_line(sysfs_dir, "dev"))) {
		fprintf(stderr, "Cannot read %s/dev\n", sysfs_dir);
		return NULL;
	}

	if ((w = strtok(majmin, ":")) != NULL) {
		major = strtoul(w, NULL, 0);
		if ((w = strtok(NULL, "")) != NULL) {
			minor = strtoul(w, NULL, 0);
			*dev_ret = makedev(major, minor);
			ok = true;
		}
	}

	free(majmin);
	return ok;
}

static bool
__uusb_attach_device(uusb_dev_t *dev)
{
	dev_t linuxdev;
	char path[PATH_MAX];
	struct stat stb;

	dev->devaddr.bus = sysfs_read_decimal(dev->sysfs_dir, "busnum");
	dev->devaddr.dev = sysfs_read_decimal(dev->sysfs_dir, "devnum");

	if (!sysfs_get_dev_t(dev->sysfs_dir, "dev", &linuxdev)) {
		fprintf(stderr, "Cannot get dev_t for USB device\n");
		return false;
	}

	snprintf(path, sizeof(path), "/dev/bus/usb/%03u/%03u",
			dev->devaddr.bus, dev->devaddr.dev);

	if (stat(path, &stb) < 0) {
		perror(path);
		return false;
	}

	if (!S_ISCHR(stb.st_mode) || stb.st_rdev != linuxdev) {
		return false;
	}

	dev->dev_path = strdup(path);
	return true;
}

static bool
__uusb_identify_device(uusb_dev_t *dev)
{
	dev->type.idVendor = sysfs_read_hexadecimal(dev->sysfs_dir, "idVendor");
	dev->type.idProduct = sysfs_read_hexadecimal(dev->sysfs_dir, "idProduct");
	return true;
}

static uusb_dev_t *
__usb_open(char *sysfs_dir)
{
	uusb_dev_t *dev;

	dev = calloc(1, sizeof(*dev));
	dev->sysfs_dir = sysfs_dir;
	dev->fd = -1;

	if (!__uusb_attach_device(dev)) {
		fprintf(stderr, "Cannot attach system device file\n");
		return NULL;
	}

	if (!__uusb_identify_device(dev)) {
		fprintf(stderr, "Cannot identify USB device\n");
		return NULL;
	}

	if (!__uusb_process_descriptors(dev)) {
		fprintf(stderr, "Error parsing USB descriptors\n");
		return NULL;
	}

	dev->fd = open(dev->dev_path, O_RDWR);
	if (dev->fd < 0) {
		fprintf(stderr, "Unable to open %s: %m\n", dev->dev_path);
		return NULL;
	}

	printf("Opened USB device %04x:%04x at %u:%u; path %s\n",
			dev->type.idVendor, dev->type.idProduct,
			dev->devaddr.bus, dev->devaddr.dev,
			dev->dev_path);
	return dev;
}

uusb_dev_t *
usb_open_type(const uusb_type_t *type)
{
	char *sysfs_dir;

	if (!(sysfs_dir = usb_find_device(usb_match_type, type)))
		return NULL;

	return __usb_open(sysfs_dir);
}

static bool
uusb_select_interface(uusb_dev_t *dev, const uusb_config_t *config, const uusb_interface_t *interface)
{
	unsigned int config_num = config->descriptor.bConfigurationValue;
	unsigned int interface_num = interface->descriptor.bInterfaceNumber;

	/* Do not try to use SETCONFIGURATION unless there is more than one config.
	 * Otherwise we get nasty kernel messages like this:
	 *   usb 1-10: usbfs: interface 0 claimed by usbhid while 'uusb' sets config #1
	 */
	if (dev->descriptor.bNumConfigurations > 1) {
		printf("Selecting config %u\n", config_num);
		if (ioctl(dev->fd, USBDEVFS_SETCONFIGURATION, &config_num) < 0) {
			perror("ioctl(USBDEVFS_SETCONFIGURATION)");
			return false;
		}
	}

	if (interface_num != 0) {
		printf("Selecting config %u interface %u\n", config_num, interface_num);
		if (ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface_num) < 0) {
			perror("ioctl(USBDEVFS_CLAIMINTERFACE)");
			return false;
		}
	}

	/* When using an interface with bAltsetting, select this using USBDEVFS_SETINTERFACE */

	return true;
}

static bool
uusb_set_endpoints(uusb_dev_t *dev, uusb_interface_t *interface)
{
	unsigned int i;

	dev->endpoints.ep_o = -1;
	dev->endpoints.ep_i = -1;
	dev->endpoints.ep_intr = -1;

	for (i = 0; i < interface->num_endpoints; ++i) {
		uusb_endpoint_t *ep = &interface->endpoint[i];
		const uusb_endpoint_descriptor_t *d = &ep->descriptor;
		unsigned char ep_type = d->bmAttributes & UUSB_ENDPOINT_TYPE_MASK;
		unsigned char ep_dir = d->bEndpointAddress & UUSB_ENDPOINT_DIR_MASK;

		if (ep_type == UUSB_ENDPOINT_TYPE_BULK) {
			if (ep_dir == UUSB_ENDPOINT_IN)
				dev->endpoints.ep_i = d->bEndpointAddress;
			else
				dev->endpoints.ep_o = d->bEndpointAddress;
		} else if (ep_type == UUSB_ENDPOINT_TYPE_INTERRUPT) {
			if (ep_dir == UUSB_ENDPOINT_IN)
				dev->endpoints.ep_intr = d->bEndpointAddress;
		}
	}

	return (dev->endpoints.ep_o >= 0 && dev->endpoints.ep_i >= 0);
}

bool
uusb_dev_select_ccid_interface(uusb_dev_t *dev, const struct ccid_descriptor **ccid_ret)
{
	ccid_descriptor_t *ccid;
	unsigned int i, j;

	*ccid_ret = NULL;

	for (i = 0; i < dev->num_configs; ++i) {
		uusb_config_t *config = &dev->config[i];

		for (j = 0; j < config->num_interfaces; ++j) {
			uusb_interface_t *interface = &config->interface[j];

			if ((ccid = interface->ccid) != NULL
			 && uusb_set_endpoints(dev, interface)
			 && uusb_select_interface(dev, config, interface)) {
				printf("Successfully selected CCID interface\n");
				*ccid_ret = ccid;
				return true;
			}
		}
	}

	return false;
}

static int
uusb_bulk(uusb_dev_t *dev, uint8_t ep, unsigned char *buffer, unsigned int len, long timeout)
{
        struct usbdevfs_bulktransfer bulk;
        int rc;

#if 0
	if (opt_debug > 1 && (ep & UUSB_ENDPOINT_DIR_MASK) == UUSB_ENDPOINT_OUT) {
		debug("uusb_bulk sending packet to endpoint 0x%02x, timeout %ld ms\n", ep, timeout);
		hexdump(buffer, len, debug2, 4);
	}
#endif

        bulk.ep = ep;
        bulk.data = buffer;
        bulk.len = len;
        bulk.timeout = timeout;
        if ((rc = ioctl(dev->fd, USBDEVFS_BULK, &bulk)) < 0) {
		error("%s: ioctl failed: %m\n", __func__);
                return rc;
        }

#if 0
	if (opt_debug > 1 && (ep & UUSB_ENDPOINT_DIR_MASK) == UUSB_ENDPOINT_IN) {
		debug("uusb_bulk received packet from endpoint 0x%02x, timeout %ld ms\n", ep, timeout);
		hexdump(buffer, bulk.len, debug2, 4);
	}
#endif

        return bulk.len;
}

bool
uusb_send(uusb_dev_t *dev, buffer_t *pkt)
{
	return uusb_bulk(dev, dev->endpoints.ep_o, 
			(void *) buffer_read_pointer(pkt),
			buffer_available(pkt),
			10000) >= 0;
}

buffer_t *
uusb_recv(uusb_dev_t *dev, size_t maxlen, long timeout)
{
	buffer_t *pkt;
	int len;

	/* Allocate a response packet large enough to hold the max response size */
	pkt = buffer_alloc_write(maxlen);
	memset(buffer_write_pointer(pkt), 0xAA, buffer_tailroom(pkt));

	len = uusb_bulk(dev, dev->endpoints.ep_i, 
			(void *) buffer_write_pointer(pkt),
			buffer_tailroom(pkt),
			timeout);
	if (len < 0) {
		buffer_free(pkt);
		return NULL;
	}

	/* there should be a buffer_* function for this */
	pkt->wpos += len;

	return pkt;
}
