# THUG2 Story Co-op

**[English](#english)** | **[Русский](#русский)**

💸 [Support the author / Отблагодарить автора](https://www.donationalerts.com/r/leftgamerr)

<details>
<summary>Screenshots / Скриншоты</summary>

**Lobby: the host opened co-op, the friend connected / Лобби: хост открыл кооператив, друг подключился**
![Lobby](docs/01_lobby.png)

**Both in the same level / Оба в одном уровне**
![Together](docs/02_together.png)

**The host's jump on the friend's screen / Прыжок хоста виден у друга**
![Jump](docs/03_jump.png)

**Chat / Чат**
![Chat](docs/05_chat.png)

**The friend took a goal from an NPC, it started for both / Друг взял задание у NPC, оно запустилось у обоих**
![Goal taken](docs/06_goal_taken.png)

**Both play the goal / Оба проходят задание**
![Both run](docs/07_goal_both_run.png)

**The friend finished and waits for the host / Друг выполнил и ждёт хоста**
![Friend waits](docs/08_goal_friend_waits.png)

**The host finishing the goal / Хост доделывает задание**
![Host finishing](docs/09_goal_host_finishing.png)

**The goal is won for both / Задание засчитано обоим**
![Won for both](docs/10_goal_won_both.png)

</details>

---

## English

Online co-op for two in the **Tony Hawk's Underground 2** (PC) story, on top of **THEMOD 1.3**.
The host plays their own Story Mode; the friend joins the host's game and skates alongside in the same level:
NPCs, cutscenes, goals.

### Features

- The friend gets the host's story (chapters, won goals, world flags) on joining and on every level change.
- Cutscenes and goal cameras play for both; a skip (Esc / num2) by either player skips them for both.
- Shared goals: a goal one player takes starts for both and is won once both have done it.
- Your friend's name and distance over their skater (visible through walls), teleport to your friend,
  chat (a Cyrillic layout types the Latin letters on the same keys).
- Guest characters and the pro teammate are synced.
- Optional skater collision (`player_collision=1`).
- XInput gamepads (Xbox; PlayStation through Steam Input or DS4Windows).
- Borderless window by default (Alt+Tab works, the game keeps running behind other windows), any resolution up
  to 4K, a wider (not stretched) view on wide screens, the main menu kept 4:3.
- Leave co-op with a key (F9 twice); the friend's save in co-op keeps their own story.

### Install

You need Tony Hawk's Underground 2 for PC. Both players install the mod. THEMOD 1.3 doesn't have to be installed:
the archive has everything for it.

> **About THEMOD 1.3.** The co-op is built on THEMOD 1.3, and the patcher reproduces its installer. I couldn't find
> THEMOD's author or anywhere to download it (its site thmods.com is gone), so we made our own patcher: it turns
> your own `THUG2.exe` into `THEMOD 1.3.exe` (the same 126 bytes THEMOD's installer changes), and THEMOD's
> `thethug2mod.prx` comes in the archive. If you know the author, a download link or other resources, please
> [open an issue](https://github.com/LeftGamer1/thug2-coop/issues).

1. Download `thug2-coop.zip` from [Releases](https://github.com/LeftGamer1/thug2-coop/releases).
2. If you have THEMOD already, keep a copy of your `Data\pre\thethug2mod.prx`.
3. Unpack the archive into the game folder (the one with `THUG2.exe`), replacing files.
4. No `THEMOD 1.3.exe` there? Run `patch_themod.bat`: it makes it from `THUG2.exe` (which stays as it is).
   Start the game with `THEMOD 1.3.exe`.
5. In `coop.ini` set `name=` (both) and `host_address=` (the friend only).
6. The host's UDP 5153 and TCP 5154 must be reachable (Radmin VPN / ZeroTier / Porthole).

Details: [README_EN.txt](README_EN.txt) (settings, controls, limits, troubleshooting).

### Controls

| Key | Action |
|---|---|
| F10 | host: open co-op |
| F11 | friend: join |
| F12 | teleport to your friend |
| F9 twice | leave co-op |
| Enter | chat (Enter on an empty line or Esc closes it) |

The keys can be changed in `coop.ini`, section `[keys]`. Gamepad: A ollie, X flip, B grab, Y grind, LB / RB spin,
LT nollie, RT revert, sticks move and camera.

### Help wanted

I don't have much time to test the co-op through and through, so not everything may work 100%.
You can help: play it and report bugs or anything odd in [Issues](https://github.com/LeftGamer1/thug2-coop/issues).
Please attach `coop.log` from the game folder.

### Known issues

- A crash on the splash screen at a picture wider than 1920 (the game itself, plain THEMOD 1.3 too) is fixed
  by the mod now. If the game still crashes there, try `mode=windowed` in `coop.ini`, section `[display]`.
- Display: `coop.ini`, section `[display]`: `mode=borderless` (default), `fullscreen` or `windowed`;
  `resolution=desktop` (default, your monitor's) or `2560x1440`, `3840x2160`, `1280x720`... In `fullscreen` your
  monitor must support it, otherwise the game closes after the splash screen.
- World objects are each player's own (a wall one breaks stays whole for the other): so both can do each
  shared goal themselves.

### Build

- `src/`: `dinput8.dll` (a proxy that patches the game at load time), C++17, MinGW i686.
- `patcher/`: `patch_themod.bat` / `.ps1`, makes `THEMOD 1.3.exe` from `THUG2.exe`.
- `qb/coop.ns`: game scripts in [NeverScript](https://github.com/byxor/NeverScript), compiled to `coop.qb`
  and added to THEMOD's `thethug2mod.prx`.

```bash
docker build -t thug2coop-mingw:1 tools/
./build.sh                                   # out/dinput8.dll
NS=/path/to/ns ORIG_PRX=/path/to/thethug2mod.prx GAME=/path/to/Game ./install.sh
./pack.sh                                    # out/thug2-coop.zip
```

### License

MIT, see [LICENSE](LICENSE). Tony Hawk's Underground 2 belongs to Activision, THEMOD to its authors.

### Credits

Made with [Claude Opus 5.5](https://www.anthropic.com/claude) in Claude Code, following the
[coop-anything](https://github.com/cranebeaker/coop-anything) playbook.
The wide screen code (wider view, 4:3 main menu, screen flash fix) and the gamepad layout come from
[PARTYMOD for THUG2](https://github.com/PARTYMANX/partymod-thug2) by PARTYMANX (MIT).

### Support

Like the mod? [Support the author](https://www.donationalerts.com/r/leftgamerr)

---

## Русский

Кооператив для сюжета **Tony Hawk's Underground 2** (PC) по сети, вдвоём, поверх **THEMOD 1.3**.
Хост играет свой Story Mode, друг заходит в его игру и катается рядом в том же уровне: NPC, катсцены, задания.

### Что умеет

- Друг получает сюжет хоста (главы, выполненные цели, флаги мира) при входе и при каждой смене уровня.
- Катсцены и камеры целей идут у обоих, пропуск (Esc / num2) у любого пропускает у обоих.
- Общие задания: взятое одним задание запускается у обоих и засчитывается, когда его выполнят оба.
- Имя и расстояние над другом (видно сквозь стены), телепорт к другу, чат (на русской раскладке печатаются латинские буквы с тех же клавиш).
- Гостевые персонажи и напарник-профи синхронизируются.
- Коллизия скейтеров по желанию (`player_collision=1`).
- Геймпады XInput (Xbox; PlayStation — через Steam Input или DS4Windows).
- Окно без рамки по умолчанию (работает Alt+Tab, игра идёт и за другими окнами), любое разрешение до 4K,
  на широком экране обзор шире (без растяжения), главное меню остаётся 4:3.
- Выход из кооператива клавишей (F9 дважды); сохранение друга в кооперативе хранит его собственный сюжет.

### Установка

Нужна Tony Hawk's Underground 2 для PC. Мод ставят оба игрока. Ставить THEMOD 1.3 заранее не нужно:
всё для него есть в архиве.

> **Про THEMOD 1.3.** Кооператив сделан на основе THEMOD 1.3, и патчер повторяет его установщик. Мы не нашли ни
> автора THEMOD, ни где его скачать (его сайт thmods.com больше не работает), поэтому решили сделать свой патчер:
> он превращает ваш `THUG2.exe` в `THEMOD 1.3.exe` (те же 126 байт, что меняет установщик THEMOD), а
> `thethug2mod.prx` от THEMOD лежит в архиве. Если знаете автора, ссылку на скачивание или другие ресурсы —
> [напишите в Issues](https://github.com/LeftGamer1/thug2-coop/issues).

1. Скачайте `thug2-coop.zip` из [Releases](https://github.com/LeftGamer1/thug2-coop/releases).
2. Если THEMOD уже стоит — сохраните свой `Data\pre\thethug2mod.prx`.
3. Распакуйте архив в папку игры (где лежит `THUG2.exe`) с заменой.
4. Нет `THEMOD 1.3.exe`? Запустите `patch_themod.bat`: он сделает его из `THUG2.exe` (сам `THUG2.exe` не меняется).
   Игру запускайте через `THEMOD 1.3.exe`.
5. В `coop.ini` впишите `name=` (оба) и `host_address=` (только друг).
6. У хоста должны быть доступны UDP 5153 и TCP 5154 (Radmin VPN / ZeroTier / Porthole).

Подробно — в [README_RU.txt](README_RU.txt): настройки, управление, ограничения, решение проблем.

### Управление

| Клавиша | Действие |
|---|---|
| F10 | хост: открыть кооператив |
| F11 | друг: подключиться |
| F12 | телепорт к другу |
| F9 дважды | выйти из кооператива |
| Enter | чат (Enter на пустой строке или Esc закрывают его) |

Клавиши меняются в `coop.ini`, раздел `[keys]`. Геймпад: A — ollie, X — flip, B — grab, Y — grind, LB / RB —
вращение, LT — nollie, RT — revert, стики — движение и камера.

### Нужна помощь

У меня мало времени, чтобы проверить кооператив на 100%, так что что-то может работать не так.
Вы можете помочь: играйте и пишите о багах и странностях в [Issues](https://github.com/LeftGamer1/thug2-coop/issues).
Приложите `coop.log` из папки игры.

### Известные проблемы

- Вылет на заставке при картинке шире 1920 (сама игра, и обычный THEMOD 1.3 тоже) теперь исправляет мод.
  Если игра всё равно вылетает там, попробуйте `mode=windowed` в `coop.ini`, раздел `[display]`.
- Экран: `coop.ini`, раздел `[display]`: `mode=borderless` (по умолчанию), `fullscreen` или `windowed`;
  `resolution=desktop` (по умолчанию, как у монитора) или `2560x1440`, `3840x2160`, `1280x720`... В `fullscreen`
  монитор должен поддерживать разрешение, иначе игра закроется после заставки.
- Объекты мира у каждого свои (стена, сломанная одним, у другого целая): так оба могут сами выполнить
  каждое общее задание.

### Сборка

- `src/` — `dinput8.dll` (прокси, патчит игру при загрузке), C++17, MinGW i686.
- `patcher/` — `patch_themod.bat` / `.ps1`, делает `THEMOD 1.3.exe` из `THUG2.exe`.
- `qb/coop.ns` — скрипты игры на [NeverScript](https://github.com/byxor/NeverScript), компилируются в `coop.qb`
  и добавляются в `thethug2mod.prx` THEMOD.

Команды сборки — выше, в разделе [Build](#build).

### Лицензия

MIT, см. [LICENSE](LICENSE). Tony Hawk's Underground 2 принадлежит Activision, THEMOD — его авторам.

### Благодарности

Сделано под руководством [Claude Opus 5.5](https://www.anthropic.com/claude) в Claude Code, по методике
[coop-anything](https://github.com/cranebeaker/coop-anything).
Код для широкого экрана (шире обзор, меню 4:3, исправление вспышки) и раскладка геймпада — из
[PARTYMOD for THUG2](https://github.com/PARTYMANX/partymod-thug2) от PARTYMANX (MIT).

### Поддержка

Понравился мод? [Отблагодарить автора](https://www.donationalerts.com/r/leftgamerr)
