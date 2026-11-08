# Lunaris – Plan

Dieses Dokument ist der einzige Plan. Es bleibt kurz. Neue Ideen kommen in die
Später-Liste, nicht in den Plan.

**Nächster Schritt:** CI aufsetzen (Linux GCC, Windows MSVC), danach der
ASan-/UBSan-Job.

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
- `buffer` kommt in 0.1. Für `vector` gibt es einen Hook zu eigenen
  Vektortypen. Der Kern hängt von keiner Math-Bibliothek ab.
- Lizenz MIT. Doku: Doxygen für die API, `docs/` für Handbuch und Plan.

## Tabelle: Sol3 → Lunaris → Luau-Abweichung

Namen sind vorläufig.

| Nr | Sol3 | Lunaris | Luau-Abweichung |
|----|------|---------|-----------------|
| 1 | `state`, `open_libraries` | `State`, `openLibraries` | kein `io`, `os` stark beschnitten, dafür `buffer` und `vector` |
| 2 | `script`, `safe_script` | `script`, `safeScript` | Compiler erzeugt Bytecode, `compile()` und `load()` getrennt, Fremd-Bytecode nur als „trusted“ |
| 3 | Variablen, `get<T>` | `state["x"]`, `get<T>` | Zahlen sind nur `double`, `vector` über Hook, `buffer` als Kopie |
| 4 | Lua-Funktionen aufrufen | `Function`, `safeCall` | Traceback im Message-Handler, Fehlerwerte sind nicht nur Strings |
| 5 | C++-Funktionen binden | `setFunction` | `catch(...)` an der Grenze, `lua_error` erst nach dem try/catch |
| 6 | Tabellen | `Table` | Zugriffe können Metamethoden auslösen oder an readonly scheitern |
| 7 | Coroutinen (0.2) | | `lua_newthread`, `lua_resume`, Handles dürfen nicht am Haupt-State hängen |
| 8 | Usertypes | | Userdata-Tags mit Destruktor, Tag-Limit prüfen (`LUA_UTAG_LIMIT`) |
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
- [ ] CI baut und testet auf Linux (GCC) und Windows (MSVC)
- [ ] ASan-/UBSan-Job auf Linux
- [ ] Tabellenzeile 1 (`State` fertig, `openLibraries` offen)
- [ ] Tabellenzeilen 2–6, jede mit Example, Test und Doxygen-Kommentar
- [ ] Roundtrip-Tests: C++-Exception durch einen Luau-Frame, Luau-Fehler nach
      C++, nicht-String-Fehlerwert
- [ ] Doxygen ohne Warnungen, `.clang-tidy` aktiv
- [ ] `README.md`, `LICENSE`
- [ ] Git-Tag `v0.1.0`

## Offene Entscheidungen

- Bleibt Luaus `vector` fest bei `float` mit 3 Komponenten, oder reicht
  Lunaris `LUAU_VECTOR_DOUBLE` und `LUAU_VECTOR_FOUR_WIDE` durch?
- Handle-Lebensdauer: Doku plus Debug-Assert, oder gemeinsamer Kontext, damit
  ein Handle den State nicht überlebt.
- Usertypes: Userdata-Tags oder Metatables, und was oberhalb des Tag-Limits.
- Was liefert `Error::what()` bei einem nicht-String-Fehlerwert.
- Speicherfehler beim Erzeugen: `std::bad_alloc` bis `lunaris::Error` kommt.

## Später

- Gecachte Handles für heiße Pfade
- Adapter für externe Vektor-Bibliotheken (optionaler Header)
- Typisierte Deklarationsdateien für Luau-Typchecking
- `Container`, `object`
