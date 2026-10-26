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

**Chat: Russian is transliterated by itself / Чат: русский текст сам пишется латиницей**
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
  chat with Cyrillic transliteration.
- Guest characters and the pro teammate are synced.
- Optional skater collision (`player_collision=1`).

### Install

You need Tony Hawk's Underground 2 for PC with **THEMOD 1.3** installed (not included here). Both players install the mod.

1. Download `thug2-coop.zip` from [Releases](https://github.com/LeftGamer1/thug2-coop/releases).
2. Keep a copy of your `Data\pre\thethug2mod.prx` (THEMOD's original).
3. Unpack the archive into the game folder (the one with `THEMOD 1.3.exe`), replacing files.
4. In `coop.ini` set `name=` (both) and `host_address=` (the friend only).
5. The host's UDP 5153 and TCP 5154 must be reachable (Radmin VPN / ZeroTier / Porthole).

Details: [README_EN.txt](README_EN.txt) (settings, controls, limits, troubleshooting).

### Controls

| Key | Action |
|---|---|
| F10 | host: open co-op |
| F11 | friend: join |
| F12 | teleport to your friend |
| Enter | chat |

The keys can be changed in `coop.ini`, section `[keys]`.

### Build

- `src/`: `dinput8.dll` (a proxy that patches the game at load time), C++17, MinGW i686.
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
- Имя и расстояние над другом (видно сквозь стены), телепорт к другу, чат с транслитом кириллицы.
- Гостевые персонажи и напарник-профи синхронизируются.
- Коллизия скейтеров по желанию (`player_collision=1`).

### Установка

Нужны: Tony Hawk's Underground 2 для PC и установленный **THEMOD 1.3** (в репозиторий не входит). Мод ставят оба игрока.

1. Скачайте `thug2-coop.zip` из [Releases](https://github.com/LeftGamer1/thug2-coop/releases).
2. Сохраните свой `Data\pre\thethug2mod.prx` (оригинал THEMOD).
3. Распакуйте архив в папку игры (где лежит `THEMOD 1.3.exe`) с заменой.
4. В `coop.ini` впишите `name=` (оба) и `host_address=` (только друг).
5. У хоста должны быть доступны UDP 5153 и TCP 5154 (Radmin VPN / ZeroTier / Porthole).

Подробно — в [README_RU.txt](README_RU.txt): настройки, управление, ограничения, решение проблем.

### Управление

| Клавиша | Действие |
|---|---|
| F10 | хост: открыть кооператив |
| F11 | друг: подключиться |
| F12 | телепорт к другу |
| Enter | чат |

Клавиши меняются в `coop.ini`, раздел `[keys]`.

### Сборка

- `src/` — `dinput8.dll` (прокси, патчит игру при загрузке), C++17, MinGW i686.
- `qb/coop.ns` — скрипты игры на [NeverScript](https://github.com/byxor/NeverScript), компилируются в `coop.qb`
  и добавляются в `thethug2mod.prx` THEMOD.

Команды сборки — выше, в разделе [Build](#build).

### Лицензия

MIT, см. [LICENSE](LICENSE). Tony Hawk's Underground 2 принадлежит Activision, THEMOD — его авторам.

### Поддержка

Понравился мод? [Отблагодарить автора](https://www.donationalerts.com/r/leftgamerr)
