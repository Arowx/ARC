# Using ARC in your other projects

ARC lives in one place, `D:\Projects\ARC`. Every game points at it, so an engine fix reaches all games at once. A game that has shipped can take a frozen copy instead.

## 1. Start a new game

You need Python, which is only used for this script:

```bat
cd D:\Projects\ARC
python tools\new_game.py D:\Projects\MyGame --title "My Game"          & rem C
python tools\new_game.py D:\Projects\MyGame --title "My Game" --cpp    & rem C++
```

This creates:

```
D:\Projects\MyGame\
  src\main.c        working starter game; add more .c/.cpp files here and they are built automatically
  assets\           models, textures, data; copied next to the .exe and packed into the web build
  arc.txt           one line: where ARC is (here "../ARC")
  build.bat         Windows / Visual Studio:  build.bat   or   build.bat headless
  build.sh          Linux, WSL, Git Bash:     ./build.sh linux | headless | win | web
  AGENTS.md         tells AI agents where the engine docs are and the rules for this game
  .gitignore
```

Build and run it:

```bat
cd D:\Projects\MyGame
build.bat
build\mygame.exe
build\mygame.exe --headless --cmd "step 60; dump; shot a.png; quit"
```

## 2. Shared or vendored

| Mode | `arc.txt` says | Engine updates | Use it when |
|---|---|---|---|
| **Shared** (default) | `../ARC` | Automatic: the next build uses the latest ARC | Games in development. |
| **Vendored** (`--vendor`) | `arc` (a copy inside the game) | Only when you copy again | A game that has shipped, a jam build you must not break, or a game you hand to someone else. |

You can switch a game at any time:

- Edit `arc.txt` to change where it looks.
- Or set the `ARC_DIR` environment variable to override it for one build.

To freeze a shared game, run `new_game.py --vendor` into a temporary folder and copy its `arc\` folder into the game. Then set `arc.txt` to `arc`.

## 3. Put ARC under version control

This is recommended. It makes "which engine did this game ship with?" answerable.

```bat
cd D:\Projects\ARC
git init
git add .
git commit -m "ARC 0.1.0"
git tag v0.1.0
```

For each release, raise `ARC_VERSION` in `include/arc.h`, add a `CHANGELOG.md` entry, commit and tag.

Games can then pin a version in either of two ways:

- **Vendor a tagged copy** (section 2).
- **Use a git submodule:** `git submodule add ../ARC arc`, then `git -C arc checkout v0.1.0`, and set `arc.txt` to `arc`.

Games can also require a minimum version at compile time:

```c
#include "arc.h"
#if ARC_VERSION_NUM < 100      /* major*10000 + minor*100 + patch: 100 = 0.1.0 */
#error "this game needs ARC 0.1.0 or newer"
#endif
```

## 4. Rules for the shared engine

- **Engine changes happen in `D:\Projects\ARC` only, never inside a game.** If a game needs a feature, add it to ARC with a test in `tests/` and a line in `CHANGELOG.md`. Then every game can use it.
- **Add a feature to ARC only when a second game could use it.** Game-specific systems stay in the game, for example the Paradroid transfer duel or Moonbase Raid's mission scripts.
- **Don't break the API casually.** If you must rename or change a function, note it under "Breaking" in the changelog. Then rebuild every shared game (section 6).
- **After any engine change**, run `build.bat test` (or `./build.sh test`) in ARC. Then rebuild the games you are working on.

## 5. Working with AI agents across projects

- Each game's `AGENTS.md` sends the agent to ARC's `AGENTS.md`, `docs/API.md` and `docs/ARC.md`.
- **Claude Code** with the game folder open: add ARC as a second working directory so the agent can read the engine source. Use `/add-dir D:\Projects\ARC`, or start it with `--add-dir`.
- **Claude in the desktop app:** connect both folders, the game and `D:\Projects\ARC`.
- When an agent needs an engine change, tell it to make the change in ARC (with a test) rather than patching around it in the game.
- `docs/API.md` is generated from `include/arc.h`. After changing the header, run `python tools/gen_api_docs.py` so agents see the current API.

## 6. Rebuild everything after an engine update

Each game is self-contained, so this is a loop over the folders that have an `arc.txt`. For example, in Git Bash or WSL:

```sh
for g in /d/Projects/*/arc.txt; do (cd "$(dirname "$g")" && ./build.sh headless && echo "OK $(basename "$(dirname "$g")")") || echo "FAILED $g"; done
```

If a game keeps a recorded replay (see `AGENTS.md`, "Regression testing pattern"), replay it after the rebuild and diff the dump. That catches behaviour changes as well as compile errors.
