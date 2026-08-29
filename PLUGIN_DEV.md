# Návod na vývoj pluginů pro Open Salamander (Template & Best Practices)

Tento dokument slouží jako kompletní vzor a kuchařka pro tvorbu a údržbu pluginů pro **Open Salamander** (a Dark Mode fork **KRtkovo-eu-AI/salamander**), postavený na ověřených postupech z vývoje pluginu `salamander-sqlite-plugin`.

---

## 1. Architektura a adresářová struktura pluginu

Plugin do Salamandera se skládá z:
- **`*.spl`** – Samotná dynamická knihovna pluginu (Windows DLL přejmenovaná na `.spl`).
- **`lang/*.slg`** – Jazykové knihovny s přeloženými texty a dialogy (`english.slg`, `czech.slg`).

### Doporučená adresářová struktura:

```
salamander-gdrive-plugin/
├── src/
│   ├── gdrive.cpp          # Hlavní vstup, DllMain, CPluginInterface, CPluginInterfaceForFS/Viewer
│   ├── gdrive.h            # Hlavičkový soubor pluginu
│   ├── gdrive.def          # Exportní definice (SalamanderPluginEntry2)
│   ├── gdrive.rc           # Hlavní resources (verze, ikony)
│   ├── gdrive.rh2          # Definice číselných ID resource a příkazů
│   ├── versinfo.rh2        # Hlavička pro verzi pluginu
│   ├── precomp.h           # Precompiled headers se Salamander SDK
│   ├── gdrivedarkmode.h    # Implementace Dark Mode podpory
│   ├── gdrivedarkmode.cpp
│   └── lang/
│       ├── lang.rh         # Společné resource ID pro jazyky
│       ├── lang_en.rc      # Anglické texty a dialogy
│       └── lang_cs.rc      # České texty a dialogy
├── test/                   # Unit testy pro nezávislou engine vrstvu
├── Makefile.mingw          # Sestavení pomocí MinGW-w64 (GCC/G++)
├── gdrive.vcxproj          # Sestavení pomocí Microsoft Visual Studio (MSVC)
├── README.md               # Dokumentace a návod k instalaci
└── .gitignore
```

---

## 2. Licencování a hlavičky souborů (SPDX & Autorské záznamy)

Pluginy pro Open Salamander podléhají licenci **GPL-2.0-or-later** (v souladu s licencí jádra Open Salamander SDK). Pokud jsou části kódu přejímány nebo portovány z moderního forku RedSalamander (který používá licenci **MIT**), je kód plně kompatibilní a v souborech je nutné uvádět příslušné autory a SPDX identifikátory.

### 1. Formát hlaviček ve zdrojových souborech (`.cpp`, `.h`, `.rh`, `.rh2`, `.rc2`)

Každý vytvořený nebo upravovaný soubor musí na prvních řádcích obsahovat standardizovanou SPDX hlavičku:

#### Běžné zdrojové soubory pluginu:
```cpp
// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later
```

#### Soubory obsahující integraci Dark Mode (`*darkmode.cpp`, `*darkmode.h`, `versinfo.rh2`):
```cpp
// SPDX-FileCopyrightText: 2026 fila73 & Ondrej Kotas
// SPDX-FileContributor: Dark Mode host policy and theme integration based on fork by Ondrej Kotas (KRtkovo-eu-AI)
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later
```

### 2. Struktura souboru `LICENSE` v kořenu repozitáře

Kořenový soubor `LICENSE` obsahuje text **GNU General Public License v2.0**:

```text
GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

Copyright (C) 1989, 1991 Free Software Foundation, Inc.
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

Everyone is permitted to copy and distribute verbatim copies
of this license document, but changing it is not allowed.

Copyright (C) 2026 fila73. Inspired by Red Salamander.
This plugin is compatible with Open Salamander and is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later).
```

---

## 3. Klíčové rozhraní Open Salamander Plugin SDK

Plugin komunikuje se Salamanderem přes virtuální C++ třídy definované v `salamander-plugins/salamand/plugins/shared/`:

### Hlavní rozhraní:
1. **`CPluginInterface`**:
   - `Init()` – Inicializace pluginu po načtení.
   - `Connect(HWND parent, CSalamanderConnectAbstract* salamander)` – Zde plugin registruje své schopnosti v Salamanderu (např. `salamander->AddFileSystem(...)`, `salamander->AddViewer(...)`, `salamander->AddIcon(...)`).
   - `LoadConfiguration(CSalamanderRegistryAbstract* registry, HKEY regKey)` – Načtení konfigurace z registru.
   - `SaveConfiguration(CSalamanderRegistryAbstract* registry, HKEY regKey)` – Uložení konfigurace.
   - `About(HWND parent)` a `Configuration(HWND parent)` – Standardní dialogy O aplikaci a Nastavení.

2. **`CPluginInterfaceForFS`** (pro souborové systémy / Google Drive):
   - Procházení adresářů (`OpenDirectory`, `ReadDirectory`, `CloseDirectory`).
   - Práce se soubory (`GetFileInfo`, `Copy`, `Move`, `Delete`, `CreateDirectory`).
   - Souborové operace na pozadí.

