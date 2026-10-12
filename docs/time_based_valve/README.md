# ESPHome — Time Based Valve

Zewnętrzny komponent `valve` do zaworów z napędem otwórz/zamknij/stop,
których pozycję szacuje się z czasu ruchu. Bazuje na `cover.time_based`
z ESPHome 2026.8.2, ale dziedziczy bezpośrednio po `valve::Valve`.
Nie tworzy encji `cover` ani nie zastępuje wbudowanego komponentu `time_based`.

## Instalacja z GitHuba

```yaml
external_components:
  - source: github://Saur0o0n/ESPHome_components@main
    components: [time_based_valve]
```

Wybierz to źródło zamiast źródła lokalnego z przykładu poniżej.
Możesz przypiąć konkretny commit zamiast `main`.

## Instalacja lokalna

Skopiuj katalog `components/time_based_valve` obok konfiguracji urządzenia:

```text
config/
├── urzadzenie.yaml
└── components/
    └── time_based_valve/
        ├── __init__.py
        ├── valve.py
        ├── time_based_valve.h
        └── time_based_valve.cpp
```

W `urzadzenie.yaml` dodaj:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [time_based_valve]

valve:
  - platform: time_based_valve
    name: "Zawor 2"
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

Ten fragment korzysta z istniejących przełączników `valve_2_on` i `valve_2_off`.
Przy zmianie kierunku najpierw wyłącza poprzedni przekaźnik. Komponent wywołuje
zdefiniowane akcje, nie zarządza sam pinami ani blokadą przekaźników.
W definicjach przełączników zachowaj wzajemny `interlock` i stan startowy
`restore_mode: ALWAYS_OFF`. Czasy przełączenia przekaźników zależą od napędu.

Przykład obu zaworów: [examples/mixing-valves.yaml](../../examples/time_based_valve/mixing-valves.yaml).
Kompletna konfiguracja do sprawdzenia kompilacji:
[examples/esp8266.yaml](../../examples/time_based_valve/esp8266.yaml). Jej GPIO i Wi-Fi są przykładowe.
W przykładach ścieżka źródła to `../../components`, bo główny YAML leży w `examples/time_based_valve/`.
Ścieżkę źródła lokalnego licz względem głównego YAML-a urządzenia, także gdy
używasz `packages`.

## Funkcje i ustawienia

| Ustawienie | Znaczenie | Domyślnie |
|---|---|---|
| `open_duration` | Czas pełnego przejścia 0 → 100% | wymagane |
| `close_duration` | Czas pełnego przejścia 100 → 0% | wymagane |
| `open_action` | Uruchomienie otwierania | wymagane |
| `close_action` | Uruchomienie zamykania | wymagane |
| `stop_action` | Zatrzymanie ruchu | wymagane |
| `assumed_state` | Informacja dla interfejsu o szacowanym stanie | `true` |
| `manual_control` | Pełny przebieg przy poleceniu 0% lub 100%, także po ręcznym przestawieniu napędu | `false` |
| `has_built_in_endstop` | Pomijanie `stop_action` na końcach zakresu, jak w `cover.time_based` | `false` |
| `start_position` | Zachowanie na starcie: `asis`, `closed` lub `open` | `asis` |
| `end_positions_overrun` | Dodatkowy czas na końcach, jako procent pełnego czasu danego kierunku (0–100%) | `0%` |

Pozycja ma zakres 0–100%. Obsługiwane są standardowe akcje `valve.open`,
`valve.close`, `valve.stop`, `valve.toggle` i `valve.control`, np.:

```yaml
- valve.control:
    id: valve_2
    position: 40%
```

Po zmianie celu w czasie ruchu komponent uwzględnia dotychczasowy ruch,
kontynuuje lub zmienia kierunek i zatrzymuje napęd po osiągnięciu celu.
Przykład: od 20% do 50%, przy `open_duration: 120s`, otwiera przez 36 s.
Pozycję publikuje co sekundę podczas ruchu, a rozpoczęcie ruchu i zatrzymanie
publikuje od razu. Dokładność zatrzymania ogranicza częstotliwość pętli ESPHome.

