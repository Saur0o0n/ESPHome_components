# Dodawanie komponentów

Każdy komponent ma własną nazwę w formacie `snake_case` i niezależny katalog:

- `components/<nazwa>/` — źródła ESPHome, z `__init__.py` i modułem platformy;
- `docs/<nazwa>/README.md` — instalacja, opcje, zachowanie i ograniczenia;
- `examples/<nazwa>/` — przykładowe YAML-e bez rzeczywistych danych dostępowych;
- `tests/<nazwa>/run.py` — punkt wejścia do sprawdzeń danego komponentu.

1. Zarejestruj komponent w tabeli w głównym README.
2. W przykładach użyj lokalnego źródła `../../components` i jawnej listy
   `components: [<nazwa>]`. Ścieżka jest względna wobec głównego YAML-a.
3. Dodaj testy zachowania odpowiednie do funkcji komponentu i walidację opcji.
   Główny `tests/run.py` automatycznie wykrywa podkatalogi z własnym `run.py`.
4. Podaj sprawdzoną wersję ESPHome i rozróżnij kompilację, testy programowe
   oraz próbę na rzeczywistym sprzęcie.
5. Zachowaj informacje o pochodzeniu i licencji adaptowanego kodu.

Do repozytorium nie dodawaj `secrets.yaml`, prywatnych konfiguracji urządzeń,
plików firmware, cache ani katalogów kompilacji. Przykłady powinny zawierać
jednoznacznie fikcyjne dane lub odwołania do sekretów użytkownika.

Zmiany sprawdzaj z katalogu głównego repozytorium:

```sh
python tests/run.py
esphome compile examples/time_based_valve/esp8266.yaml
```

Dotychczasowy komponent zaworu jest testowany na ESPHome 2026.8.2.