3. **`CPluginInterfaceForViewer`** (pro interní prohlížeč souborů):
   - `ViewFile(...)` – Spuštění samostatného vlákna prohlížeče (`CViewerThread`).

---

## 4. Zásadní pravidla a řešení známých chyb (Gotchas)

### ⚠️ 1. Statické linkování běhových knihoven v MinGW (`-static`)
Pokud se použije MinGW GCC/G++, kompilátor ve výchozím stavu dynamicky linkuje `libwinpthread-1.dll`, `libstdc++-6.dll` a `libgcc_s_dw2-1.dll`. Na jiných počítačích pak Salamander zahlásí:
> `Chyba 126: Uvedený modul nebyl nalezen.`

**Řešení v `Makefile.mingw`**:
```make
LDFLAGS = -shared -s -static -static-libgcc -static-libstdc++ \
    -lshlwapi -lcomctl32 -lcomdlg32 -lole32 -lshell32 -lgdi32 -lmsimg32 -luxtheme -ldwmapi
```
Vždy zkontrolujte závislosti příkazem:
```powershell
objdump -p gdrive.spl | Select-String "DLL Name"
```
Musí obsahovat pouze standardní knihovny Windows (KERNEL32, USER32, GDI32, COMCTL32, api-ms-win-crt-*).

---

### ⚠️ 2. Ošetření prázdného klíče registru při prvním spuštění (Chyba 6)
Při prvním spuštění, kdy v registru ještě neexistují klíče pluginu, předává Salamander do `LoadConfiguration` ukazatel `regKey == NULL`. Pokud se zavolá `registry->GetValue(regKey, ...)`, skončí to chybou:
> `(6) Neplatný popisovač. Registry value: Expected type...`

**Správná implementace**:
```cpp
void WINAPI CPluginInterface::LoadConfiguration(CSalamanderRegistryAbstract* registry, HKEY regKey)
{
    // Nastavit výchozí hodnoty
    CfgSavePosition = FALSE;
    CfgDefaultPageSize = 200;

    // Číst z registru POUZE pokud klíč existuje
    if (registry != NULL && regKey != NULL)
    {
        registry->GetValue(regKey, "SavePosition", REG_DWORD, &CfgSavePosition, sizeof(CfgSavePosition));
        // ...
    }
}
```

---

### ⚠️ 3. Automatická registrace a migrace konfigurace v `Connect()`
Aby uživatel nemusel ručně konfigurovat asociace v nastavení Salamandera, plugin se sám zaregistruje v metodě `Connect()`:

```cpp
void WINAPI CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    // 1. Zaregistrovat ikony pluginu v GUI Salamandera
    CGUIIconListAbstract* iconList = salamander->GetPluginIconList();
    if (iconList)
    {
        HICON hIcon = LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_PLUGIN));
        if (hIcon) iconList->AddIcon(hIcon);
    }

    // 2. Pro prohlížeče zaregistrovat podporované masky souborů (nebo AddFileSystem pro FS)
    salamander->AddViewer("*.gdrive;*.gdoc;*.gsheet", FALSE);

    // 3. Pro stávající uživatele zajistit aktualizaci konfigurace při změně verze
    if (ConfigVersion < 2)
    {
        salamander->AddViewer("*.gdrive;*.gdoc;*.gsheet", TRUE);
    }
}
```

---

### ⚠️ 4. Správné škálování pro High-DPI monitory (125 %, 150 %, 200 %)
Nikdy nenastavujte výšky ovládacích prvků (Edit boxy, lišty, tlačítka) pevnými pixely (např. 22px). Na monitorech s vyšším DPI se do nich nevejde kurzor ani text.

**Dynamický výpočet výšky**:
```cpp
int dpi = 96;
HDC hdc = GetDC(HWindow);
if (hdc) {
    dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(HWindow, hdc);
}
auto ScaleDpi = [dpi](int v) -> int { return MulDiv(v, dpi, 96); };

// Výšku Edit boxu sladit s výškou ComboBoxu:
RECT rcCombo = {0};
GetWindowRect(m_hCombo, &rcCombo);
int ctrlH = rcCombo.bottom - rcCombo.top;
if (ctrlH < ScaleDpi(26)) ctrlH = ScaleDpi(26);

// Nastavení pozice Edit boxu s přesnou výškou:
SetWindowPos(m_hEditFilter, NULL, x, ctrlY, ScaleDpi(170), ctrlH, SWP_NOZORDER);
```

---

### ⚠️ 5. Zamezení zobrazení prázdného okna při chybě otevření
Pokud selže otevření souboru/spojení (uzamčený soubor, výpadek sítě), nesmí prohlížeč zůstat viset na obrazovce jako prázdné šedé okno.

**V těle vlákna (`CViewerThread::Body`)**:
```cpp
if (!window->OpenFile(m_name, FALSE))
{
    // Chyba již byla uživateli zobrazena dialogem
    // Zničit okno a nevstupovat do message loopu:
    DestroyWindow(window->HWindow);
}
else
{
    ShowWindow(window->HWindow, SW_SHOWNORMAL);
    // standardní GetMessage smyčka...
}
```

