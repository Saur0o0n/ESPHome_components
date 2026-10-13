# Mixing valve climate controller

`mixing_valve_climate` controls mixed-water temperature by driving a
[`time_based_valve`](../time_based_valve/README.md) in pulses. Opening must admit
more hot water; closing must reduce the hot-water contribution. The temperature
sensor belongs downstream of the mixer. Return-water and boiler temperatures
are not used in this version.

The controller has one target temperature. `AUTO` controls both directions;
`OFF` stops regulation. No separate heat/cool setpoints, supplemental actions,
PID tuning, or relay scripts are required. This component does not request heat
from a boiler or control a circulation pump.

## Installation

Select both components from the same revision:

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve, mixing_valve_climate]

climate:
  - platform: mixing_valve_climate
    id: water_controller
    name: Mixed water controller
    sensor: mixed_water_temperature
    valve_id: mixing_valve
    default_target_temperature: 30°C
    deadband: 2°C
    fast_close_offset: 5°C
    fast_close_hysteresis: 1°C
    deadband_number:
      name: Mixed water temperature tolerance
    fast_close_offset_number:
      name: Mixed water fast closing offset
    max_temperature: 50°C
    sensor_timeout: 30s
    default_mode: 'OFF'
    heat_source_required: true
    heat_source_timeout: 90s
    status:
      name: Mixed water controller status
