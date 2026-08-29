# Plugin Google Disk pro Open Salamander

Plnohodnotný souborový plugin pro správce souborů **Open Salamander** (a Dark Mode fork **KRtkovo-eu-AI/salamander**), který umožňuje procházet, prohlížet, zjišťovat velikosti a stahovat soubory a složky z **Google Disku** (včetně *Můj disk* a *Sdílených disků*) přímo ze dvou panelů Salamandera.

---

## ✨ Klíčové funkce

- 🔒 **Bezpečné přihlášení (OAuth 2.0 s PKCE)**:
  - Přihlášení přes výchozí webový prohlížeč bez nutnosti zadávat hesla do aplikace.
  - Možnost zadat vlastní **OAuth Client ID** a **OAuth Client Secret** přímo v konfiguračním dialogu pluginu.
- 🔑 **Šifrované uložení tokenů (Windows DPAPI)**:
  - Refresh token je bezpečně zašifrován v rámci uživatelského profilu Windows.
  - **Tichá obnova (Silent Refresh)**: Přístupové tokeny se na pozadí automaticky obnovují bez opakovaného otevírání prohlížeče.
- 📁 **Kompletní podpora Google Disku**:
  - **Můj disk (My Drive)**: Osobní úložiště Google Disku.
  - **Sdílené disky (Shared Drives)**: Týmové disky Google Workspace.
  - **Sdíleno se mnou (Shared with me)**: Přístup ke složkám a souborům sdíleným s vaším účtem.
- ⚡ **Výpočet velikosti složek (Kontextové menu / Hlavní menu)**:
  - Rekurzivní skenování velikosti a počtu položek s živým dialogem průběhu.
  - Možnost okamžitého přerušení (tlačítko **Zrušit** nebo klávesa `Esc`) se zobrazením částečných statistik.
  - Automatické promítnutí výsledné velikosti do panelu Salamandera – **nahradí `DIR` / `ADR` ve sloupci Velikost**.
- 📥 **Kopírování a stahování (F5)**:
  - Stahování jednotlivých souborů i celých stromů složek na lokální disk.
- 👁️ **Interní prohlížeč (F3)**:
  - Rychlé prohlížení souborů s automatickou mezipamětí.
- 📄 **Export Google Dokumentů**:
  - Automatický převod formátů Google Dokumenty, Tabulky a Prezentace do standardních formátů (`.docx`, `.xlsx`, `.pptx`, `.pdf`).
- 🌙 **Plná podpora Dark Mode**:
  - Tmavý režim plně ladící s barevným schématem tmavého motivu Open Salamandera.

---

## 🚀 Instalace

1. Stáhněte nejnovější archiv `salamander-gdrive-plugin-v1.0.zip` ze sekce GitHub Releases.
2. Rozbalte soubory do adresáře `plugins\gdrive` v instalaci Open Salamandera:
   ```
   Open Salamander/
   └── plugins/
       └── gdrive/
           ├── gdrive.spl
           └── lang/
               ├── english.slg
               └── czech.slg
   ```
3. Spusťte **Open Salamander**, přejděte do **Plugins → Plugins Manager...**, klikněte na **Add...** a vyberte `gdrive.spl`.
4. V panelu stiskněte **Alt+F1** nebo **Alt+F2** a zvolte disk **gdrive:**.
5. V menu **Plugins → Google Disk → Nastavení...** zadejte své přihlašovací údaje (Client ID a Client Secret).

---

## ⚙️ Vytvoření přístupových údajů v Google Cloud Console

Pro připojení k vašemu Google Disku si v [Google Cloud Console](https://console.cloud.google.com/) vytvořte bezplatné OAuth klíče typu Desktop:

1. V Google Cloud Console vytvořte nový projekt.
2. V sekci **APIs & Services → Library** povolte **Google Drive API**.
3. Nastavte **OAuth Consent Screen** (Typ uživatele: *External*, přidejte svůj e-mail jako *Test User*).
4. Přejděte do **Credentials → Create Credentials → OAuth Client ID**:
   - Typ aplikace: **Desktop App**.
5. Zkopírujte vygenerované **Client ID** a **Client Secret**.
6. V Open Salamanderu otevřete **Plugins → Google Disk → Nastavení...**, vložte oba klíče a klikněte na **Přihlásit se**.

---

## 🛠️ Kompilace ze zdrojových kódů (MinGW-w64)

### Požadavky:
- MinGW-w64 (GCC 10+ s podporou C++17)
- `make` (`mingw32-make`)
- Open Salamander SDK (v adresáři `../salamander-plugins/salamand`)

### Příkaz pro kompilaci:
```powershell
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw
```

Příkaz vytvoří:
- `gdrive.spl` (Hlavní knihovna pluginu)
- `english.slg` (Anglické jazykové zdroje)
- `czech.slg` (České jazykové zdroje)

---

## 📄 Licence a autoři

Licencováno pod **GNU General Public License v2.0 nebo novější** ([GPL-2.0-or-later](LICENSE)).  
Všechny zdrojové soubory obsahují standardizované SPDX hlavičky.

- Portováno do Open Salamanderu: **fila73**.
- Integrace Dark Mode podle forku **Ondrej Kotas (KRtkovo-eu-AI)**.
