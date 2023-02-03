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


#include <stdlib.h>
#include <stdio.h>
#include "uusb.h"
#include "ccid.h"
#include "scard.h"
#include "ccid_impl.h"
#include "bufparser.h"

#define CCID_CMD_FIRST		0x60
#define CCID_CMD_ICCPOWERON	0x62
#define CCID_CMD_ICCPOWEROFF	0x63
#define CCID_CMD_GETSLOTSTAT	0x65
#define CCID_CMD_XFRBLOCK	0x6F
#define CCID_CMD_GETPARAMS	0x6C
#define CCID_CMD_RESETPARAMS	0x6D
#define CCID_CMD_SETPARAMS	0x61
#define CCID_CMD_ESCAPE		0x6B
#define CCID_CMD_ICCCLOCK	0x6E
#define CCID_CMD_T0APDU		0x6A
#define CCID_CMD_SECURE		0x69
#define CCID_CMD_MECHANICAL	0x71
#define CCID_CMD_ABORT		0x72
#define CCID_CMD_SET_DR_FREQ	0x73

#define CCID_RESP_DATA          0x80
#define CCID_RESP_SLOTSTAT      0x81
#define CCID_RESP_PARAMS        0x82

#define CCID_HDR_OFFSET_SLOT	5
#define CCID_HDR_OFFSET_SEQ	6
#define CCID_HDR_OFFSET_CTL1	7
#define CCID_HDR_OFFSET_CTL2	8
#define CCID_HDR_OFFSET_CTL3	9
#define CCID_HDR_SIZE		10

struct ccid_reader {
	uusb_dev_t *		dev;
	const ccid_descriptor_t *ccid;
	unsigned int		max_message_size;
	unsigned int		supported_protocols;

	bool			auto_voltage;
	unsigned int		supported_voltages;

	int			current_slot;

	unsigned int		ccid_seq;
};

typedef struct ccid_command ccid_command_t;
struct ccid_command {
	uint8_t			slot, seq;
	buffer_t *		pkt;
};

typedef struct ccid_response ccid_response_t;
struct ccid_response {
	uint8_t			type, slot, seq;
	uint8_t			ctl[3];
	buffer_t *		pkt;
	buffer_t *		payload;
};

static bool	ccid_reader_set_features(ccid_reader_t *, const ccid_descriptor_t *);

ccid_reader_t *
ccid_reader_create(uusb_dev_t *dev)
{
	const ccid_descriptor_t *ccid;
	ccid_reader_t *reader;

	if (!uusb_dev_select_ccid_interface(dev, &ccid)) {
		fprintf(stderr, "USB device does not have a CCID descriptor\n");
		return NULL;
	}

	if ((ccid->dwProtocols & (CCID_PROTO_T0_MASK | CCID_PROTO_T1_MASK)) == 0) {
		fprintf(stderr, "CCID device does not speak any protocol we understand\n");
		return NULL;
	}

	if ((ccid->bVoltageSupport & 0x07) == 0) {
	}

	reader = calloc(1, sizeof(*reader));
	reader->dev = dev;
	reader->ccid = ccid;

	reader->current_slot = -1;

	if (!ccid_reader_set_features(reader, ccid)) {
		/* ccid_reader_free(reader); */
		return NULL;
	}

	reader->max_message_size = ccid->dwMaxCCIDMessageLength;
	reader->supported_protocols = ccid->dwProtocols;
	reader->supported_voltages = ccid->bVoltageSupport & 0x7;

	if (reader->supported_voltages == 0 && !reader->auto_voltage) {
		/* bummer */
	}

	return reader;
}

static ccid_command_t *
ccid_command_create(uint8_t seqno, uint8_t slot, buffer_t *pkt)
{
	ccid_command_t *cmd;

	cmd = calloc(1, sizeof(*cmd));
	cmd->pkt = pkt;
	cmd->seq = seqno;
	cmd->slot = slot;
	return cmd;
}

static void
ccid_command_free(ccid_command_t *cmd)
{
	if (cmd->pkt)
		buffer_free(cmd->pkt);
	free(cmd);
}

