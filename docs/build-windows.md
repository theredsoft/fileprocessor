# Сборка под Windows

## Требования

- Visual Studio 2022 (компонент «Desktop development with C++»)
- CMake ≥ 3.21 и Ninja (входят в установку VS)
- Qt 6.x **msvc2022_64** (через Qt Online Installer)
- Для инсталлятора: [Inno Setup 6](https://jrsoftware.org/isinfo.php), `iscc` в PATH

## Разработка

Из «x64 Native Tools Command Prompt for VS 2022»:

```bat
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2022_64
cmake --build build
ctest --test-dir build --output-on-failure
build\fileprocessor.exe
```

Без Qt собираются только ядро (`fp_core`) и тесты — этого достаточно
для работы над движком шаблонов, планировщиком и исполнителем.

## Релиз

```powershell
cd packaging\windows
.\build.ps1 -QtDir "C:\Qt\6.7.2\msvc2022_64"             # dist\ — портативная сборка
.\build.ps1 -QtDir "C:\Qt\6.7.2\msvc2022_64" -Installer  # + output\fileprocessor-*-setup.exe
```

`build.ps1` собирает Release, прогоняет тесты, складывает exe c Qt-DLL
в `dist\` через `windeployqt` и (с флагом `-Installer`) собирает
установщик Inno Setup.

## Что проверить на реальной Windows

Код разрабатывался кроссплатформенно; Windows-специфика покрыта
условной компиляцией и требует ручной проверки на месте:

- прогресс на кнопке таскбара (`ITaskbarList3`, src/ui/wintaskbar.cpp);
- длинные пути (`\\?\`-префикс в src/core/exec/renameexecutor.cpp);
- кириллические имена файлов (конвертация UTF-8 → UTF-16);
- поведение tray-уведомлений Windows 10/11.
