# ESPHome components — Sauron

External components for [ESPHome](https://esphome.io/).
This repository contains independent components that can be selected through
`external_components`. Maintained by **Sauron**.

## Available components

| Component | Platform | Description | Documentation |
|---|---|---|---|
| `mixing_valve_climate` | `climate` | Single-setpoint mixed-water regulation, fast closing, runtime temperature settings and heat-source availability | [Guide](docs/mixing_valve_climate/README.md) |
| `time_based_valve` | `valve` | Time-based valve positioning, startup positioning, pulsed movement and extra travel time at the endpoints | [Guide](docs/time_based_valve/README.md) |

## Installation

Add the following to your device configuration:

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve]
```

For automatic mixing-temperature control, select
`components: [time_based_valve, mixing_valve_climate]` and follow the
[climate guide](docs/mixing_valve_climate/README.md).

Then configure the selected component using its documentation.
The basic valve configuration is:

```yaml
valve:
  - platform: time_based_valve
    name: "Mixing valve"
    id: mixing_valve
    device_class: water
    open_duration: 120s
    close_duration: 120s
    start_position: closed
    end_positions_overrun: 5%
    open_action:
      - switch.turn_off: valve_close
      - switch.turn_on: valve_open
    close_action:
      - switch.turn_off: valve_open
      - switch.turn_on: valve_close
    stop_action:
      - switch.turn_off: valve_open
      - switch.turn_off: valve_close
```

`valve_open` and `valve_close` refer to your actuator's switch IDs.
A complete relay example is available in
[examples/time_based_valve/esp8266.yaml](examples/time_based_valve/esp8266.yaml).
The defaults are `start_position: asis` and `end_positions_overrun: 0%`.
The startup positioning run does not include `end_positions_overrun`.

For gradual movement, use the additional action:

```yaml
- time_based_valve.control:
    id: mixing_valve
    position: 30%
    movement: pulsed
```

The default opening cycle is 2 s on / 8 s off; the closing cycle is 2 s on /
4 s off. Each duration is configurable. Standard `valve.*` positioning commands
remain continuous. See the [pulsed movement guide](docs/time_based_valve/README.md#pulsed-movement)
for timing, cancellation and endpoint behavior.

`main` receives ongoing updates. To pin a specific version, replace `main`
with the full SHA of the desired commit. Select each component explicitly in `components:`.

## Repository structure

```text
components/
  time_based_valve/       # component Python and C++ source
docs/
  time_based_valve/       # guide and validation results
examples/
  time_based_valve/       # example configurations
tests/
  time_based_valve/       # tests and host program
  run.py                 # component test runner
LICENSE
NOTICE
```

Additional components use the same directory layout under their own names.
See [CONTRIBUTING.md](CONTRIBUTING.md) for instructions on adding a component.

## Validation

In a Python environment with ESPHome 2026.8.2 installed:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
esphome compile examples/time_based_valve/esp32.yaml
esphome compile examples/mixing_valve_climate/esp8266.yaml
esphome compile examples/mixing_valve_climate/esp32.yaml
```

To run a single suite, use `python tests/run.py time_based_valve`.
The examples load component sources from this local repository. The tests do not
operate hardware or flash firmware. See the valve's
[validation report](docs/time_based_valve/VALIDATION.md) for results and limitations.

## Licensing and provenance

`time_based_valve` is based on `cover.time_based` from ESPHome 2026.8.2.
It retains ESPHome's licensing split: **GPLv3 for C++**, **MIT for Python
and other files**. [LICENSE](LICENSE) contains the upstream license text;
[NOTICE](NOTICE) describes the source and modifications.

`mixing_valve_climate` is an original controller by Sauron using the ESPHome
climate, sensor and valve APIs, with the same GPLv3 C++ / MIT Python licensing
split. Its [validation report](docs/mixing_valve_climate/VALIDATION.md) lists
software checks separately from physical installation tests.
