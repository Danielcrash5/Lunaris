# Lunaris – Plan

Dieses Dokument ist der einzige Plan. Es bleibt kurz. Neue Ideen kommen in die
Später-Liste, nicht in den Plan.

**Nächster Schritt:** `State` auf den gemeinsamen Kontext umbauen (ohne neue
Features), danach `Error`, danach `script` und `safeScript` mit `ScriptResult`
(Tabellenzeile 2).

## Zielsatz

Lunaris ist eine öffentliche, exception-basierte C++23-Bibliothek mit
Sol3-Gefühl für Luau (MIT), die später Scripts sandboxen kann (CPU-Zeit,
Speicher, geschützte Globals).

## Regeln

1. Kein Feature ohne kompilierendes Example, Test und Doxygen-Kommentar.
2. Nur eine Phase ist offen. Neue Ideen kommen in die Später-Liste.
3. Namen: PascalCase für Typen, camelCase für Funktionen. STL-Konzepte
   (`begin`, `end`, `size`, `value_type`) behalten die STL-Schreibweise.
   Namespace `lunaris`, Makros `LUNARIS_…`. Durchsetzung per `clang-tidy`.
4. Fehler: Exceptions als Standard. Ein `safeScript`-Pendant mit
   `std::expected` gibt es nur für Scripts und Callbacks.
5. Keine Verweise auf andere Projekte im Repo.
6. Kein statischer, veränderlicher Zustand in Lunaris (Threads nicht verhindern).

## Entscheidungen

- C++23 (`std::expected`), Exceptions erlaubt, `LUA_USE_LONGJMP` bleibt aus.
- Statische Bibliothek, Templates im Header, Einbindung per `add_subdirectory`.
- Luau ist vendort (`thirdparty/luau`), Version steht in `VERSION.md` dort.
- Version einmalig in `project(Lunaris VERSION …)`, Release per Git-Tag.
- Global-Scope ist der Default. Die Sandbox ist ein expliziter Modus.
- `buffer` kommt in 0.1. Eigene Vektortypen werden über einen Hook
  eingebunden. Lunaris bleibt beim Luau-Standard (float, 3 Komponenten),
  wrappt Luaus `vector`-Bibliothek nicht und reicht keine Luau-Build-Optionen
  durch. Der Kern hängt von keiner Math-Bibliothek ab.
- Handles halten einen gemeinsamen Kontext (`std::shared_ptr`), den auch der
  `State` besitzt. `State` schließt den `lua_State` explizit über den Kontext.
  Handles werden mit dem Schließen des States ungültig. Ihr Zugriff wirft dann
  eine `Error`, ihr Destruktor bleibt still. Das Speicherlayout hängt nicht vom
  Build-Typ ab. Umgebaut wird `State` mit dem ersten Handle-Typ (Zeile 3).
- `Error::what()` liefert immer einen String: Strings unverändert, Zahlen und
  Booleans als Text, alles andere als Hinweis mit dem Typnamen. Keine
  Metamethoden. `kind` kommt getrennt aus dem Luau-Status. Bei `LUA_ERRMEM`
  kann stattdessen `std::bad_alloc` ankommen.
- `script` liefert ein `ScriptResult`, `safeScript` liefert
  `std::expected<ScriptResult, Error>` (`[[nodiscard]]`). Die Rückgabewerte
  liegen als Registry-Referenzen im Ergebnisobjekt, unabhängig von der
  Reihenfolge, in der Objekte zerstört werden. Das Objekt hält den gemeinsamen
  Kontext. In Zeile 2 bietet es `size()` und `type(i)`, `get<T>` kommt mit
  Zeile 3.
- `script` nimmt nur Quelltext (`std::string_view`) und einen Chunk-Namen,
  kein `scriptFile`. Lunaris stellt dem Namen immer `=` voran. Der Name wird
  für Luau nullterminiert kopiert.
- `Error` trägt `kind` (`compile`, `runtime`, `memory`; `type` kommt mit
  Zeile 3), Meldung, Chunk-Name, Zeile (`std::optional<int>`) und Traceback
  ohne Lunaris-Frames. Bei Laufzeitfehlern kommen Chunk und Zeile aus
  `lua_getinfo` im Message-Handler (erste Lua-Ebene), bei Compile-Fehlern aus
  der Meldung.
- Bis `lunaris::Error` existiert, wirft `State` bei Speichermangel
  `std::bad_alloc`.
