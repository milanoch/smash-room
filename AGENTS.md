# SMASH LAB: a game on the Panda engine

A PandaEditor project. The entire project state is text files: worlds, materials and settings
are JSON, gameplay is C++. Any editor and any coding agent can read and edit them directly; the
running editor picks up the changes on one explicit command, Reload.

## Постоянные правила для всех ИИ-ассистентов

Эти правила действуют для всех следующих заданий SMASH LAB, пока пользователь не изменит их.
Применяй их вместе с техническими инструкциями ниже. Общий контекст проекта —
`PROJECT_CONTEXT.md`; он описывает состояние проекта и не заменяет эти правила.

1. **Экономь токены и время.** Отвечай кратко, не повторяй известное и не описывай очевидные действия.
2. **Выполняй только поставленную задачу.** Не добавляй незапрошенные функции, улучшения,
   рефакторинг и следующие этапы. После завершения остановись.
3. **Вноси минимум изменений.** Используй существующие код и API PandaSDK/Box3D;
   не переписывай работающие системы без необходимости.
4. **Не создавай собственные замены систем движка без разрешения пользователя:** физику,
   рендеринг и другие уже существующие системы. Не скрывай обходные решения и существенные
   архитектурные изменения.
5. **Не проводи избыточные тесты.** Не создавай сотни случаев, отдельные тестовые программы,
   тестовые фреймворки и большие отчёты без необходимости. При изменении C++ выполни обычную
   сборку; дополнительные проверки нужны только при реальном риске ошибки.
6. **Не исследуй весь проект заново.** Читай только необходимые для задачи файлы;
   используй уже полученные знания и актуальный контекст.
7. **Не спрашивай лишнего.** Очевидные технические решения принимай самостоятельно.
   Уточняй только то, что нельзя безопасно определить.
8. **Сохраняй качество и чужие изменения.** Экономия не оправдывает нерабочий код,
   потерю данных или нарушение существующих функций.
9. **Отделяй сборку от работы в игре.** Успешная компиляция не подтверждает поведение,
   физику или внешний вид в PandaEditor. Явно сообщай, что проверено и что ещё не проверено.

### Короткий отчёт после каждой задачи

Пользователь не разбирается в программировании. Пиши простым русским языком, объясняя
сложные термины, если они необходимы. В обычной ситуации — **3–5 коротких предложений,
примерно 5–8 коротких строк**:

- Что сделано: какие объекты или функции добавлены, изменены или удалены.
- Как реализовано: использована существующая система PandaSDK/Box3D или собственный
  игровой код; важные обходные решения не скрывай.
- Какие файлы изменены и каков результат сборки; если сборка не требовалась, так и напиши.
- Что подтверждено проверкой и что человеку нужно проверить в PandaEditor.
- Ошибки, ограничения и незавершённые части, если они есть.

Длинный технический отчёт нужен только при серьёзной проблеме или по отдельному запросу.
Для изменений только документации сборка и запуск/Reload редактора не требуются.

## Structure

- `Assets/` — worlds (`.world`, JSON), materials (`.mat`, JSON), textures, meshes (glTF). Next
  to each asset file the editor keeps a `<file>.pmeta` sidecar (asset id and import settings):
  together they are the asset registry.
- `Scripts/SMASH LAB/src/` — the game's C++ scripts, built into a library the editor
  loads.
- `Scripts/SMASH LAB/scripts/` — shell scripts for the builds (see "Building scripts").
- `.Panda/` — service data: `project.json` (the project settings), `scripts-manifest.json`,
  cooked data. Do not edit by hand.
- `Export/` — the build setup of the standalone game.
- `.zed/tasks.json` — tasks for the Zed editor (configure, build, open the export projects,
  format). `.clangd` points at the debug build's compile database.

## Working rules

- **Only a human runs and debugs the game.** Do not try to launch the editor or the game; ask
  the user to check and give feedback.
- **At the end of your work ask the user to press Reload (Cmd/Ctrl+R)** — the editor
  synchronizes with the disk in a single command: it rereads changed assets, the fresh script
  bundle and the world (by mtime). Broken world JSON is safe — the editor reports the error and
  keeps the current world. A conflict (newer on disk plus unsaved edits in the editor) is
  resolved by the user per item.
