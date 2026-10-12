# ESPHome — Time Based Valve

An external `valve` component for actuators controlled by open/close/stop commands,
with position estimated from travel time. It is based on `cover.time_based`
from ESPHome 2026.8.2 and inherits directly from `valve::Valve`.
It does not create a `cover` entity or replace the built-in `time_based` component.

## Installation from GitHub

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve]
```

Use this source instead of the local source in the example below.
You can pin a specific commit instead of using `main`.

## Local installation

Copy the `components/time_based_valve` directory alongside your device configuration:

```text
config/
├── device.yaml
└── components/
    └── time_based_valve/
        ├── __init__.py
        ├── valve.py
        ├── time_based_valve.h
        └── time_based_valve.cpp
```

Add the following to `device.yaml`:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [time_based_valve]

valve:
  - platform: time_based_valve
    name: "Valve 2"
    id: valve_2
    device_class: water
    assumed_state: false
    start_position: closed
    end_positions_overrun: 5%
    open_duration: 120s
    close_duration: 120s

    open_action:
      - switch.turn_off: valve_2_off
      - switch.turn_on: valve_2_on

    close_action:
      - switch.turn_off: valve_2_on
      - switch.turn_on: valve_2_off

    stop_action:
      - switch.turn_off: valve_2_off
      - switch.turn_off: valve_2_on
```

This snippet uses existing switches named `valve_2_on` and `valve_2_off`.
When reversing direction, it turns off the previous relay first. The component
executes the configured actions; it does not manage pins or relay interlocks itself.
Keep a mutual `interlock` and `restore_mode: ALWAYS_OFF` in the switch definitions.
Relay switching delays depend on the actuator.

Example with two valves: [mixing-valves.yaml](../../examples/time_based_valve/mixing-valves.yaml).
Complete configuration for checking compilation:
[esp8266.yaml](../../examples/time_based_valve/esp8266.yaml). Its GPIO and Wi-Fi settings are examples.
The examples use `../../components` because the main YAML file is in `examples/time_based_valve/`.
Resolve local source paths relative to the device's main YAML file, including
when using `packages`.

## Features and settings

| Setting | Description | Default |
|---|---|---|
| `open_duration` | Full travel time from 0% to 100% | required |
| `close_duration` | Full travel time from 100% to 0% | required |
| `open_action` | Action that starts opening | required |
| `close_action` | Action that starts closing | required |
| `stop_action` | Action that stops movement | required |
| `assumed_state` | Indicates to the UI that the state is estimated | `true` |
| `manual_control` | Full travel on a 0% or 100% command, allowing for an actuator that may have been moved manually | `false` |
| `has_built_in_endstop` | Skips `stop_action` at the endpoints, as in `cover.time_based` | `false` |
| `start_position` | Startup behavior: `asis`, `closed` or `open` | `asis` |
| `end_positions_overrun` | Extra time at the endpoints, as a percentage of full travel time in the respective direction (0–100%) | `0%` |

Position ranges from 0% to 100%. The standard `valve.open`, `valve.close`,
`valve.stop`, `valve.toggle` and `valve.control` actions are supported, for example:

```yaml
- valve.control:
    id: valve_2
    position: 40%
```

When the target changes during movement, the component accounts for movement
already completed, continues or reverses direction, and stops the actuator at the target.
For example, moving from 20% to 50% with `open_duration: 120s` takes 36 s.
Position updates are published once per second while moving; movement starts and
stops are published immediately. Stopping accuracy is limited by the ESPHome loop frequency.

With `manual_control: true`, every full-open/full-close command assumes a starting
position at the opposite endpoint and runs for the full travel time.
Repeating such a command during the normal travel phase restarts the timer.
Repeating it during the additional overrun phase does not restart travel or extend
the overrun. Intermediate positions continue to work normally.
This option is not a GPIO input and does not measure manual valve movement.

With `has_built_in_endstop: true`, the direction action/relay remains active after
reaching 0% or 100%; the actuator's own limit switch is expected to stop the motor.
`stop_action` still runs at intermediate positions and on an explicit `valve.stop`.
Having built-in limit switches does not require this option to be `true`.
The examples leave it `false`, so the relays also turn off at the endpoints.
This option preserves behavior inherited from `cover.time_based`; it does not
require limit switches to be connected to the ESP. It has only been tested in software.

### Extra time at the endpoints

`end_positions_overrun: 5%` adds 5% of the full closing or opening duration
to normal travel towards 0% or 100%. For a 120 s duration, this adds 6 s.
For example, closing from 25% takes 30 s + 6 s, and opening from 50% takes
60 s + 6 s. Intermediate targets receive no additional time.

When an endpoint command is issued while idle and the position already reads
0% or 100% at that endpoint, only the additional time is applied.
`manual_control: true` still forces full travel plus overrun. Repeated commands
for the same endpoint during overrun do not extend its timer. A new target,
STOP or toggle can interrupt overrun; it does not automatically resume after stopping.