static ccid_response_t *
ccid_response_create(buffer_t *pkt)
{
	ccid_response_t *resp;
	uint32_t payload_len;

	if (buffer_available(pkt) < 10) {
		debug("short ccid response packet\n");
		return NULL;
	}

	resp = calloc(1, sizeof(*resp));
	if (!buffer_get_u8(pkt, &resp->type)
	 || !buffer_get_u32le(pkt, &payload_len)
	 || !buffer_get_u8(pkt, &resp->slot)
	 || !buffer_get_u8(pkt, &resp->seq)
	 || !buffer_get(pkt, resp->ctl, 3)
	 || !buffer_truncate(pkt, payload_len)
	 ) {
		debug2("short ccid response packet\n");
		free(resp);
		return NULL;
	}

	resp->payload = pkt;
	return resp;
}

static void
ccid_response_free(ccid_response_t *resp)
{
	if (resp->pkt)
		buffer_free(resp->pkt);
	if (resp->payload)
		buffer_free(resp->payload);
	free(resp);
}

static ccid_command_t *
ccid_build_command(ccid_reader_t *reader, uint8_t slot, uint8_t cmd,
			const unsigned char *ctl_data,
			const void *payload, unsigned int payload_len)
{
	static const unsigned char ctl_zero[3] = { 0, 0, 0 };
	uint8_t seq = reader->ccid_seq;
	buffer_t *bp;

	if (ctl_data == NULL)
		ctl_data = ctl_zero;

	bp = buffer_alloc_write(16 + payload_len);
	if (!buffer_put_u8(bp, &cmd)
	 || !buffer_put_u32le(bp, payload_len)
	 || !buffer_put_u8(bp, &slot)
	 || !buffer_put_u8(bp, &seq)
	 || !buffer_put(bp, ctl_data, 3))
		goto failed;

	if (payload_len && !buffer_put(bp, payload, payload_len))
		goto failed;

	/* Increase the seqno once we've actually sent the packet */
	return ccid_command_create(seq, slot, bp);

failed:
	buffer_free(bp);
	return NULL;
}

static void
ccid_dump_response(buffer_t *pkt)
{
	const unsigned char *hdr;
	unsigned int payload_len;

	if (buffer_available(pkt) < 10) {
		debug("Received short CCID response packet\n");
		return;
	}

	hdr = buffer_read_pointer(pkt);
	payload_len = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16) | (hdr[4] << 24);

	if (buffer_available(pkt) < 10 + payload_len) {
		debug("Received CCID response, data truncated\n");
	} else {
		debug("Received CCID response\n");
	}

	hexdump(hdr, 10 + payload_len, debug2, 4);
}

static ccid_command_t *
ccid_build_simple_packet(ccid_reader_t *reader, unsigned int slot, unsigned int cmd)
{
	return ccid_build_command(reader, slot, cmd, NULL, NULL, 0);
}

static ccid_response_t *
ccid_xfer(ccid_reader_t *reader, ccid_command_t *cmd, uint8_t expected_resp_type)
{
	ccid_response_t *resp = NULL;
	unsigned int retries = 6;
	bool rv;

	debug("Sending CCID packet (slot=%u seq=%u)\n", cmd->slot, cmd->seq);
	if (opt_debug > 1) {
	       buffer_t *pkt = cmd->pkt;

               hexdump(buffer_read_pointer(pkt), buffer_available(pkt), debug2, 4);
        }

	rv = uusb_send(reader->dev, cmd->pkt);
	if (!rv)
		return NULL;

	reader->ccid_seq = cmd->seq + 1;

	while (true) {
		buffer_t *rbuf;

		if (retries-- == 0) {
			error("%s: too many retries\n", __func__);
			break;
		}

		rbuf = uusb_recv(reader->dev, reader->max_message_size, 10000);
		if (rbuf == NULL)
			break;

		if (opt_debug > 1)
			ccid_dump_response(rbuf);

		resp = ccid_response_create(rbuf);
		if (resp == NULL) {
			/* truncated packet */
			buffer_free(rbuf);
		} else {
			if (resp->slot == cmd->slot && resp->seq == cmd->seq) {
				uint8_t ctl = resp->ctl[0];

				if (resp->type != expected_resp_type) {
					error("CCID response type %02x, expected %02x\n",
							resp->type, expected_resp_type);
					goto failed;
				}

				if ((ctl & 0xc0) == 0)
					return resp;

				if ((ctl & 0xc0) == 0x80) {
					debug("Card needs more time\n");
				} else {
					error("CCID error %u\n", resp->ctl[1]);
					goto failed;
				}
			}
		}

		/* wrong packet, or timeout extension */
		ccid_response_free(resp);
		resp = NULL;
	}

	return resp;

failed:
	if (resp)
		ccid_response_free(resp);
	return NULL;
}

