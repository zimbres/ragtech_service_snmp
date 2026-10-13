# Ragtech UPS SNMP agent

Read-only SNMP v2c agent for Ragtech/Easy Pro Family 10 UPS devices connected
through a serial port at 2560 baud. Written in C11 using Linux/POSIX APIs,
native platform APIs only (Linux pthreads/libm or Windows system libraries).
No third-party runtime libraries or separate SNMP daemon. Native Windows build,
COM-port configuration, ZIP packaging and automatic startup instructions are in
[windows/README.md](windows/README.md).

## Build and run on Linux

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
./build/ragtech_service -c ragtech.conf -t
./build/ragtech_service -c ragtech.conf -f
```

Edit `ragtech.conf`: set the serial device, calibration/battery capacity, and
`[snmp]` bind address, UDP port and community. Defaults are `0.0.0.0:161` and
`public`. UDP 161 requires root or `CAP_NET_BIND_SERVICE`; for unprivileged
testing use port 1161. SNMP v2c communities travel in clear text; restrict access
to your monitoring network and change the community before deployment.
Configuration changes require restarting the service.

Optional monitoring client examples (Net-SNMP is not an agent dependency):

```sh
snmpget -v2c -c public localhost:161 1.3.6.1.2.1.33.1.4.1.0
snmpwalk -v2c -c public localhost:161 1.3.6.1.2.1.33
snmpbulkwalk -v2c -c public localhost:161 1.3.6.1.3.1628.1
```

The agent supports GET, GETNEXT and GETBULK, multiple varbinds, numeric OID
ordering and SNMP v2 exception values. SET returns `notWritable`. Other SNMP
versions and incorrect communities are silently discarded. Requests and
responses are bounded to 8192 bytes and 64 varbinds; bulk output is limited to
64 varbinds. No traps or remote UPS control are implemented. The agent binds
an IPv4 address; it must have its own address/port if another SNMP agent runs
on the host.

## RFC1628 UPS-MIB

Implemented readable objects follow [RFC1628](https://www.rfc-editor.org/rfc/rfc1628).
This is a telemetry subset, not full UPS-MIB compliance. All eight required
objects below are implemented at their exact scalar/table instance OIDs.

| OID | Object | Type / units |
| --- | --- | --- |
| 1.3.6.1.2.1.33.1.1.1.0 | upsIdentManufacturer | OCTET STRING: Ragtech |
| 1.3.6.1.2.1.33.1.1.2.0 | upsIdentModel | OCTET STRING: detected model |
| 1.3.6.1.2.1.33.1.4.1.0 | upsOutputSource | INTEGER: other(1), none(2), normal(3), battery(5) |
| 1.3.6.1.2.1.33.1.2.1.0 | upsBatteryStatus | INTEGER: unknown(1), normal(2), low(3), depleted(4) |
| 1.3.6.1.2.1.33.1.2.2.0 | upsSecondsOnBattery | INTEGER: seconds |
| 1.3.6.1.2.1.33.1.2.3.0 | upsEstimatedMinutesRemaining | INTEGER: minutes |
| 1.3.6.1.2.1.33.1.2.4.0 | upsEstimatedChargeRemaining | INTEGER: 0–100% |
| 1.3.6.1.2.1.33.1.4.4.1.5.1 | upsOutputPercentLoad.1 | INTEGER: percent |

Additional objects (suffixes relative to `1.3.6.1.2.1.33.1`):

| Suffix | Object | Units |
| --- | --- | --- |
| .1.4.0 | upsIdentAgentSoftwareVersion | OCTET STRING |
| .2.5.0 | upsBatteryVoltage | 0.1 V DC |
| .3.2.0 | upsInputNumLines | 1 |
| .3.3.1.3.1 | upsInputVoltage.1 | RMS V |
| .4.2.0 | upsOutputFrequency | 0.1 Hz |
| .4.3.0 | upsOutputNumLines | 1 |
| .4.4.1.2.1 | upsOutputVoltage.1 | RMS V |
| .4.4.1.3.1 | upsOutputCurrent.1 | 0.1 A RMS |
| .4.4.1.4.1 | upsOutputPower.1 | W (voltage × current estimate) |

Integer readings are rounded to RFC1628 units. upsBatteryVoltage,
upsOutputFrequency and upsOutputCurrent.1 use tenths of volts, hertz and amps;
divide their SNMP values by 10 to display physical units. upsOutputPower.1
returns estimated watts (voltage × current); the proprietary `pOutput` field
is percentage load and maps to upsOutputPercentLoad.1. For the supplied
proprietary sample the SNMP values are 136, 597, 25, 276 and 32 respectively,
representing 13.6 V, 59.7 Hz, 2.5 A, approximately 276 W and 32% load.
The experimental metrics retain hundredths precision (e.g. battery voltage
1360 means 13.60 V, frequency 5971 means 59.71 Hz).
Runtime is estimated on mains and
battery as capacity(Ah) × nominal voltage(V) × charge fraction / output(W),
capped at 1440 minutes. Positive sub-minute estimates return 1; when no estimate
is possible, the runtime instance returns `noSuchInstance`. Battery flags drive
status (missing/depleted takes precedence over low). Battery mode uses the
operational flag, with the existing fallback of input <50 V and output >10 W.
Elapsed battery time uses a monotonic clock from the first observed battery
sample and resets on mains or communication loss.

The device does not expose a reliable bypass/boost/reduce indicator, battery
current, input current/frequency, or firmware version; these are not fabricated.
UPS temperature is exposed in the extension because the sensor location is
unknown. Input and output power retain the existing estimates and are not
measurements of power factor. Disconnected UPS status returns unknown/other,
seconds on battery returns zero, and RFC telemetry returns `noSuchInstance`.
Identification retains the last detected model (or `Unknown` before detection).

## All UPS data and diagnostic extensions

The locally chosen **experimental**, unregistered subtree `1.3.6.1.3.1628.1`
exposes all 13 metrics, all 20 flags, connection state and raw response bytes.
It is not part of RFC1628 or an assigned enterprise MIB. The extensions retain
the last sample on disconnection; check `.3.0` before using those readings.

Each metric is INTEGER, scaled by 100 (divide by 100 to recover the original
unit), at `<base>.1.<index>.0`:

| Index | Metric | Original unit |
| --- | --- | --- |
| 1 | power_out_percent | % |
| 2 | current_out | A |
| 3 | voltage_out | V |
| 4 | voltage_in | V |
| 5 | power_out | W |
| 6 | power_in | W |
| 7 | energy_out | kWh |
| 8 | energy_in | kWh |
| 9 | temperature | °C |
| 10 | battery_state | % |
| 11 | battery_voltage | V |
| 12 | frequency | Hz |
| 13 | battery_runtime | min |

Each flag is INTEGER 0=false, 1=true, at `<base>.2.<index>.0`:

| Index | Flag | Index | Flag |
| --- | --- | --- | --- |
| 1 | no_battery | 11 | fail_end_battery |
| 2 | old_battery | 12 | fail_overload |
| 3 | op_checkup | 13 | fail_abnormal_vout |
| 4 | no_v_input | 14 | fail_abnormal_vbat |
| 5 | lo_v_input | 15 | fail_inverter |
| 6 | hi_v_input | 16 | fail_shortcircuit |
| 7 | op_battery | 17 | sync_input |
| 8 | hi_p_output | 18 | more_battery |
| 9 | lo_battery | 19 | less_battery |
| 10 | fail_overtemp | 20 | remote_control_active |

`<base>.3.0` is INTEGER connection state (0=disconnected, 1=connected).
`<base>.4.0` is an OCTET STRING containing the last raw UPS packet.
All objects in a response use one synchronized snapshot.

## MIB files and Zabbix

The included [RAGTECH-UPS-MIB](mibs/RAGTECH-UPS-MIB.txt) defines every extension
metric, status flag and diagnostic object. Load it alongside the standard
IETF `UPS-MIB` for the RFC1628 objects. See [MIB installation and Zabbix item
examples](mibs/README.md), including preprocessing multipliers. CMake installs
the custom MIB under `share/snmp/mibs`; no agent dependency is added.

The [Zabbix template](zabbix/ragtech_ups_snmp.json) includes all 52 exposed
objects, scaling, value maps, triggers and graphs. See the [template setup and
API installer](zabbix/README.md).

## Install

```sh
sudo cmake --install build
# Create a ragtech system user before starting the service.
sudo systemctl daemon-reload
sudo systemctl enable --now ragtech.service
```

CMake installs `ragtech.conf` under `etc/ragtech` and `ragtech.service` under
`lib/systemd/system`, relative to the install prefix (default `/usr/local`).
The supplied service uses `/etc/ragtech/ragtech.conf`; copy the installed
example there and adjust it before starting. The service runs in foreground,
grants low-port binding capability and dialout access, and creates state/runtime
directories. Energy totals persist in `/var/lib/ragtech/energy.dat` on Linux. The optional
`[general] state_directory` setting can select another directory.

## Tests

Python 3 is needed only for the optional protocol tests:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The independent Python BER client sends encoded packets through a C fixture
using synthetic UPS readings. It checks all required OIDs, types, RFC scaling,
mains/battery/low/depleted states, disconnected behavior, complete walks,
GETBULK ordering and limits, exceptions, SET rejection and malformed packets.
It does not require UDP sockets or UPS hardware. Native platform, CLI and UDP transport tests are included; UDP tests skip
where the environment forbids sockets. Family 10 tests also check
register command bytes, fragmented/invalid replies and conversion factors for
all 20 models against XML defaults and explicit overrides. SNMP tests cover missing oscillator
calibration. These simulated tests do not establish physical-device acceptance
of the new serial commands.

## Serial protocol

Family 10 polling command: `AA 04 00 80 1E 9E`; responses are 31 bytes starting
with `AA`. Model is register 0x009A (buffer[27]). All 20 Family 10 Easy TI/M2
models in `devices.xml` are recognized, including 600 through 2200 VA.
Family defaults plus explicit model overrides supply the
current/output-voltage/battery-voltage factors. EASY 700 TI (model 2) uses the
family default current factor **4.30**. The vendor's `config.so` configuration
loader applies default variables followed by the selected model's own overrides;
`from="0"` does not justify inheriting model 0's 3.20 current override.
Vendor samples confirm raw current 30/31 with IOUTCALIB 53 produce
2.433962/2.515094 A. The previous 3.20 factor underestimated current and power
and overestimated runtime for this model. Accumulated energy recorded with the
old factor is retained; the correction applies to subsequent samples.

Calibration reads now follow the compact framing observed for normal polling:
`AA 04 address-hi address-lo count sum(address-hi,address-lo,count)`.
IOUTCALIB at `00F3` uses command `AA 04 00 F3 01 F4`, expecting `AA` plus one
register byte. Oscillator registers `0202`/`0203` use `AA 04 02 02 02 06`,
expecting `AA` plus two bytes. This extends the observed polling convention;
`devices.xml` specifies register ranges and formulas, not serial framing.
**The new calibration transactions still require verification on real hardware.**

The reader accumulates fragmented bytes under one total timeout, rejects
unframed replies (such as the previously observed `FD`) and unexpected trailing
bytes, and logs commands/responses at debug level. Positive one-byte calibration
values (1-255) are accepted; zero or failed reads use the existing configured
fallback. The example's fallback 53.0 is consistent with the supplied vendor
current samples; a valid device calibration still takes priority.
Calibration is read at startup and
again after reconnection (a previous valid current calibration can be retained).

Frequency follows the XML formula:
`(53 + 4 * (raw - OSC53) / (OSC57 - OSC53)) * 0.9806 + 1.46`.
If oscillator reads fail or the two coefficients are equal, standard and
extension frequency OIDs return `noSuchInstance` rather than an approximation.
Custom 2560 baud uses Linux termios2/BOTHER. Serial failures trigger reconnects.