- Usertypes verwenden Userdata-Tags. Die Zuordnung Typ → Tag liegt pro State
  in einer Map, nie in einer statischen Variablen. Beim Erreichen des
  Tag-Limits wirft die Registrierung eine `Error`.
- Lizenz MIT. Doku: Doxygen für die API, `docs/` für Handbuch und Plan.

## Tabelle: Sol3 → Lunaris → Luau-Abweichung

Namen sind vorläufig.

| Nr | Sol3 | Lunaris | Luau-Abweichung |
|----|------|---------|-----------------|
| 1 | `state`, `open_libraries` | `State`, `openLibraries(Library…)`, `openAllLibraries()` | kein `io`, `os` stark beschnitten, dafür `buffer` und `vector`. Libraries werden mit Name aufgerufen wie in `luaL_openlibs`. `luaL_sandbox` muss danach laufen |
| 2 | `script`, `safe_script` | `script`, `safeScript` | Compiler erzeugt Bytecode, Compile-Fehler stecken im Bytecode. Intern getrennt in compile, load und run, öffentlich erst bei Bedarf (Bytecode-Cache). Fremd-Bytecode nur als „trusted“ |
| 3 | Variablen, `get<T>` | `state["x"]`, `get<T>` | Zahlen sind nur `double`, `vector` über Hook, `buffer` als Kopie |
| 4 | Lua-Funktionen aufrufen | `Function`, `safeCall` | Traceback im Message-Handler, Fehlerwerte sind nicht nur Strings |
| 5 | C++-Funktionen binden | `setFunction` | `catch(...)` an der Grenze, `lua_error` erst nach dem try/catch |
| 6 | Tabellen | `Table` | Zugriffe können Metamethoden auslösen oder an readonly scheitern |
| 7 | Coroutinen (0.2) | | `lua_newthread`, `lua_resume`, Handles dürfen nicht am Haupt-State hängen |
| 8 | Usertypes | | Userdata-Tags mit Destruktor, Zuordnung pro State, `Error` beim Erreichen von `LUA_UTAG_LIMIT` |
| 9 | Environments, Sandbox | | `luaL_sandbox`, `luaL_sandboxthread`, Interrupt-Callback, Allocator mit Limit |
| 10 | Container, `object` | | später |

Reihenfolge: Zeilen 1–6 sind 0.1, danach 7, 8, 9.

## Non-Goals

- Paketmanager und `require`-Loader (Nutzer bauen ihren eigenen).
- Bytecode-Cache, Hot-Reload-Mechanik und Thread-Scheduler. Lunaris verhindert
  sie nicht, bietet sie aber nicht an.
- Lua 5.x und LuaJIT.
- `-fno-exceptions`.
- Eine Math-Bibliothek im Kern.
- Safety-Makros oder ein unchecked-Pfad.

## 0.1 fertig, wenn

- [x] `project(Lunaris VERSION 0.1.0)`, Target `Lunaris::Lunaris`, Tests nur im
      eigenen Repo
- [x] `thirdparty/luau/VERSION.md`
- [x] CI baut und testet auf Linux (GCC) und Windows (MSVC)
- [x] ASan-/UBSan-Job auf Linux
- [x] Tabellenzeile 1 (`State`, `openLibraries`, `openAllLibraries`)
- [ ] Tabellenzeilen 2–6, jede mit Example, Test und Doxygen-Kommentar
- [ ] Roundtrip-Tests: C++-Exception durch einen Luau-Frame, Luau-Fehler nach
      C++, nicht-String-Fehlerwert
- [ ] `State` hält einen gemeinsamen Kontext, ein Handle erkennt das Ende des
      States
- [ ] Handle-Test: Zugriff nach dem Ende des States wirft eine `Error`, der
      Destruktor bleibt still
- [x] `.clang-tidy` aktiv, eigener CI-Job
- [x] Doxygen ohne Warnungen, eigener CI-Job
- [x] `README.md`, `LICENSE`
- [ ] Git-Tag `v0.1.0`

## Offene Entscheidungen

- Welche `kind` hat die `Error`, wenn ein Handle nach dem Ende seines States
  benutzt wird.

## Später

- Metatable-Fallback für Usertypes oberhalb des Tag-Limits
- Experimentelle Luau-Libraries `integer` und `class` (hängen an Fast-Flags)
- Gecachte Handles für heiße Pfade
- Adapter für externe Vektor-Bibliotheken (optionaler Header)
- Typisierte Deklarationsdateien für Luau-Typchecking
- `Container`, `object`