`manual_control: true` powoduje, że każde polecenie pełnego otwarcia/zamknięcia
przyjmuje pozycję początkową na przeciwnym końcu zakresu i odmierza pełny czas.
Ponowienie takiego polecenia podczas podstawowego przebiegu rozpoczyna
odliczanie od nowa. Ponawianie podczas samego dodatkowego dobijania nie
restartuje przebiegu ani nie przedłuża dobijania.
Pozycje pośrednie nadal działają normalnie. Nie jest to wejście GPIO ani pomiar
ręcznego ruchu zaworu.

`has_built_in_endstop: true` pozostawia akcję/przekaźnik kierunku aktywny po
osiągnięciu 0% lub 100%; silnik ma zatrzymać własna krańcówka napędu.
Przy pozycji pośredniej oraz jawnym `valve.stop` działa `stop_action`.
Sama obecność krańcówek w napędzie nie wymaga ustawienia tej opcji na `true`.
Przykłady pozostawiają `false`, więc przekaźniki wyłączają się także na końcach.
Opcja zachowuje działanie odziedziczone z `cover.time_based`; nie wymaga
podłączenia krańcówek do ESP. Sprawdzono ją tylko w testach programowych.

### Dodatkowy czas na końcach

`end_positions_overrun: 5%` dodaje do normalnego dojazdu do 0% lub 100%
5% pełnego czasu zamykania albo otwierania. Przy 120 s to dodatkowe 6 s.
Przykład: od 25% do zamknięcia jest to 30 s + 6 s, a od 50% do otwarcia
60 s + 6 s. Pozycje pośrednie nie otrzymują dodatkowego czasu.

Przy poleceniu tego samego krańca w spoczynku, gdy pozycja już wynosi 0%
lub 100%, wykonywany jest sam dodatkowy czas. `manual_control: true` nadal
wymusza pełny przebieg plus zapas. Powtarzane polecenia tego samego krańca
podczas dobijania nie przedłużają odliczania. Nowy cel, STOP lub toggle
mogą przerwać dobijanie; nie ma automatycznego ponawiania po zatrzymaniu.

Podczas dobijania pozycja jest ograniczona do 0–100%, a `current_operation`
nadal wskazuje ruch. Zapis pozycji następuje po zakończeniu. Samo pojawienie
się 0%/100% (także trigger `on_open`/`on_closed`) nie oznacza końca dobijania;
zakończenie ruchu wskazuje `IDLE`.

Ta opcja nie korzysta z czujników krańcowych. Przy
`has_built_in_endstop: true` po upływie dodatkowego czasu stan przechodzi
na `IDLE`, ale `stop_action` jest pomijane zgodnie z zachowaniem tej opcji.
Przy `false` wykonywane jest `stop_action`.

### Pozycja startowa

- `asis`: odtworzenie zapamiętanej pozycji bez ruchu; bez zapisu przyjmowane 50%.
- `closed`: `close_action` przez pełne `close_duration`, następnie `stop_action`
  i zapis pozycji 0%.
- `open`: `open_action` przez pełne `open_duration`, następnie `stop_action`
  i zapis pozycji 100%.

Przebieg startowy ignoruje zapamiętane położenie, `manual_control` i
`assumed_state`. Nie dolicza `end_positions_overrun`. Na końcu zawsze
wykonuje `stop_action`, również przy `has_built_in_endstop: true`.
Rozpoczyna się po inicjalizacji wyjść/przełączników GPIO (priorytet 599).

Podczas tego przebiegu wskazywana pozycja jest prowizorycznym oszacowaniem,
liczonym od przeciwnego końca zakresu. `id(valve_2).is_initializing()` pozwala
rozpoznać trwające ustalanie pozycji. Zwykłe polecenia pozycji czekają na koniec
przebiegu; wykonywany jest ostatni cel. Polecenie krańca, do którego trwa
przebieg startowy, usuwa oczekujący cel i nie powoduje drugiego przejazdu.
Jawny STOP lub toggle przerywa przebieg i usuwa oczekujący cel. Po takim
przerwaniu pozycja nadal jest tylko prowizorycznym oszacowaniem.

Akcje otwierania i zamykania powinny uruchamiać ruch od razu. `delay` lub
`interlock_wait_time` przed załączeniem napędu będą wliczane do czasu ruchu.
Obsługa osobnego opóźnienia startu/zmiany kierunku nie jest jeszcze zaimplementowana.

