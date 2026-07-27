# phy-nlinkctl

`phy-nlinkctl` is the repository-owned, GPL-3.0 command-line transfer tool for
TI-Nspire CX II development. It vendors the small C transport layer from
`libnspire-sys 0.3.4` and patches three CX II failure modes:

- a lost NNSE ACK now causes a bounded retransmission with the retry bit set;
- the former ten 60-second waits are split into a short packet-ACK timeout and
  a longer, separately configurable handshake/response timeout;
- fragmented NNSE reads validate the checksum over the complete packet.

The packet payload is also configurable. Uploads default to 1280 bytes. On the
project's `usbipd`/WSL CX II path, two 1024-byte attempts at the current
1,095,275-byte application reached a LibUSB failure at about 55 seconds; after
reattaching the device, a 1280-byte upload plus full SHA-256 readback and atomic
promotion completed in 18.4 seconds. Reads retain the calculator's native
1440-byte framing: requesting a smaller read truncates each inbound CSP frame
because upstream libnspire has no remainder buffer. Direct Linux USB can still
opt into 1440-byte uploads with `--cx2-packet-size 1440`.

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

## Upstream and license

The vendored transport originates from `libnspire-sys 0.3.4` in
<https://github.com/lights0123/libnspire-rs>. Both the tool and the transport
are GPL-3.0. The upstream license is retained at
`vendor/libnspire-sys/libnspire/COPYING`.
