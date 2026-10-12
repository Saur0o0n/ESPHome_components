# Weryfikacja — 2026-10-12

Środowisko: lokalny kontener `esphome-builder`, ESPHome **2026.8.2**.
Źródła do testów skopiowano do tymczasowego katalogu wewnątrz kontenera.
Nie zmieniano konfiguracji istniejących urządzeń ani nie wgrywano firmware.

| Sprawdzenie | Wynik |
|---|---|
| `esphome config examples/time_based_valve/esp8266.yaml` | PASS |
| `python tests/run.py`: walidacja konfiguracji | PASS — 10 metod testowych, w tym podprzypadki |
| `python tests/run.py`: kompilacja na host | PASS — platformio/native 1.2.1 |
| `python tests/run.py`: testy zachowania C++ | PASS |
| Akcje wygenerowane z YAML: control/stop/open/close/toggle | PASS |
| `esphome compile examples/time_based_valve/esp8266.yaml` | PASS — nodemcuv2, Arduino 3.1.2 |

Kompilacja ESP8266 obejmuje dwa zawory, cztery przełączniki GPIO z interlock,
Wi-Fi i natywne API. Oba zawory mają `start_position: closed` i
`end_positions_overrun: 5%`. Rozmiar programu zależy od konfiguracji i wersji ESPHome.

Testy C++ używają rzeczywistej implementacji `Valve`, `ValveCall`, `Trigger`,
`Automation` i preferences ESPHome. Zegar jest zastąpiony kontrolowanym zegarem
testowym, a wyjścia napędu licznikami/stanami w pamięci. Sprawdzono:

- pierwsze uruchomienie na 50%, odtworzenie zapisu bez ruchu i odrzucenie NaN;
- brak zapisywania każdej aktualizacji podczas ruchu;
- 20 → 50% w 36 s dla pełnego przebiegu 120 s;
- osobne czasy otwierania i zamykania;
- STOP także pomiędzy wywołaniami pętli i ponowiony STOP w spoczynku;
- zmianę celu w tym samym kierunku oraz zmianę kierunku;
- zatrzymanie przy celu równym aktualnej pozycji;
- sekwencję toggle, niezależność dwóch instancji i deklarowane możliwości encji;
- przepełnienie `millis()` i 120 000 kroków zegara bez kumulacji błędu pozycji;
- pełne przebiegi `manual_control`, ich ponawianie i zmianę kierunku;
- pomijanie STOP na krańcach przy `has_built_in_endstop`, STOP pośredni i jawny.
- `start_position: open/closed`: pełny przebieg pomimo zapisu wskazującego już
  ten sam kraniec, bez dodatkowego czasu i ze STOP także przy `has_built_in_endstop`;
- zapis pozycji po przebiegu startowym i odtworzenie przez `asis`;
- odroczenie ostatniego celu podczas startu oraz anulowanie przez STOP;
- zapas 5% w obu kierunkach, liczony od pełnego czasu danego kierunku;
- samo dobijanie przy ponownym poleceniu krańca w spoczynku;
- tysiące ponowień polecenia podczas dobijania bez przesuwania terminu STOP;
- przerwanie dobijania przez STOP/zmianę celu, brak zapasu dla celów pośrednich;
- `manual_control` wraz z zapasem i przepełnienie `millis()` podczas dobijania;
- zachowanie `has_built_in_endstop` po dodatkowym czasie w normalnej pracy.

Walidacja YAML sprawdza także brak wymaganych akcji/czasów, czas zerowy,
ujemny i przekraczający przyjęty limit oraz nieobsługiwaną opcję. Sprawdzono
trzy wartości `start_position`, odrzucenie innej wartości oraz zakres procentowy
`end_positions_overrun` (odrzucenie wartości ujemnej, ponad 100% i czasu `5s`).

Nie sprawdzono fizycznego napędu, rzeczywistego czasu przekaźników, trwałości
zapisu po odcięciu zasilania ESP ani widoku/sterowania w działającym Home Assistant.
Nie potwierdzono kompilacji na innych wersjach ESPHome ani na ESP32.
