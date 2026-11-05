# USB IDs (pid.codes)

OpenWheel uses VID **0x1209** (pid.codes, the shared vendor ID for open-source hardware). The PID
identifies the product; the ASCOM and INDI drivers find the wheel by VID/PID and then confirm it with
the `:32#` identify command.

| PID    | Product                | Status |
|--------|------------------------|--------|
| 0x2180 | OpenWheel Filter Wheel | requested: <https://github.com/pidcodes/pidcodes.github.com/pull/1323> |
| 0x0001 | pid.codes test PID     | used by firmware builds until 0x2180 is assigned |

The test PID 0x0001 is shared by many test devices: do not ship it, and do not run two different
0x0001 devices on one PC with the ASCOM driver. The drivers accept both PIDs.

After 0x2180 is assigned: set `USBD_PID=0x2180` in `fw/platformio.ini`, rebuild and reflash. The
wheel re-enumerates with the new ID (and usually a new COM port number); the drivers don't care.
