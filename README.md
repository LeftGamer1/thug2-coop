# THUG2 Story Co-op

Кооператив для сюжета **Tony Hawk's Underground 2** (PC) по сети, вдвоём, поверх **THEMOD 1.3**.
Хост играет свой Story Mode, друг заходит в его игру и катается рядом в том же уровне: NPC, катсцены, задания.

*English: see [README_EN.txt](README_EN.txt).*

💸 [Отблагодарить автора](https://www.donationalerts.com/r/leftgamerr)

## Что умеет

- Друг получает сюжет хоста (главы, выполненные цели, флаги мира) при входе и при каждой смене уровня.
- Катсцены и камеры целей идут у обоих, пропуск (Esc / num2) у любого пропускает у обоих.
- Общие задания: взятое одним задание запускается у обоих и засчитывается, когда его выполнят оба.
- Имя и расстояние над другом (видно сквозь стены), телепорт к другу, чат с транслитом кириллицы.
- Гостевые персонажи и напарник-профи синхронизируются.
- Коллизия скейтеров по желанию (`player_collision=1`).

<details>
<summary>Скриншоты</summary>

**Лобби: хост открыл кооператив, друг подключился**
![Лобби](docs/01_lobby.png)

**Оба в одном уровне**
![Вместе](docs/02_together.png)

**Прыжок хоста виден у друга**
![Прыжок](docs/03_jump.png)

**Чат: русский текст сам пишется латиницей**
![Чат](docs/05_chat.png)

**Друг взял задание у NPC — оно запустилось у обоих**
![Задание взято](docs/06_goal_taken.png)

**Оба проходят задание**
![Оба проходят](docs/07_goal_both_run.png)

**Друг выполнил и ждёт хоста**
![Друг ждёт](docs/08_goal_friend_waits.png)

**Хост доделывает задание**
![Хост доделывает](docs/09_goal_host_finishing.png)

**Задание засчитано обоим**
![Засчитано обоим](docs/10_goal_won_both.png)

</details>

## Установка

Нужны: Tony Hawk's Underground 2 для PC и установленный **THEMOD 1.3** (в репозиторий не входит).

1. Скачайте `thug2-coop.zip` из [Releases](https://github.com/LeftGamer1/thug2-coop/releases).
2. Сохраните свой `Data\pre\thethug2mod.prx` (оригинал THEMOD).
3. Распакуйте архив в папку игры (где лежит `THEMOD 1.3.exe`) с заменой.
4. В `coop.ini` впишите `name=` (оба) и `host_address=` (только друг).
5. У хоста должны быть доступны UDP 5153 и TCP 5154 (Radmin VPN / ZeroTier / Porthole).

Подробно — в [README_RU.txt](README_RU.txt): настройки, управление, ограничения, решение проблем.

## Управление

| Клавиша | Действие |
|---|---|
| F10 | хост: открыть кооператив |
| F11 | друг: подключиться |
| F12 | телепорт к другу |
| Enter | чат |

Клавиши меняются в `coop.ini`, раздел `[keys]`.

## Сборка

- `src/` — `dinput8.dll` (прокси, патчит игру при загрузке), C++17, MinGW i686.
- `qb/coop.ns` — скрипты игры на [NeverScript](https://github.com/byxor/NeverScript), компилируются в `coop.qb` и добавляются в `thethug2mod.prx` THEMOD.

```bash
docker build -t thug2coop-mingw:1 tools/
./build.sh                                   # out/dinput8.dll
NS=/path/to/ns ORIG_PRX=/path/to/thethug2mod.prx GAME=/path/to/Game ./install.sh
./pack.sh                                    # out/thug2-coop.zip
```

## Лицензия

MIT, см. [LICENSE](LICENSE). Tony Hawk's Underground 2 принадлежит Activision, THEMOD — его авторам.

## Поддержка

Понравился мод? [Отблагодарить автора](https://www.donationalerts.com/r/leftgamerr)
