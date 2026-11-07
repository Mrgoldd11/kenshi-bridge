# Kenshi Bridge

Плагин для [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi). Он связывает игру с
внешними программами, например с Cactus. В игре плагин открывает локальный сервер на
`127.0.0.1` и через него:

- отдаёт состояние: выбранный персонаж, координаты, фракция, текущий диалог, время в игре;
- выполняет команды: телепорт, камера в точку, скорость времени, пауза;
- пишет **трассировку диалога**: какой диалог игра выбрала на событие, какие соперники выпали и почему
  (не прошли условия или не дошла очередь по весу).

Данные игры плагин не меняет: его `.mod` пустой. Сервер слушает только `127.0.0.1`, поэтому
с другого компьютера к нему не подключиться.

Лицензия — GPLv3 (см. `LICENSE`), как у KenshiLib. Редактор Cactus (отдельная программа) с плагином общего кода не
имеет: они общаются только по сети через протокол ниже.

## Что нужно

- Kenshi 1.0.65 (Steam или GOG);
- RE_Kenshi 0.3.5 с KenshiLib 0.5.0.

## Установка и удаление

Готовая сборка лежит в Releases (вручную: папку `KenshiBridge` из архива в `<Kenshi>\mods\`). Скрипт ниже нужен, если вы собрали плагин сами (как собрать — дальше).

```
scripts\install.cmd "D:\SteamLibrary\steamapps\common\Kenshi"
```

Скрипт копирует три файла в `<Kenshi>\mods\KenshiBridge\`. Если такая папка уже была, её копия
сохраняется в `build\game-backup\KenshiBridge`. Потом включите мод **KenshiBridge** в лаунчере
игры (вкладка модов).

Удаление: выключить мод в лаунчере, затем

```
scripts\uninstall.cmd "D:\SteamLibrary\steamapps\common\Kenshi"
```

Работает ли плагин, видно в `<Kenshi>\RE_Kenshi_log.txt`. Там появится строка
`[KenshiBridge] 0.1.0 listening on 127.0.0.1:47801`.

## Сборка

KenshiLib и сама игра собраны компилятором Visual Studio 2010 (v100, x64). Плагин нужно собирать
тем же компилятором, иначе не совпадут библиотеки C++. Компилятор бесплатно входит в
Windows SDK 7.1. Его можно распаковать в папку проекта, ничего не устанавливая в систему:

1. Скачайте ISO Windows SDK 7.1 (`GRMSDKX_EN_DVD.iso`, x64) с сайта Microsoft. Распакуйте его в
   `_downloads\iso`.
2. Распакуйте три пакета административной установкой, она ничего не регистрирует в системе
   (PowerShell, из папки проекта):
   ```
   foreach ($m in 'vc_stdx86','vc_stdamd64','WinSDKBuild_amd64') {
     Start-Process msiexec -Wait -ArgumentList '/a', "_downloads\iso\Setup\$m\$m.msi", '/qn', "TARGETDIR=$PWD\toolchain"
   }
   ```
3. Клонируйте [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps)
   в `_downloads\deps` вместе с файлами LFS. Распакуйте там `boost_1_60_0`. Их `Setup.bat`
   запускать не нужно: пути задаёт `scripts\env.cmd`.
4. `scripts\build.cmd` → `build\KenshiBridge.dll`.

Проверка сетевой части без игры: `scripts\test.cmd`. Скрипт собирает `build\bridge-harness.exe`
из того же `server.cpp`/`json.cpp` с поддельным игровым циклом и гоняет тесты протокола (нужен Node.js).

### Почему часть функций ищется по адресу

KenshiLib 0.5.0 экспортирует не все функции, объявленные в её заголовках. Например, нет
`Character::teleport`, `CameraClass::teleport`, `GameWorld::setGameSpeed`/`userPause`,
`Dialogue::sendEvent`. Плагин находит их сам (`src/rva.cpp`):

- в заголовке KenshiLib указан адрес функции в версии GOG;
- таблицы `RE_Kenshi\RVAs\{GOG,Steam}_1.0.65.br` переводят его в адрес нужной версии;
- версию игры (Steam или GOG) плагин определяет по функции, которую KenshiLib экспортирует.

Если что-то не нашлось, команда вернёт ошибку `unsupported`, а игра продолжит работать.

## Протокол (версия 1)

TCP на `127.0.0.1:47801`. Порт можно сменить переменной окружения `KENSHI_BRIDGE_PORT` до
запуска игры. Каждое сообщение — один JSON-объект в одной строке (UTF-8, в конце `\n`).
Одновременно можно подключить до 8 клиентов. Строка не длиннее 1 МБ.

Запрос:

```json
{"id": 1, "cmd": "state", "args": {}}
```

`id` — любое число или строка, ответ вернёт его как есть. `args` можно не писать.

Ответ:

```json
{"id": 1, "ok": true, "result": {...}}
{"id": 1, "ok": false, "error": {"code": "not_in_game", "message": "..."}}
```

`ping` отвечает сразу, даже в главном меню. Остальные команды выполняются в игровом цикле: до 16
за кадр. Если игровой мир не работает больше 3 секунд (меню, загрузка), ответ — ошибка
`not_in_game`.

| Команда | Аргументы | Результат |
|---|---|---|
| `ping` | — | `{plugin, version, protocol, inGame}` |
| `state` | — | `{paused, speed, time:{days,hours}, selected, selectedDialogue, selectedIsPlayer, camera:{center}, playerCharacters, trace, build}` |
| `teleport` | `{pos:[x,y,z]}` | переносит выбранного персонажа; `{name, pos}` |
| `camera` | `{pos:[x,y,z]}` | камера перестаёт следить и встаёт в точку; `{pos}` |
| `speed` | `{value: 0..20}` | 0 — пауза, иначе скорость времени; `{value}` |
| `pause` | `{on: true\|false}` | `{paused}` |
| `trace` | `{on, filter:"player"\|"all"}` | включить или выключить трассировку диалога; `{on, all}` |
| `trace_get` | `{since: N}` | записи трассировки с номером больше N: `{next, items:[...]}` |

`selected` — `{name, template:{stringId,name}, faction:{name,stringId}, pos:[x,y,z]}` или `null`.
`selectedDialogue` — `{active, event, dialogue, line, target}` или `null`.

Коды ошибок: `bad_json`, `bad_request`, `unknown_cmd`, `bad_args`, `not_in_game`, `no_selection`,
`no_camera`, `unsupported`, `busy`, `crash` (исключение внутри команды поймано, игра работает).

### Трассировка диалога

По умолчанию она выключена. Её включает `{"cmd":"trace","args":{"on":true}}`. Фильтр `player`
(по умолчанию) записывает выбор диалога на клик игрока (событие 1) и случаи, где говорящий или
собеседник — персонаж игрока. Фильтр `all` записывает все выборы (их много).

Как это устроено. Игра на событие вызывает `Dialogue::_chooseDialog`. Та идёт по списку
кандидатов, отсортированному по весу, и для каждого проверяет условия
(`DialogLineData::checkConditions`). Первый, кто прошёл, побеждает. Плагин перехватывает обе
функции и записывает, что игра проверила. Сам он условия не вызывает: у проверки есть побочные
эффекты (броски шанса, «уникальный владелец»).

Запись (последние 300 хранятся в памяти и выдаются через `trace_get`):

```json
{"seq": 12, "event": 1, "wordswap": false,
 "speaker": {"name": "...", "templateId": "...", "player": false},
 "target":  {"name": "...", "templateId": "...", "player": true},
 "chosen":  {"dialogue": "<id>", "dialogueName": "...", "line": "<id>"},
 "candidates": [
   {"id": "...", "name": "...", "score": 10, "scoreForTarget": 10, "interjection": false,
    "chanceTemporary": 100, "chancePermanent": 100, "conditions": "fail", "chosen": false},
   {"id": "...", "conditions": "pass", "chosen": true},
   {"id": "...", "conditions": "not_reached", "chosen": false}
 ],
 "checks": [{"id": "...", "depth": 1, "pass": false}]}
```

`conditions` у кандидата:

- `pass` — условия прошли;
- `fail` — отсеян условиями;
- `not_reached` — игра до него не дошла: раньше выбрала кандидата с бо́льшим весом.

Каждая запись дублируется строкой в `RE_Kenshi_log.txt`:
`[KenshiBridge trace] ev=1 speaker=... chosen=<id> | <id>(вес,статус[,CHOSEN]) ...`.

## Пример клиента

```
node -e "const s=require('net').connect(47801,'127.0.0.1',()=>s.write(JSON.stringify({id:1,cmd:'state'})+'\n'));s.on('data',d=>{console.log(String(d));s.end()})"
```
