# Инструкция по сборке и установке мода AI Style Decorator (.geode)

Этот проект содержит полный исходный код мода **AI Style Decorator** для Geometry Dash 2.2074 под загрузчик [Geode SDK](https://docs.geode-sdk.org/).

## Как работает мод в игре

1. Откройте **Редактор уровней (Level Editor)** в Geometry Dash.

2. Выделите область (несколько блоков вашей структуры или рамку зоны), которую нужно задекорировать. Если ничего не выделено, мод автоматически возьмет центральную зону экрана (`6x4` блока).

3. Нажмите **Правый Alt (`Right Alt`)** — откроется окно **AI Style Decorator**.

4. Выберите один из 4 стилей: **Neon**, **Modern**, **Hell** или **Reality**.

5. Укажите примерное количество объектов (от `10` до `500`, по умолчанию `80`).

6. В поле **AI Chat Instructions** напишите пожелания на русском или английском (например: `добавь больше пил, сделай фиолетовое свечение и темный фон` или `cyan & pink glow, add saws`).

7. Нажмите **Generate Decor** — выбранная зона мгновенно заполнится декором, а все созданные объекты автоматически выделятся и получат группу `999` (их можно сразу подвинуть или удалить).

## Способ 1: Сборка на ПК через Geode CLI (за 1 минуту)

Если у вас установлены [Geode CLI](https://www.google.com/search?q=https://docs.geode-sdk.org/getting-started/cli/) и Visual Studio (с компонентом C++) / CMake:

1. Создайте пустую папку `AIStyleDecorator`, положите туда файлы `mod.json`, `CMakeLists.txt` и создайте подпапку `src`, куда поместите `main.cpp` (либо оставьте `main.cpp` прямо в корне папки — `CMakeLists.txt` поддерживает оба варианта).

2. Откройте терминал (PowerShell или командную строку) в этой папке и выполните:

```
geode build

```

3. После завершения сборки готовый пакет `dev.aidecorator.geode` автоматически скопируется в папку `geode/mods` вашей Geometry Dash (а также появится в папке `build/`).

## Способ 2: Облачная сборка без установки компилятора (через GitHub Actions)

Если вы не хотите устанавливать Visual Studio и C++ компилятор на свой ПК:

1. Создайте репозиторий на GitHub и загрузите туда `mod.json`, `CMakeLists.txt` и `main.cpp`.

2. Создайте файл `.github/workflows/build.yml` со стандартным экшеном [geode-sdk/build-geode-mod](https://github.com/geode-sdk/build-geode-mod):

```
name: Build Geode Mod
on: [push, workflow_dispatch]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: geode-sdk/build-geode-mod@main
        with:
          combine: true
      - uses: actions/upload-artifact@v4
        with:
          name: AIStyleDecorator-geode
          path: ${{ steps.build.outputs.build-output }}

```

3. Перейдите во вкладку **Actions** в вашем репозитории и скачайте готовый `.geode` файл из раздела **Artifacts**, после чего перетащите его в папку `Geometry Dash/geode/mods/`.

## Настройка облачного ИИ (опционально)

По умолчанию в мод встроен **автономный процедурный ИИ-движок**, который работает сразу после установки без интернета и ключей, распознавая цвета и команды из чата (`свечение`/`glow`, `пилы`/`saws`, `шипы`/`spikes`, `цепи`/`chains`, `темный`/`dark` и оттенки цветов).

Если вы хотите подключить внешнюю нейросеть (OpenAI, Gemini через совместимый эндпоинт или локальную Ollama), откройте настройки мода в меню Geode внутри игры и укажите:

* **AI API Endpoint URL**: например, `https://api.openai.com/v1/chat/completions` или `http://localhost:11434/v1/chat/completions`

* **AI API Key**: ваш API-ключ

* **AI Model Name**: например, `gpt-4o-mini`