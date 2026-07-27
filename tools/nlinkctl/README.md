# phy-nlinkctl

`phy-nlinkctl` is the repository-owned, GPL-3.0 command-line transfer tool for
TI-Nspire CX II development. It vendors the small C transport layer from
`libnspire-sys 0.3.4` and patches three CX II failure modes:

- a lost NNSE ACK now causes a bounded retransmission with the retry bit set;
- the former ten 60-second waits are split into a short packet-ACK timeout and
  a longer, separately configurable handshake/response timeout;
- fragmented NNSE reads validate the checksum over the complete packet, retain
  any coalesced following packet in a per-device receive buffer, and reject
  zero-length/invalid frames rather than looping, underflowing, or discarding
  the next ACK;
- an incoming service response that races its request ACK is deferred to the
  next receive call instead of being consumed by the ACK loop;
- the outer NNSE frame and inner NavNet/CSP file packet are both acknowledged,
  so physical CX II reads continue beyond the first receive window;
- a complete file-service failure restarts the transfer up to
  `--file-attempts` times;
- every completed calculator service is followed by a configurable
  `--service-settle-ms` quiet interval (250 ms by default), preventing a new
  service from racing firmware cleanup through `usbipd`.

The packet payload is also configurable. Uploads default to 1280 bytes. On the
project's `usbipd`/WSL CX II path, two 1024-byte attempts at the current
1,095,275-byte application reached a LibUSB failure at about 55 seconds; after
reattaching the device, a 1280-byte upload plus full SHA-256 readback and atomic
promotion completed in 18.4 seconds. Reads retain the calculator's native
1440-byte framing: requesting a smaller read truncates each inbound CSP frame
because upstream libnspire has no remainder buffer. Direct Linux USB can still
opt into 1440-byte uploads with `--cx2-packet-size 1440`.

The complete 2026-07-27 project sync reused and SHA-256-verified the
1,105,773-byte application, uploaded and SHA-256-verified the 13,588-byte tour
notebook, atomically promoted both, removed the old examples probe, and
performed the final directory checks in 36.6 seconds. The verified hashes were
`7eb36249ca1fadf32c3614d57fbb9c441a10745dc9bd7503889bf46017069791`
and `718a0a40fcd68c57113b88f3a3fe24bbb6463d9f936f1481709a0ca17498e90a`.

Local inputs are read-only memory maps rather than file-sized heap buffers.
There is no project-specific size threshold: every file length representable by
the calculator protocol is accepted. The protocol's length field is 32-bit, so
the exact per-file ceiling is `4,294,967,295` bytes; calculator free space is
normally the tighter bound. “Any size” in this tool means any size within those
device/protocol limits, not an unbounded byte stream.

## Build in WSL

```sh
cd /path/to/NspirePhysics/tools/nlinkctl
cargo test
cargo build --release
```

The calculator USB node must be writable. With `usbipd` this is simplest by
running the built binary as WSL root:

```powershell
usbipd attach --wsl --busid 4-1
wsl.exe -u root -- /path/to/phy-nlinkctl ls /
```

## Safe application deployment

```sh
phy-nlinkctl deploy ../../dist/phy-nspire.tns
```

`deploy` uploads to `/phy-nspire/phy-nspire.upload.tns`, checks its size and
SHA-256 by reading it back, moves the installed program to
`phy-nspire.previous.tns`, and only then moves the verified upload into place.
The rollback copy is kept by default. It never reads, changes, or deletes the
`notebooks/` directory.

If a prior deployment completed the temporary upload but stopped during
verification, resume without retransmitting it:

```sh
phy-nlinkctl deploy ../../dist/phy-nspire.tns --reuse-temporary
```

Direct diagnostic operations are also available:

```sh
phy-nlinkctl ls /phy-nspire
phy-nlinkctl stat /phy-nspire/phy-nspire.tns
phy-nlinkctl upload local.tns /phy-nspire/probe.tns
phy-nlinkctl download /phy-nspire/probe.tns ./probe-readback.tns
```

## Multi-file project sync

CX II firmware can be slow to accept a fresh NNSE handshake immediately after a
process disconnects. Starting one CLI process per file therefore makes a
multi-artifact project less reliable as it grows. `sync` keeps one USB handle
and one handshake for all atomic deployments, then performs requested cleanup
only after every new file has passed size and SHA-256 verification:

```sh
phy-nlinkctl sync \
  --upload ../../dist/phy-nspire.tns /phy-nspire/phy-nspire.tns \
  --upload ../../examples/phy-nspire-cas-tour.tns \
           /phy-nspire/notebooks/phy-nspire-cas-tour.tns \
  --clean-dir /phy-nspire/examples
```

Repeat `--upload LOCAL REMOTE` and `--clean-dir REMOTE` as needed. Missing
parent directories are created. Every destination uses sibling `.upload` and
`.previous` files, cleanup refuses the calculator root, and the final pass
confirms destination sizes, absence of stale `.upload` files, and empty cleaned
directories. Use `--service-settle-ms 0` only on a direct USB path that has
demonstrated reliable back-to-back service transitions.

## Upstream and license

The vendored transport originates from `libnspire-sys 0.3.4` in
<https://github.com/lights0123/libnspire-rs>. Both the tool and the transport
are GPL-3.0. The upstream license is retained at
`vendor/libnspire-sys/libnspire/COPYING`.
