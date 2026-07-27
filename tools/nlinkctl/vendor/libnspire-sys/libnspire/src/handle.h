/*
    This file is part of libnspire.

    libnspire is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libnspire is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libnspire.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _HANDLE_H
#define _HANDLE_H

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>

#include "api/handle.h"
#include "usb.h"

#define NSP_CX2_RX_BUFFER_SIZE 4096
#define NSP_CX2_DEFERRED_PACKET_SIZE 1536

struct nspire_handle {
	usb_device_t device;

	uint16_t host_addr, device_addr;
	uint16_t host_sid, device_sid;
	uint8_t seq, connected;

	bool is_cx2;
	bool cx2_handshake_complete;
	uint8_t cx2_rx_pending[NSP_CX2_RX_BUFFER_SIZE];
	size_t cx2_rx_pending_size;
	uint8_t cx2_deferred_packet[NSP_CX2_DEFERRED_PACKET_SIZE];
	size_t cx2_deferred_packet_size;
	uint16_t cx2_last_stream_seqno;
	bool cx2_last_stream_seqno_valid;
};

#endif
