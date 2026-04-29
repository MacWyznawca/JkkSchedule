# JkkSchedule

JkkSchedule to lekka biblioteka harmonogramów dla ESP32 i projektów ESP-IDF.

Jest spakowana jako komponent ESP-IDF wielokrotnego użytku i dostępna w ESP Component Registry.

Obsługuje:
- harmonogramy dni tygodnia
- harmonogramy datowe
- harmonogramy z interwałem względnym
- harmonogramy oparte na wschodzie/zachodzie słońca z pozycją geograficzną urządzenia
- trwałe przechowywanie w NVS
- jeden wspólny timer nadrzędny dla wydajnego działania w czasie rzeczywistym

## Status projektu

Biblioteka jest zorientowana produkcyjnie i wyodrębniona jako samodzielny komponent z większego prywatnego projektu.

### Inspiracja i uwaga o przepisaniu

JkkSchedule powstała na bazie oryginalnego komponentu schedule firmy Espressif.
Implementacja nie jest kopią 1:1.
Została gruntownie przebudowana i poprawiona — naprawiono m.in. obsługę kalendarza, zachowanie harmonogramów wschód/zachód słońca oraz przeprojektowano strategię zarządzania timerem.

## Licencja

Apache License 2.0. Zobacz [LICENSE](LICENSE).

## Wymagania

- ESP-IDF 5.5, 6.0 (testowany zakres wersji)
- FreeRTOS (dostarczany przez ESP-IDF)
- NVS (komponent `nvs_flash`)

## Instalacja (komponent ESP-IDF)

Z ESP Component Registry:

```yaml
dependencies:
    MacWyznawca/JkkSchedule: "^1.0.0"
```

Lub przez CLI:

```bash
idf.py add-dependency "MacWyznawca/JkkSchedule^1.0.0"
```

Opcje ręczne:

Skopiuj to repozytorium jako komponent:
- `components/JkkSchedule`

Lub dodaj jako submoduł git wewnątrz katalogu komponentów projektu.

W kodzie źródłowym projektu:

```c
#include "JkkSchedule.h"
```

## Szybki start

```c
jkk_schedules_handle_t *sched = jkk_schedule_init(
    NULL,
    "sched0",
    5212345,   // szerokość geograficzna * 100000
    2101234    // długość geograficzna * 100000
);

if (sched) {
    jkk_schedule_get_all(sched);
    jkk_schedule_run_all(sched, false);
}
```

## Podstawowy schemat użycia

1. Utwórz menedżer: `jkk_schedule_init(...)`
2. Załaduj harmonogramy z NVS: `jkk_schedule_get_all(...)`
3. Zarejestruj callbacki (`jkk_schedule_callback_all(...)` lub osobno dla każdego harmonogramu)
4. Utwórz lub edytuj harmonogramy
5. Uruchom timer: `jkk_schedule_run_all(...)`

## Uwagi do API

- Koordynaty przekazywane są jako `int32_t` w formacie E5 (wartość × 100 000).
- Tryby wschód/zachód słońca korzystają z koordynatów menedżera.
- Jeśli koordynaty są nieprawidłowe, harmonogramy słoneczne automatycznie przełączają się na działanie oparte na stałej godzinie.

## Harmonogramy słoneczne z granicą czasową

Typy `SUNRISE` i `SUNSET` obsługują opcjonalną **granicę godzinową**. Aktywuje się ją przez ustawienie poprawnych pól `hours` (0–23) i `minutes` (0–59) w konfiguracji wyzwalacza. Gdy oba pola mają wartości spoza zakresu (domyślne), harmonogram odpala się wyłącznie o czasie astronomicznym.

**Reguła wyboru:**

| Typ | Zachowanie z granicą | Efekt |
|---|---|---|
| `SUNRISE` | `min(wschód_słońca, hours:minutes)` | Nie później niż podana godzina |
| `SUNSET` | `max(zachód_słońca, hours:minutes)` | Nie wcześniej niż podana godzina |

**Przykład — zimowe wieczory:**  
Żeby oświetlenie nie przełączało się na tryb ciepłej bieli zbyt wcześnie zimą (kiedy zachód słońca może być ok. 15:30), ustaw typ `SUNSET` z granicą `18:00`. Harmonogram odpali się o 18:00 zimą i o czasie zachodu słońca latem (gdy zachód jest po 18:00):

```c
jkk_schedule_config_t cfg = {
    .name = "evening_warm_white",
    .trigger = {
        .type    = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET,
        .hours   = 18,   // nie wcześniej niż 18:00
        .minutes = 0,
        .day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY,
    },
    .trigger_cb = evening_cb,
};
```

**Analogicznie dla SUNRISE** — `hours:minutes` pełni rolę górnego limitu (np. "budzik nie później niż o 7:30, nawet jeśli wschód jest późniejszy zimą").

> Jeśli koordynaty geograficzne nie są ustawione lub są nieprawidłowe, biblioteka używa wyłącznie podanej godziny — `hours:minutes` staje się wtedy zwykłym wyzwalaczem godzinowym.

## Przykłady

Zobacz:
- [examples/esp-idf/basic](examples/esp-idf/basic)
- [examples/esp-idf/sun_events](examples/esp-idf/sun_events)
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

## Kompatybilność z Arduino

Biblioteka jest przeznaczona dla komponentów ESP-IDF.

Eksperymentalny przykład dla Arduino jest dostępny w:
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

Status:
- nietestowany
- stanowi jedynie punkt wyjścia

Może być używana z Arduino tylko przy spełnieniu następujących warunków:
- platforma docelowa to ESP32 (Arduino-ESP32)
- projekt jest budowany z integracją ESP-IDF (np. Arduino jako komponent ESP-IDF)
- `nvs_flash` jest zainicjalizowany przed ładowaniem harmonogramów
- standardowe funkcje czasu C są dostępne, a SNTP/strefa czasowa skonfigurowane dla poprawności harmonogramów słonecznych

## Struktura repozytorium

- `include/JkkSchedule.h` - publiczne API
- `src/JkkSchedule.c` - rdzeń planisty
- `src/JkkScheduleNvs.c` - warstwa trwałości NVS
- `src/JkkScheduleInternal.h` - wewnętrzne deklaracje
- `examples/` - przykładowe projekty integracyjne

## Podziękowania

- Espressif Systems za opublikowanie oryginalnej koncepcji komponentu schedule.
- Użytkownicy weryfikujący zachowanie na rzeczywistych urządzeniach.
