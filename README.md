# FileProcessor

Windows-утилита для массового переименования файлов в папке по шаблону
на простом псевдоязыке. До 10 000 файлов, многопоточная обработка,
прогресс и ETA в окне и в системном трее.

```
{folder}_{number = 0001}.{ext}                          → Vacation_0001.jpg
Photo_{photodate = YYYYMMDD OR filedate = YYYYMMDD}_{number = 001}.{ext}
{name AND uppercase}_{number = 01}.{ext}                → SCAN_01.pdf
```

## Документация

- [План реализации](docs/plan.md)
- [Спецификация псевдоязыка шаблонов](docs/template-language.md)

## Сборка

Ядро (`fp_core`) — чистый C++20 без зависимостей, UI — Qt 6 Widgets.
Без установленного Qt собираются только ядро и тесты.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Полная сборка под Windows: MSVC + Qt 6 (путь к Qt — через
`-DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64`).

## Статус

Готово: движок шаблонов, планировщик с проверкой коллизий и валидацией
Windows-имён, многопоточный исполнитель с журналом и undo, живой
двухпанельный предпросмотр, прогресс/ETA, tray-иконка с уведомлениями,
прогресс на таскбаре Windows, дата съёмки из EXIF (`{photodate}`).
Остался этап 8: полировка — история шаблонов, пресеты, установщик.