## Uruchomienie i migracja z cover

1. Wczytaj komponent przez `external_components`.
2. Przenieś definicję z `cover:` do `valve:` i zmień platformę na `time_based_valve`.
3. Zachowaj czasy i identyfikatory przełączników. Zmień `device_class: damper`
   na `water` albo usuń `device_class`; `damper` jest klasą `cover`.
4. Zmień wywołania `cover.*` na `valve.*`. W lambdach używaj operacji
   `valve::VALVE_OPERATION_*`. `id(valve_2).position` nadal oznacza zakres 0–1.
5. W Home Assistant zaktualizuj odwołania z encji `cover.*` na nowo odkryte
   `valve.*`, w tym automatyzacje i dashboardy. Samo zachowanie YAML-owego `id`
   nie zachowuje domeny encji HA.

Przy domyślnym `start_position: asis` komponent odtwarza zapisane oszacowanie
bez ruchu; bez zapisu zaczyna od szacowanych 50%, podobnie jak `cover.time_based`.
Zapamiętuje pozycję po zatrzymaniu; aktualizacje podczas ruchu nie zapisują jej
co sekundę. Trwałość zapisu zależy od platformy i ustawień preferences ESPHome;
na ESP8266 trwałe odtwarzanie po utracie zasilania wymaga odpowiedniej konfiguracji
`restore_from_flash`. Zanik zasilania podczas ruchu może pozostawić starszy zapis.

Migracja nie obejmuje konwersji zapisu `cover` na `valve`. Przy pierwszym
uruchomieniu można zsynchronizować oszacowanie z położeniem napędu przez
`start_position: closed` lub `open`. Usuń wtedy dotychczasową sekwencję
`on_boot`, która bezpośrednio sterowała tymi samymi przekaźnikami — zastępuje
ją komponent. Przykłady używają `closed` i dodatkowego czasu 5% w zwykłej pracy.
`assumed_state: false` wpływa na interfejs, nie daje sprzężenia zwrotnego.
Procent oznacza oszacowane położenie mechaniczne, nie procent przepływu/mieszania.

## Rozbudowa

Pythonowy schemat i generowanie konfiguracji są w `valve.py`; logika napędu
w klasie `TimeBasedValve` w plikach `.h`/`.cpp`. Klasa nie jest `final`.
`control()` obsługuje komendy, `recompute_position_()` wylicza pozycję,
`start_direction_()` uruchamia akcje, a `loop()` sprawdza cel i publikuje stan.
Chronione metody i pola umożliwiają rozwijanie tej klasy; `start_direction_()`
jest wirtualne. Wirtualne `now_ms_()` pozwala testować czas bez rzeczywistego napędu.

Nie ma zależności od `cover`. Można tu później dodać np. minimalny impuls
ruchu, opóźnienie zmiany kierunku lub okresowy ruch zaworu.
Te rozszerzenia nie są częścią obecnej wersji.

## Weryfikacja

W środowisku z zainstalowanym ESPHome, z katalogu projektu:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
```

`tests/run.py` sprawdza walidację YAML, kompiluje program dla platformy `host`
i uruchamia testy C++ przeciwko rzeczywistym klasom ESPHome Valve/Automation.
Kontrolowany zegar sprawdza m.in. przejście 20 → 50% w 36 s, różne czasy kierunków,
zmianę celu, zmianę kierunku, STOP, toggle, pełny przebieg ręczny, krańcówki,
odtwarzanie oszacowania, oba kierunki przebiegu startowego, dobijanie na krańcach,
powtarzane/przerywane polecenia oraz przepełnienie `millis()`.
Stan preferences testów trafia do tymczasowego katalogu.

Zakres przeprowadzonych sprawdzeń opisuje [VALIDATION.md](VALIDATION.md).
Nie przeprowadzono próby z fizycznym napędem ani odbioru encji w działającym HA.

## Pochodzenie

Punkt wyjścia: [ESPHome time_based/cover 2026.8.2](https://github.com/esphome/esphome/tree/2026.8.2/esphome/components/time_based/cover).
Zachowane licencje źródłowe: GPLv3 dla C++, MIT dla Pythona i pozostałych plików.
Szczegóły: [LICENSE](../../LICENSE) i [NOTICE](../../NOTICE).