static bool
ccid_get_slot_status(ccid_reader_t *reader, unsigned int slot, int *status_ret)
{
	ccid_command_t *cmd = NULL;
	ccid_response_t *resp = NULL;

	cmd = ccid_build_simple_packet(reader, slot, CCID_CMD_GETSLOTSTAT);
	if (cmd == NULL)
		goto failed;

	if (!(resp = ccid_xfer(reader, cmd, CCID_RESP_SLOTSTAT)))
		goto failed;

	if ((resp->ctl[0] & 0x3) == 2)
		*status_ret = 0;
	else
		*status_ret = 1;

	ccid_command_free(cmd);
	ccid_response_free(resp);
	return true;

failed:
	if (cmd)
		ccid_command_free(cmd);
	if (resp)
		ccid_response_free(resp);
	return false;
}

static bool
ccid_card_poweron(ccid_reader_t *reader, unsigned int slot, uint8_t voltage, ifd_atrbuf_t *atr)
{
	unsigned char ctl[3] = { 0, 0, 0 };
	ccid_command_t *cmd = NULL;
	ccid_response_t *resp = NULL;
	buffer_t *atrbuf;
	bool rv = false;

	ctl[0] = voltage;

	cmd = ccid_build_command(reader, slot, CCID_CMD_ICCPOWERON, ctl, NULL, 0);
	if (cmd == NULL)
		goto done;

	if (!(resp = ccid_xfer(reader, cmd, CCID_RESP_DATA)))
		goto done;

	atrbuf = resp->payload;
	ifd_atrbuf_set(atr, buffer_read_pointer(atrbuf), buffer_available(atrbuf));
	rv = true;

done:
	if (cmd)
		ccid_command_free(cmd);
	if (resp)
		ccid_response_free(resp);

	return rv;

}

static bool
ccid_reset_card(ccid_reader_t *reader, unsigned int slot, ifd_atrbuf_t *atr)
{
	unsigned int i;

	if (reader->auto_voltage) {
		debug("%s: powering on with auto voltage\n", __func__);
		if (ccid_card_poweron(reader, slot, 0, atr))
			return true;
	}

	for (i = 0; i < 3; ++i) {
		if (reader->supported_voltages & (1 << i)
		 && ccid_card_poweron(reader, slot, i + 1, atr))
			return true;
	}

	error("Unable to power on card\n");
	return false;
}

bool
ccid_reader_select_slot(ccid_reader_t *reader, unsigned int slot)
{
	int status;

	if (reader->current_slot == slot)
		return true;

	if (!ccid_get_slot_status(reader, slot, &status)) {
		fprintf(stderr, "Cannot get slot status\n");
		return false;
	}

	if (status == 0) {
		error("No smart card present\n");
		return false;
	}

	infomsg("Slot status 0x%x\n", status);
	reader->current_slot = slot;

	return true;
}

ifd_card_t *
ccid_reader_identify_card(ccid_reader_t *reader, unsigned int slot)
{
	ifd_atrbuf_t atr;
	ifd_card_t *card;

	if (slot != reader->current_slot) {
		error("Cannot handle multiple slots simultaneously\n");
		return NULL;
	}

	if (!ccid_reset_card(reader, slot, &atr))
		return NULL;

	card = ifd_create_card(&atr, reader, slot);
	if (card == NULL) {
		error("Unable to identify card\n");
		return NULL;
	}

	debug("Found %s\n", card->name);
	return card;
}

