# QFT Q-7 CX II measurement record

This is the successor measurement file required by
`docs/agent-tasks/QFT_DIRAC.md` Q-7. It separates facts established by the
build from observations that require the physical calculator.

## Instrument

- target: TI-Nspire CX II CAS, Ndless r2022;
- benchmark source: `tests/device/qft_bench.c`;
- artifact: `dist/phy-qft-bench.tns`;
- artifact size: 42,948 bytes;
- SHA-256:
  `968924364704b363be8c76ba1434029b6d4d79aeb28f4e22ecdecd30c5841876`;
- deployed path: `Documents/phy-nspire/examples/phy-qft-bench.tns`;
- transfer evidence: calculator readback was 42,948 bytes with the same SHA-256;
- timer: CX II SP804 timer 0, 32 kHz, saved and restored with the same register
  discipline as Ndless `msleep`;
- time quantum: 31.25 microseconds; the display rounds to the nearest
  millisecond and also prints raw ticks;
- memory observable: peak bytes routed through `phy_alloc` above the live-byte
  baseline immediately before `phy_dirac_trace_scalar`.

The memory value is **tracked project heap, not process RSS**. Ndless exposes
no process-RSS interface. Q-7's original "peak RSS" wording therefore remains
open unless a reliable OS-level observable is added; it must not be silently
relabelled.

The isolated benchmark ELF has no unresolved `phy_host_*`, libm, floating
formatter, or ARM soft-float symbol.

## Workloads

| Row | Exact workload | Required result |
| --- | --- | --- |
| 4 gamma | four distinct upper Lorentz indices | `PHY_OK`, 3 trace terms |
| 8 gamma | eight distinct upper Lorentz indices | `PHY_OK`, 105 trace terms |
| 12 gamma | six adjacent upper/lower dummy pairs | `PHY_OK`, exact scalar 16384 |
| over limit | four distinct indices with `max_terms = 2` | `PHY_ERR_TERM_LIMIT`, then `Tr[1] = 4` succeeds |

The twelve-gamma row is deliberately contracted. A twelve-distinct-index
trace has 10,395 leaves and exceeds the provisional 4,096-term policy by
definition; timing it as a successful default-policy case would contradict
the stated limit rather than test it.

The same mathematical workloads pass on the strict host suite. That does not
substitute for the device run.

## Physical run

Status: **pending**.

| Row | Raw ticks | Rounded ms | Tracked heap delta | Screen status |
| --- | ---: | ---: | ---: | --- |
| 4 gamma | pending | pending | pending | pending |
| 8 gamma | pending | pending | pending | pending |
| 12 gamma | pending | pending | pending | pending |
| over limit | pending | pending | pending | pending |

After photographing the `4/4 PASS` screen, open the production notebook and
save a small notebook. Record that result here separately; a successful
standalone recovery trace does not prove notebook persistence after an
interrupted or resource-limited evaluation.
