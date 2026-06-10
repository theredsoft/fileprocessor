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

## Статус проверки на Windows

Проверено 2026-06-10 на Windows Server 2022 (MSVC 19.44, Qt 6.7.3
msvc2019_64, CMake 3.31, Ninja 1.12):

- [x] полная сборка Release (fp_core, тесты, GUI) — чисто, без warnings;
- [x] все 52 юнит-теста, включая executor-тесты с реальными
      переименованиями на NTFS;
- [x] `windeployqt` собирает работоспособный dist (~25 МБ);
- [x] приложение запускается (smoke на offscreen-платформе).

Требует интерактивной проверки (нужен рабочий стол):

- прогресс на кнопке таскбара (`ITaskbarList3`);
- tray-уведомления Windows 10/11;
- длинные пути (>260 символов) на реальных данных.

Примечание: `windeployqt` сам не копирует msvcp140/vcruntime140 —
build.ps1 докладывает их в dist из `VC\Redist\MSVC\...\VC143.CRT`,
после чего каталог dist полностью переносим: копируете на любой
64-битный Windows 10 (1809+)/11/Server 2016+ и запускаете
fileprocessor.exe без установки чего-либо.