```

`mixing_valve` must be an existing `time_based_valve` entity. Pin a tested commit
instead of `main` when deploying. Complete compilation examples are provided for
[ESP8266](../../examples/mixing_valve_climate/esp8266.yaml) and
[ESP32](../../examples/mixing_valve_climate/esp32.yaml). They contain a demonstration
temperature sensor and fictional Wi-Fi credentials; adapt them before use.

## Regulation

For a target of **30°C** and `deadband: 2°C`:

| Measured temperature | Command |
|---|---|
| Below 28°C, heat available | Pulsed opening toward 100% |
| From 28°C through 32°C, inclusive | STOP, including cancellation of pending pulses |
| Above 32°C through 37°C, before fast closing activates | Pulsed closing toward 0% |
| Above 37°C | Continuous closing toward 0%; remain continuous until at/below 36°C |
| Below 28°C, heat unavailable or unknown | STOP; do not open |
| At or above `max_temperature` | Continuous closing, including in OFF |
| Invalid or expired temperature, in AUTO | Continuous closing |

Fast closing starts strictly above `target + deadband + fast_close_offset`.
After activation it stays continuous until temperature falls to or below that
threshold minus `fast_close_hysteresis`. For **29°C** with the defaults, fast
closing starts **above 36°C** and returns to pulsed closing **at/below 35°C**.
Between 35°C and 36°C the previous speed determines the current speed. Entering
the idle band always stops motion, even after a sudden temperature drop.
Opening remains pulsed regardless of how far temperature is below the target.
The independent maximum-temperature protection has higher priority than these
thresholds; at high setpoints it can activate before fast closing would.

Each new measurement immediately re-evaluates the command. The loop also checks
for timeouts and completed initialization every 100 ms, subject to ESPHome loop
latency. Repeated measurements do not restart pulse timers. The valve owns pulse
lengths, pauses, position estimation, and endpoint overrun. At a completed
endpoint, ongoing demand does not repeatedly restart its overrun.

Configure the asymmetric response on the **valve**:

```yaml
open_pulse_duration: 2s
open_pause_duration: 8s
close_pulse_duration: 2s
close_pause_duration: 4s
```

These are also the valve defaults. The controller requests movement until the
measured temperature enters the band; it does not calculate a target valve
percentage from a thermal model. A valve percentage is an estimated mechanical
position, not a measured proportion of hot water.

The climate action reports the requested regulation direction (`HEATING` or
`COOLING`), including pauses and endpoint saturation. `COOLING` means reducing
hot-water input; it does not imply a refrigerating device. Within the band it
reports `IDLE`. `status` distinguishes waiting, startup, and protection states.

## Configuration

| Option | Default | Meaning |
|---|---|---|
| `sensor` | Required | Mixed-water temperature sensor ID, in Celsius |
| `valve_id` | Required | `time_based_valve` ID; one controller per valve |
| `default_target_temperature` | `30°C` | Target when no restored setting is used |
| `min_target_temperature` | `18°C` | Enforced minimum target |
| `max_target_temperature` | `46°C` | Enforced maximum target |
| `deadband` | `2°C` | Allowed deviation on **each** side of target; positive |
| `fast_close_offset` | `5°C` | Additional excess **above the upper idle boundary** that starts continuous closing |
| `fast_close_hysteresis` | `1°C` | Temperature fall below the fast threshold required to resume pulsed closing |
| `deadband_number` | Omitted | Optional HA number entity for changing the tolerance at runtime |
| `fast_close_offset_number` | Omitted | Optional HA number entity for changing the fast closing offset at runtime |
| `max_temperature` | `50°C` | Measured-temperature threshold for continuous closing |
| `overtemperature_hysteresis` | `2°C` | A fresh sample at/below maximum minus this difference clears protection |
| `sensor_timeout` | `30s` | Maximum age of a received temperature sample |
| `heat_source_required` | `false` | Require a fresh `available` heartbeat before opening |
| `heat_source_timeout` | `90s` | Validity of the last heat-source heartbeat |
| `default_mode` | `OFF` | `OFF` or `AUTO`, used when settings are not restored |
| `restore_state` | `true` | Restore the saved mode and target after reboot |
| `status` | Omitted | Optional text sensor for controller status |

Timeouts accept 1 through 2147483647 milliseconds. Target limits must be ordered,
and the default target must lie within them. `max_target_temperature + deadband`
must be strictly below `max_temperature`. The reset threshold must be above the
maximum target. Standard climate `visual` options only change presentation;
they do not replace the enforced limits above.

Set `sensor_timeout` longer than the sensor's actual reporting interval,
including filters. A repeated identical measurement refreshes it; no new
measurement does not. Deduplication/throttling filters can therefore cause a
sensor fault. Invalid values (including NaN/infinity) cause a fault immediately.
A fresh finite reading automatically restores normal evaluation.

`manual_control: true` on the linked valve is rejected because its external-button
position assumptions conflict with automatic regulation. Hardware direction
interlocks and immediate, simple valve open/close/stop actions remain necessary.

## Runtime temperature settings in Home Assistant

Configure `deadband_number` and `fast_close_offset_number` under each climate
entity, as shown above, to expose two independent configuration numbers in HA.
They accept ordinary number entity options such as `name`, `id`, and `mode`.
There is no optimistic template or separate relay automation involved: a number
command changes the controller directly and publishes its accepted value.

- Both values use Celsius differences, in steps of 0.1°C.
- Tolerance ranges from 0.1°C to the largest 0.1°C step strictly below
  `max_temperature - max_target_temperature`. With the default 50°C/46°C
  limits, the allowed range is **0.1–3.9°C**. This protects the whole selectable
  setpoint range, not only the current target.
- Fast closing offset ranges from `fast_close_hysteresis` to 20°C: **1–20°C**
  with the default hysteresis. The reset threshold cannot be below the idle band.
- A changed target or parameter immediately recomputes the motion decision,
  clearing the old fast-speed latch. Repeating an unchanged value preserves the
  hysteresis and pulse timing. Invalid dynamic values are rejected.
- With `restore_state: true`, changed parameters are saved separately from the
  climate mode/target and restored on reboot. YAML values are initial/fallback
  values; changing a YAML default does not override a valid saved setting.
  Values incompatible with new configured limits fall back to their YAML defaults.
- With `restore_state: false`, reboot uses the YAML defaults. Normal ESPHome
  preference flushing applies; an immediate power cut can lose a recent change.
- `fast_close_hysteresis` remains a YAML setting, default 1°C. Only the tolerance
  and extra fast-closing offset are exposed as runtime numbers.

The status text is `fast_closing` during continuous temperature regulation;
`overtemperature` and `sensor_fault` retain their separate meanings. Changing
speed at a completed closed endpoint does not initiate another overrun.

## Remote boiler state through Home Assistant

With `heat_source_required: true`, the controller initially treats the source as
unknown. Both `unavailable` (for example, a DHW cycle) and `unknown` block opening
but permit temperature-driven closing. Closing does not depend on HA. With the
option disabled, normal regulation needs only the local temperature measurement.

The action below accepts `available`, `unavailable`, or `unknown`:

```yaml
- mixing_valve_climate.set_heat_source:
    id: water_controller
    state: available