static int
ccid_reader_getparams(ccid_reader_t *reader, unsigned int slot, unsigned char *parambuf, unsigned int size)
{
	ccid_command_t *cmd = NULL;
	ccid_response_t *resp = NULL;
	unsigned int len;
	int result = -1;

	cmd = ccid_build_simple_packet(reader, slot, CCID_CMD_GETPARAMS);

	resp = ccid_xfer(reader, cmd, CCID_RESP_PARAMS);
	if (resp == NULL)
		goto done;

	len = buffer_available(resp->payload);
	if (len > size)
		len = size;
	if (buffer_get(resp->payload, parambuf, len))
		result = len;
done:
	if (cmd)
		ccid_command_free(cmd);
	if (resp)
		ccid_response_free(resp);
	return result;
}

static bool
ccid_reader_setparams(ccid_reader_t *reader, unsigned int slot, unsigned int t, const unsigned char *parambuf, unsigned int len)
{
	ccid_command_t *cmd = NULL;
	ccid_response_t *resp = NULL;
	unsigned char ctl[3] = { t, 0, 0 };
	bool okay = false;

	cmd = ccid_build_command(reader, slot, CCID_CMD_GETPARAMS, ctl, parambuf, len);

	resp = ccid_xfer(reader, cmd, CCID_RESP_PARAMS);
	if (resp == NULL)
		goto done;

	okay = true;

done:
	if (cmd)
		ccid_command_free(cmd);
	if (resp)
		ccid_response_free(resp);
	return okay;
}

bool
ccid_reader_select_protocol(ccid_reader_t *reader, unsigned int slot, unsigned int t)
{
	unsigned char parambuf[7];
	int len;

	memset(parambuf, 0, sizeof(parambuf));
	len = ccid_reader_getparams(reader, slot, parambuf, sizeof(parambuf));
	if (len < 0)
		return false;

	if (t == 0) {
		len = 5;
	} else {
		len = 7;
	}

	return ccid_reader_setparams(reader, slot, t, parambuf, len);
}

buffer_t *
ccid_reader_apdu_xfer(ccid_reader_t *reader, unsigned int slot, buffer_t *apdu)
{
	ccid_command_t *cmd = NULL;
	ccid_response_t *resp = NULL;
	buffer_t *rapdu = NULL;

	cmd = ccid_build_command(reader, slot, CCID_CMD_XFRBLOCK, NULL,
			buffer_read_pointer(apdu),
			buffer_available(apdu));
	if (cmd == NULL)
		goto done;

	resp = ccid_xfer(reader, cmd, CCID_RESP_DATA);
	if (resp == NULL)
		goto done;

	rapdu = resp->payload;
	resp->payload = NULL;

done:
	if (cmd)
		ccid_command_free(cmd);
	if (resp)
		ccid_response_free(resp);
	return rapdu;
}

bool
ccid_reader_set_features(ccid_reader_t *reader, const ccid_descriptor_t *ccid)
{
	unsigned int f = ccid->dwFeatures;

	if (f & 0x60000) {
		debug("Reader supports APDU exchange\n");
	} else {
		error("Reader does not support APDU exchange; other modes currently not implemented\n");
		return false;
	}

	printf("Reader features");

	if (ccid->dwFeatures & 0x2)
		printf(" FLAG_AUTO_ATRPARSE");
	if (ccid->dwFeatures & 0x4) {
		printf(" FLAG_AUTO_ACTIVATE");
		reader->auto_voltage = true;
	}
	if (ccid->dwFeatures & 0x8) {
		printf(" AUTO_VOLTAGE");
		reader->auto_voltage = true;
	}
	if (ccid->dwFeatures & 0x40)
		printf(" FLAG_NO_PTS FLAG_NO_SETPARAM");
	if (ccid->dwFeatures & 0x80)
		printf(" FLAG_NO_PTS");


	printf("\n");
	return true;
}
