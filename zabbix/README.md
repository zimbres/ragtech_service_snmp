# Ragtech UPS Zabbix template

`ragtech_ups_snmp.json` is a Zabbix 6.0-format template intended for Zabbix 6.0
and newer (newer releases convert older export formats during import).
Name: **Ragtech UPS by SNMP**, group: **Templates/Power**.
It uses numeric OIDs, so the Zabbix server needs no custom MIB files installed.

Contents:

- All 17 implemented RFC1628 objects, including the eight required readings.
- All 35 extension objects: 13 metrics, 20 flags, connection state and raw bytes.
- RFC tenths converted with multiplier 0.1; extension hundredths with 0.01.
- Four value maps: output source, battery status, flags and serial connection.
- Ten triggers: serial loss, no SNMP data, battery operation, low/depleted
  battery, high load, overtemperature, overload, inverter and short-circuit faults.
- Three graphs: voltages, load/charge and output power.

All scalar OIDs end in `.0`; line readings use `.1`. Items poll every 30 seconds
by default; identification every hour and raw packets every five minutes.
Numeric history is 7 days and trends 365 days; raw packet history is one day.
Output power retains the agent's voltage-times-current estimate.

## Import through the frontend

Open Zabbix Templates (under Data collection in newer releases), choose Import,
and select `ragtech_ups_snmp.json`. Enable creating missing templates, groups,
items, triggers, graphs and value maps. Existing objects with the same identifiers
may be updated; leave deletion of missing objects disabled.

## Install through the API

Run this from a machine that can reach your Zabbix frontend:

```sh
python3 zabbix/install_template.py \
  --url https://zabbix.dev.zimbres.com --username Admin
```

Enter the password at the prompt. The installer accepts `ZABBIX_PASSWORD` for
unattended execution but never writes credentials or API tokens to a file.
It discovers the server version, logs in, imports the template without deleting
missing objects, checks all item OIDs/types/scales plus trigger/graph names, and
logs out. TLS verification remains enabled. Python uses only the standard library.
For Zabbix hosted under a path, include that path in `--url`, or provide the full
`api_jsonrpc.php` URL. The installer handles legacy JSON auth and Zabbix 7.2+
Bearer authentication. API permissions must allow template import.

## Link a UPS host

Link **Ragtech UPS by SNMP** to the UPS host. Configure the host's SNMP interface
with the **address of the machine running ragtech_service**, its configured UDP
port (normally 161), SNMP v2c, and the community from `ragtech.conf`. The frontend
URL is not the UPS SNMP address. The installer imports the template only; it does
not create a host or choose an existing host to link.

Macros:

| Macro | Default | Purpose |
| --- | --- | --- |
| {$UPS.POLL.INTERVAL} | 30s | Polling interval |
| {$UPS.NODATA.TIMEOUT} | 5m | SNMP data absence threshold |
| {$UPS.LOAD.WARN} | 80 | Load warning percentage |

Extension readings retain the last sample while serial communication is lost.
Fault triggers require a connected UPS and depend on the serial-loss trigger.
SNMP no-data and serial loss are separate conditions. Standard telemetry can
return `noSuchInstance` on serial loss; runtime is also unavailable when no
estimate exists. Frequency is unavailable until valid oscillator calibration
is read. No discovery of unsupported RFC1628 objects is configured.

## Regenerate and test

```sh
python3 zabbix/generate_template.py
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The template is generated from the shipped MIB for extensions and the standard
agent's RFC1628 object list. Tests check all 52 OIDs/types against the agent
fixture, preprocessing with the supplied proprietary sample, unique identifiers,
trigger references, and the installer's import/verification logic. Live server
schema acceptance is confirmed only when the API import succeeds.

References: [Zabbix template format](https://www.zabbix.com/documentation/6.0/en/manual/xml_export_import/templates),
[configuration.import](https://www.zabbix.com/documentation/7.0/en/manual/api/reference/configuration/import),
[Zabbix 7.2 API authentication](https://www.zabbix.com/documentation/7.2/en/manual/api).
