# ESPHome components — Sauron

External components for [ESPHome](https://esphome.io/).
This repository contains independent components that can be selected through
`external_components`. Maintained by **Sauron**.

## Available components

| Component | Platform | Description | Documentation |
|---|---|---|---|
| `time_based_valve` | `valve` | Time-based valve positioning, startup positioning and extra travel time at the endpoints | [Guide](docs/time_based_valve/README.md) |

## Installation

Add the following to your device configuration:

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve]
```

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