```

`state` also accepts a lambda returning a string. Unrecognized dynamic strings
are treated as unknown. Every call is a fresh heartbeat, even if its value is
unchanged. Availability is never persisted across reboot.

For HA, expose a native API action:

```yaml
api:
  actions:
    - action: heat_source_heartbeat
      variables:
        state: string
      then:
        - mixing_valve_climate.set_heat_source:
            id: water_controller
            state: !lambda return state;
```

For two controllers, put two `mixing_valve_climate.set_heat_source` actions under
`then`, referencing their respective IDs. The boiler signal can be shared.

Use the [HA automation example](../../examples/mixing_valve_climate/home-assistant.yaml)
to send a heartbeat every 30 seconds and immediately on source-state changes.
Replace its placeholder boiler entities and generated ESPHome action name.
The boiler-connected entity must represent the **boiler device's** connectivity.
A confirmed active floor-heating circuit maps to `available`, an inactive one to
`unavailable`, and any missing/unknown/disconnected input to `unknown`.

The heartbeat proves the HA-to-controller path was refreshed. It does not prove
the boiler telemetry itself is current. The producer must check availability and,
where necessary, telemetry freshness before sending `available`. Do not refresh
from an unchecked cached boolean. A legitimate boiler state can stay unchanged
for hours, so its last-change time alone is not a freshness test.

When HA or the delivery path disappears, opening is inhibited after the configured
timeout (up to 90 s by default), not immediately. A received `unknown` or
`unavailable` message inhibits opening immediately. The timeout is a policy
setting, not a physical protection response time.

Reference: [ESPHome user-defined API actions](https://esphome.io/components/api/#user-defined-actions)
and [Home Assistant time-pattern triggers](https://www.home-assistant.io/docs/automation/trigger/#time-pattern-trigger).

## Startup, OFF and protection

- Normal regulation waits for the valve's `start_position` run to finish. It
  neither stops it nor queues control commands during initialization. Prefer
  `start_position: closed` for this application.
- Overtemperature has higher priority than initialization and OFF. A closing
  startup run continues for its full travel. An opening startup run is stopped
  and replaced with a full continuous closing traversal because position was
  still unknown. This exceptional close also uses the configured endpoint overrun.
- Invalid/missing/stale sensor data in AUTO closes continuously once startup
  positioning is finished. Sensor faults do not interrupt normal startup homing.
- Overtemperature clears only on a fresh finite sample at or below the reset
  threshold. A sensor failure cannot clear an active overtemperature condition.
- Protection closes only the valve. It does **not** turn off pumps, shut down a
  boiler, or implement the installation's other emergency actions. Retain those
  separately. This software cannot guarantee a maximum physical water temperature
  with a 120-second actuator and delayed temperature measurement.
- Entering OFF stops regulation and then permits manual valve commands. An
  explicit repeated OFF command stops a manual move too. Startup homing is allowed
  to finish; overtemperature protection remains active. Thus OFF does not always
  imply that the actuator is stationary.
- In AUTO the controller owns the valve. Direct relay commands, old thermostats,
  scripts with delayed STOP, and competing valve automations must be removed or
  disabled for that valve. Use OFF for manual assistance; AUTO resumes automatic
  regulation.
- Setpoint and mode restoration uses ESPHome climate preferences and the normal
  platform flash-write policy. Very recent changes may not survive immediate
  power loss. Pulse sequences and remote availability are not restored.

Status values: `off`, `initializing`, `sensor_fault`, `overtemperature`,
`waiting_for_heat`, `idle`, `opening`, `closing`, `fast_closing`. During protection or a sensor
fault, `COOLING` denotes a closing demand even after the valve reaches 0%.

## Migrating an existing thermostat

1. Select both external components at the same commit.
2. Keep the existing valve IDs, physical actions, timings and temperature sensors.
3. Replace the old thermostat for each valve with `mixing_valve_climate` and one
   target temperature. HA will offer `AUTO` and `OFF`, not `HEAT_COOL` or two targets.
4. Remove the old supplemental-action/relay-pulse control path, including delayed
   STOP actions, and update automations that refer to the old climate modes or IDs.
5. Add the heat-source heartbeat before using `heat_source_required: true`.
6. Start in OFF, check the measured temperature and status, then enable AUTO with
   a supervised target. Verify direction, timing, CWU interruption, signal loss,
   temperature sensor failure, and protection on the actual installation.

The public examples do not modify an existing private heating configuration.
See [validation results](VALIDATION.md) for what has actually been tested.