---

## 5. Šablona `Makefile.mingw` (pro MinGW-w64)

```make
CROSS_COMPILE ?= 
CXX = $(CROSS_COMPILE)g++
CC = $(CROSS_COMPILE)gcc
RC = $(CROSS_COMPILE)windres

SALAMAND_DIR = ../salamander-plugins/salamand
SHARED_DIR = ../salamander-plugins/salamand/plugins/shared

CXXFLAGS = -O2 -Wall -Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-function -Wno-parentheses -std=c++17 \
    -DWIN32 -D_WIN32 -D_WINDOWS -D_USRDLL -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 \
    -D_CRT_SECURE_NO_WARNINGS -DUNICODE_NOT_USED -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/lang -I$(SHARED_DIR) -I$(SALAMAND_DIR)

LDFLAGS = -shared -s -static -static-libgcc -static-libstdc++ \
    -lshlwapi -lcomctl32 -lcomdlg32 -lole32 -lshell32 -lgdi32 -lmsimg32 -luxtheme -ldwmapi

CPP_SRCS = \
    src/gdrive.cpp \
    src/dialogs.cpp \
    src/menu.cpp \
    src/gdrivedarkmode.cpp \
    $(SHARED_DIR)/auxtools.cpp \
    $(SHARED_DIR)/dbg.cpp \
    $(SHARED_DIR)/mhandles.cpp \
    $(SHARED_DIR)/winliblt.cpp \
    $(SHARED_DIR)/plugindarkmode.cpp

CPP_OBJS = $(CPP_SRCS:.cpp=.o)
ALL_OBJS = $(CPP_OBJS) src/gdrive.res

all: gdrive.spl english.slg czech.slg

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/gdrive.res: src/gdrive.rc
	cd src && $(RC) -O coff -I. -Ilang -I$(SHARED_DIR) -I$(SALAMAND_DIR) -i gdrive.rc -o gdrive.res

gdrive.spl: $(ALL_OBJS) src/gdrive.def
	$(CXX) -shared src/gdrive.def $(ALL_OBJS) $(LDFLAGS) -o $@

english.slg: src/lang/lang_en.rc
	cd src/lang && $(RC) -O coff -I. -I.. -I$(SHARED_DIR) -I$(SALAMAND_DIR) -i lang_en.rc -o lang_en.res
	$(CXX) -shared -nostdlib -Wl,-e,0 src/lang/lang_en.res -o $@

czech.slg: src/lang/lang_cs.rc
	cd src/lang && $(RC) -O coff -I. -I.. -I$(SHARED_DIR) -I$(SALAMAND_DIR) -i lang_cs.rc -o lang_cs.res
	$(CXX) -shared -nostdlib -Wl,-e,0 src/lang/lang_cs.res -o $@

clean:
	rm -f src/*.o src/lang/*.res src/*.res *.spl *.slg
```

---

## 6. Šablona `gdrive.def`

```def
LIBRARY "gdrive.spl"
EXPORTS
    SalamanderPluginEntry2 @2
```

---

## 7. Správa Git repozitáře a GitHub Releases

### Inicializace a propojení s GitHubem:
```powershell
# 1. Nastavení identity autora
git config user.name "Filip Novák"
git config user.email "filip.novak@gmail.com"

# 2. Inicializace a commit
git init -b main
git add .
git commit -m "Initial commit of Google Drive plugin for Open Salamander"

# 3. Vytvoření veřejného repozitáře na GitHubu
gh repo create fila73/salamander-gdrive-plugin --public --source=. --remote=origin --push
```

### Balení a publikování nového Release (např. v0.1):
```powershell
# 1. Kompilace čistého sestavení
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw

# 2. Příprava distribučního ZIP archivu
$stage = "release_stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path "$stage\lang" | Out-Null
Copy-Item "gdrive.spl" -Destination "$stage\gdrive.spl"
Copy-Item "english.slg" -Destination "$stage\lang\english.slg"
Copy-Item "czech.slg" -Destination "$stage\lang\czech.slg"

$zipName = "salamander-gdrive-plugin-v0.1.zip"
if (Test-Path $zipName) { Remove-Item -Force $zipName }
Compress-Archive -Path "$stage\*" -DestinationPath $zipName
Remove-Item -Recurse -Force $stage

# 3. Publikování release přes GitHub CLI
gh release create v0.1 $zipName --title "v0.1" --notes "Initial release of Google Drive plugin for Open Salamander"
```

---

## 8. Instalace pluginu uživatelem

1. Stáhnout `salamander-gdrive-plugin-v0.1.zip` z GitHub Releases.
2. Rozbalit obsah archivu do podadresáře pluginů v Salamanderu:
   `C:\Program Files\Open Salamander\plugins\gdrive\` (nebo adresář Salamandera).
3. Struktura v adresáři musí být:
   ```
   plugins/gdrive/gdrive.spl
   plugins/gdrive/lang/english.slg
   plugins/gdrive/lang/czech.slg
   ```
4. V menu Salamandera: **Plugins → Plugins Manager... → Add...** vybrat `gdrive.spl`.
