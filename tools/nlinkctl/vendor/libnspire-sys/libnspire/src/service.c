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

#include "error.h"
#include "data.h"
#include "packet.h"
#include <stdio.h>
#include <stdlib.h>

int service_connect(nspire_handle_t *handle, uint16_t sid) {
	if (handle->connected)
		return -NSPIRE_ERR_BUSY;

	handle->connected = 1;
	handle->device_sid = sid;

	return NSPIRE_ERR_SUCCESS;
}

static void mod_src(struct packet *p) {
	p->src_sid = 0x40DE;
}

int service_disconnect(nspire_handle_t *handle) {
	int ret;
	uint8_t data[] = {
		(handle->host_sid>>8) & 0xFF,
		(handle->host_sid>>0) & 0xFF };

	if (!handle->connected)
		return NSPIRE_ERR_SUCCESS;

	/*
	 * A lost disconnect ACK must not poison the host handle. The service
	 * helpers historically ignored this function's return value, so returning
	 * early left connected=1 and every later operation failed with BUSY even
	 * though the preceding upload or stat had succeeded. Advance the local
	 * service epoch unconditionally; optional service tracing still records the
	 * transport error for diagnosis.
	 */
	ret = data_write_special(handle, &data, 2, mod_src);
	if (ret && getenv("NSPIRE_TRACE_SERVICES"))
		fprintf(stderr, "service_disconnect host_sid=%04x ret=%d\n",
			handle->host_sid, ret);
	handle->connected = 0;
	handle->host_sid++;

	return ret;
}