During overrun, the position is clamped to 0–100%, while `current_operation`
continues to indicate movement. The position is saved after movement finishes.
Reaching 0%/100% (including the `on_open`/`on_closed` trigger) does not mean
overrun is complete; `IDLE` indicates that movement has finished.

This option does not use endpoint sensors. With `has_built_in_endstop: true`,
the state changes to `IDLE` after the additional time, but `stop_action` is skipped
in keeping with that option's behavior. With `false`, `stop_action` is executed.

### Startup position

- `asis`: restores the saved position without movement; assumes 50% if no saved position exists.
- `closed`: runs `close_action` for the full `close_duration`, then executes `stop_action`
  and saves a position of 0%.
- `open`: runs `open_action` for the full `open_duration`, then executes `stop_action`
  and saves a position of 100%.

The startup positioning run ignores the saved position, `manual_control` and
`assumed_state`. It does not add `end_positions_overrun`. It always executes
`stop_action` at the end, even with `has_built_in_endstop: true`.
It starts after GPIO outputs/switches have been initialized (priority 599).

During this run, the reported position is a provisional estimate calculated
from the opposite endpoint. Use `id(valve_2).is_initializing()` to detect startup
positioning in progress. Normal position commands wait until the run finishes;
only the latest target is applied. A command for the startup endpoint clears
the pending target and does not trigger a second run. An explicit STOP or toggle
interrupts the run and clears the pending target. After an interruption,
the position remains only a provisional estimate.

Opening and closing actions should start movement immediately. A `delay` or
`interlock_wait_time` before energizing the actuator counts towards travel time.
Separate startup/direction-change delay handling has not yet been implemented.

## Setup and migration from cover

1. Load the component through `external_components`.
2. Move the definition from `cover:` to `valve:` and change the platform to `time_based_valve`.
3. Keep the durations and switch IDs. Change `device_class: damper` to `water`
   or remove `device_class`; `damper` is a `cover` device class.
4. Replace `cover.*` calls with `valve.*`. In lambdas, use the
   `valve::VALVE_OPERATION_*` constants. `id(valve_2).position` still uses the 0–1 range.
5. In Home Assistant, update references from `cover.*` entities to the newly discovered
   `valve.*` entities, including automations and dashboards. Keeping the same YAML `id`
   does not preserve the Home Assistant entity domain.

With the default `start_position: asis`, the component restores the saved estimate
without movement; if there is no saved position, it starts at an estimated 50%,
as `cover.time_based` does. It saves the position after stopping; updates while
moving do not save it every second. Persistence depends on the platform and ESPHome
preferences settings; on ESP8266, restoring after power loss requires an appropriate
`restore_from_flash` configuration. A power failure during movement may leave an older saved position.

Migration does not convert saved `cover` state to `valve` state. On first startup,
use `start_position: closed` or `open` to synchronize the estimate with the actuator's
position. Remove any existing `on_boot` sequence that directly controls the same
relays, since the component replaces it. The examples use `closed` and 5% overrun
during normal operation. `assumed_state: false` affects the UI; it does not provide
position feedback. The percentage represents estimated mechanical position,
not a percentage of flow or mixing ratio.

## Extending the component

The Python schema and configuration code generation are in `valve.py`; actuator
logic is in the `TimeBasedValve` class in the `.h`/`.cpp` files. The class is not `final`.
`control()` handles commands, `recompute_position_()` calculates position,
`start_direction_()` starts actions, and `loop()` checks the target and publishes state.
Protected methods and fields allow subclasses to extend it; `start_direction_()`
is virtual. The virtual `now_ms_()` allows timing tests without a physical actuator.

There is no dependency on `cover`. Future extensions could include a minimum
movement pulse, a direction-change delay or periodic valve movement.
These extensions are not part of the current version.

## Validation

From the repository root, in an environment with ESPHome installed:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
```

`tests/run.py` checks YAML validation, compiles a program for the `host` platform
and runs C++ tests against the actual ESPHome Valve/Automation classes.
A controlled clock checks movement from 20% to 50% in 36 s, different directional
durations, target changes, direction changes, STOP, toggle, full manual travel,
built-in limit switch behavior, estimate restoration, startup travel in both directions,
endpoint overrun, repeated/interrupted commands and `millis()` wraparound.
Test preferences are stored in a temporary directory.

See [VALIDATION.md](VALIDATION.md) for the scope of completed checks.
Testing with a physical actuator or entity discovery in a running Home Assistant
instance has not been performed.

## Provenance

Based on [ESPHome time_based/cover 2026.8.2](https://github.com/esphome/esphome/tree/2026.8.2/esphome/components/time_based/cover).
Upstream licenses are preserved: GPLv3 for C++, MIT for Python and other files.
See [LICENSE](../../LICENSE) and [NOTICE](../../NOTICE).
