# ESPHome components — Sauron

Zewnętrzne komponenty dla [ESPHome](https://esphome.io/).
Repozytorium zawiera niezależne komponenty, które można wybierać przez
`external_components`. Opiekun: **Sauron**.

## Dostępne komponenty

| Komponent | Platforma | Opis | Dokumentacja |
|---|---|---|---|
| `time_based_valve` | `valve` | Pozycjonowanie zaworu według czasu, ustalanie pozycji przy starcie i dodatkowy czas na końcach zakresu | [Instrukcja](docs/time_based_valve/README.md) |

## Instalacja

Dodaj do YAML-a urządzenia:

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve]
```

Następnie skonfiguruj wybrany komponent zgodnie z jego instrukcją.
Dla zaworu podstawowe opcje to:

```yaml
valve:
  - platform: time_based_valve
    name: "Zawór mieszający"
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

`valve_open` i `valve_close` to identyfikatory własnych przełączników napędu.
Kompletny przykład z przekaźnikami znajduje się w
[examples/time_based_valve/esp8266.yaml](examples/time_based_valve/esp8266.yaml).
Domyślne wartości to `start_position: asis` i `end_positions_overrun: 0%`.
Przebieg startowy nie dolicza zapasu z `end_positions_overrun`.

`main` otrzymuje kolejne zmiany. Aby zachować konkretną wersję, zastąp `main`
pełnym SHA wybranego commita. Każdy komponent wybiera się jawnie w `components:`.

## Struktura repozytorium

```text
components/
  time_based_valve/       # Python + C++ komponentu
docs/
  time_based_valve/       # instrukcja i wyniki weryfikacji
examples/
  time_based_valve/       # konfiguracje przykładowe
tests/
  time_based_valve/       # testy i program host
  run.py                 # uruchamianie testów komponentów
LICENSE
NOTICE
```

Kolejne komponenty otrzymują analogiczne podkatalogi pod własną nazwą.
[CONTRIBUTING.md](CONTRIBUTING.md) opisuje sposób dodania nowego komponentu.

## Weryfikacja

W środowisku Python z ESPHome 2026.8.2:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
```

Można uruchomić tylko jeden zestaw: `python tests/run.py time_based_valve`.
Przykłady korzystają lokalnie ze źródeł tego repozytorium. Testy nie sterują
sprzętem i nie wgrywają firmware. Wyniki i ograniczenia dla zaworu opisuje
[raport weryfikacji](docs/time_based_valve/VALIDATION.md).

## Licencje i pochodzenie

`time_based_valve` bazuje na `cover.time_based` z ESPHome 2026.8.2.
Zachowuje podział licencyjny ESPHome: **GPLv3 dla C++**, **MIT dla Pythona
i pozostałych plików**. Dokument [LICENSE](LICENSE) zawiera tekst źródłowej
licencji, a [NOTICE](NOTICE) opisuje pochodzenie i modyfikacje.