- Build the C++ scripts yourself (see "Building scripts") — Reload picks up the fresh library.
- New asset files are registered by the editor; do not edit `.pmeta` sidecars by hand.
- Detailed formats: `docs/` in the PandaSDK package (`world-format.md`, `assets.md`,
  `scripting.md`, `zed-and-agents.md`).

## Editing a world (`.world`)

A world is one JSON file under `Assets/`. Minimal entity:

```json
{
    "id": 2847561930,
    "tagComponent": { "tag": "MyEntity" },
    "relationshipComponent": { "parent": 0, "children": [] },
    "transformComponent": {
        "position": {"x":0,"y":0,"z":0},
        "rotation": {"x":0,"y":0,"z":0,"w":1},
        "scale": {"x":1,"y":1,"z":1}
    },
    "scriptListComponent": { "scripts": [] }
}
```

- `id` — a random uint32, unique within the file, not 0. Generate it and check with grep.
- `rotation` — a quaternion (identity = 0,0,0,1), not Euler angles.
- The hierarchy is two-sided: the parent's `children` AND the child's `parent`. `parent: 0` is a
  root. The positions of children are in the parent's local space.
- Optional components (camera, light, meshes, physics...) are just keys next to the others; the
  full list and the exact fields are in `world-format.md`. For an unfamiliar component, build it
  once in the editor, save the world and use the JSON as a sample.
- References to assets (mesh, texture, world) are ids from the assets' `.pmeta` sidecars
  (`<file>.pmeta` next to the asset file, key `id`).

## Script fields in a world

A script is attached to an entity in `scriptListComponent`; field values are set in the same
place:

```json
"scriptListComponent": {
    "scripts": [
        {
            "name": "MoveScript",
            "scriptFields": [
                { "name": "speed", "fieldId": 123456789, "type": 1, "value": 3.5 }
            ]
        }
    ]
}
```

- `.Panda/scripts-manifest.json` is the source of truth: the editor writes it on every script
  load with the class names, the fields, their `fieldId` and types. **Always take `fieldId` from
  there**, do not compute it. If the file is missing, ask the user to build/reload the scripts.
- `name` — the name of the C++ class (`REGISTER_SCRIPT`).
- `type` as a number: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world (the
  manifest has the same types as words).
- `value`: integer/float — a number; entity — the id of an entity of THIS world; texture/
  material/world — the asset id from the asset's `.pmeta` sidecar.
- List EVERY field of the script: a field that is not listed is written as zero when Play
  starts — the initializer in the C++ class is not used.
- A script without parameters: `{ "name": "MyScript", "scriptFields": [] }`.

## Assets and materials

- Put the file into `Assets/` — the editor registers new files when the project is opened or
  rescanned and assigns ids in the `.pmeta` sidecars. If the id of a new file is needed right
  away and the editor has not rescanned yet, ask the user.
- Textures: `.png/.jpg`. sRGB (color) by default; DATA maps (normal, roughness, noise) need
  sRGB switched off — ask the user to toggle it in the texture inspector.
- Meshes: `.gltf` (+ `.bin` and textures next to it). For every glTF material the editor creates
  `<name>.mat` next to the mesh; meshes of one folder share materials by name.
- A material (`.mat`) is JSON `{"inputs": [{"name","type","value"}]}`; `type`: 0 float, 1 uint,
  2 vec4, 3 texture (value — asset id, 0 = empty), 4 color (value — {x,y,z,w}). Edit the values
  freely; the SET and the ORDER of the fields are the shader's contract, do not change them.
  Create a new material in the editor (choosing a shader fills in the field template). The
  material's shader is stored in the `.pmeta` sidecar, not in the `.mat`.

## Building scripts

1. Edit the code (the script skeleton and fields — `scripting.md`: the `Script` class,
   `PANDA_FIELDS_*`, `REGISTER_SCRIPT`).
2. Build from a terminal:
   `Scripts/SMASH LAB/scripts/configure_scripts.sh debug` (once),
   then `Scripts/SMASH LAB/scripts/build_scripts.sh debug`.
   The scripts need `PANDA_SDK_DIR` set to `<PandaSDK>/lib/cmake/PandaSDK`. The same commands
   are the Zed tasks "Configure SMASH LAB Debug" and "Build SMASH LAB Debug".
   Fix compilation errors yourself, using the output.
3. Ask the user to press Reload (Cmd/Ctrl+R) — the editor picks up the fresh library (by mtime)
   and updates `.Panda/scripts-manifest.json`.
