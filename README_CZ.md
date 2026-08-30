# Plugin Google Disk pro Open Salamander

Plnohodnotný souborový plugin pro správce souborů **Open Salamander** (a Dark Mode fork **KRtkovo-eu-AI/salamander**), který umožňuje procházet, spravovat, nahrávat, stahovat a organizovat soubory a složky na **Google Disku** přímo ze dvou panelů Salamandera s bleskovou inteligentní mezipamětí.

---

## ✨ Klíčové funkce

### 🚀 Inteligentní paměťová a trvalá disková mezipaměť (Cache)
- **Okamžité procházení (0 ms latence)**: Obsah navštívených složek je uložen v paměti RAM a ukládán na disk do `%APPDATA%\Open Salamander\plugins\gdrive\cache_<email_hash>.bin`.
- **Synchronizace přes Google Drive Changes API (`changes.list`)**:
  - Periodická lehká kontrola změn pomocí synchronizačního tokenu podle nastavitelného intervalu (10 s, 30 s, 1 min, 5 min nebo ručně přes `Ctrl+R`).
  - Selektivní zneplatnění pouze těch složek, kde došlo ke změně, namísto stahování celých stromů.
  - Nulové zbytečné síťové dotazy, pokud se na vzdáleném disku nic nezměnilo.

### 👥 Správa více účtů (Multi-Account) a přepínání
- Možnost mít přihlášeno **více Google účtů současně** (např. osobní a firemní).
- **100% oddělení dat**: Každý účet má vlastní nezávislou diskovou i paměťovou cache.
- Snadné přepínání aktivního účtu, přidávání i odebírání přímo v dialogu Konfigurace.
- **Šifrované uložení tokenů (Windows DPAPI)**: Bezpečné šifrování v rámci uživatelského profilu Windows.
- **Tichá obnova (Silent Refresh)**: Přístupové tokeny se na pozadí automaticky obnovují bez otevírání prohlížeče.

### ✏️ Zápisové a souborové operace
- 📁 **F7 – Vytvoření složky (`CreateDir`)**: Vytváření nových složek v *Můj disk*, *Sdílených discích* i podsložkách s okamžitou aktualizací panelu.
- ✏️ **Shift+F6 – Rychlé přejmenování (`QuickRename`)**: Okamžité přejmenování souborů a složek přímo v panelu s automatickým refreshem a zaostřením.
- 🗑️ **F8 – Mazání a přesun do Koše (`Delete`)**:
  - Standardní stisk `F8`: Přesun položky do Koše Google Disku.
  - Stisk `Shift+F8`: Trvalé smazání z Google Disku.
- 📤 **F5 – Nahrávání na Google Disk (`CopyOrMoveFromDiskToFS`)**:
  - Streamovaný multipart upload (256 KB buffer) s podporou jednotlivých souborů i rekurzivních stromů složek.
  - **Předběžný průchod (Pass 1) & dva progress bary**: Předem spočítá přesný počet i objem dat v bajtech a zobrazuje jak průběh aktuálního souboru, tak celkový přenos včetně rychlosti.
  - **Detekce kolizí existujících složek i souborů**: Interaktivní dialog s možnostmi *Sloučit / Přepsat*, *Ponechat oba*, *Přeskočit* a *Použít pro všechny zbývající*.
- 📥 **F5 – Stahování na lokální disk**:
  - Rychlé stahování se stavovým dialogem průběhu a rekurzivní předkalkulací velikosti složek.
- 🔀 **Disambiguace duplicitních názvů**:
  - Automatická detekce stejnojmenných položek v jedné složce a přidání jednoznačného suffixu s posledními 6 znaky Google Drive ID (`Složka [mK9xQ2]`, `Dokument [mK9xQ2].pdf`) pro 100% spolehlivou navigaci i položkové operace.
- 👁️ **F3 – Interní prohlížeč**:
  - Okamžitý náhled vzdálených souborů s automatickou mezipamětí.
- 📊 **Ukazatel volného místa na disku**:
  - Zobrazení volné a celkové kapacity Google Disku v patičce panelu Salamandera.
- 📋 **Vlastní sloupce v podrobném pohledu (`CSalamanderViewAbstract`)**:
  - **`Vlastník`**: Zobrazuje jméno nebo e-mail tvůrce položky (klíčové ve Sdílených discích a složce *Sdíleno se mnou*).
  - **`Sdíleno`**: Indikace, zda je položka sdílena s dalšími uživateli (`Ano` / `Ne`).
  - **`Hvězdička`**: Vizuální označení `*` pro oblíbené položky.
  - Plná podpora řazení podle sloupců a interaktivní změny šířky myší s perzistentním ukládáním.
- 🔎 **Cloudové a fulltextové vyhledávání (`Alt+F7` / `FS_SERVICE_OPENFINDDLG`)**:
  - Rozhraní dialogu přebírá přesnou podobu a ovládání okna Find ze Salamandera.
  - **Hledání podle názvu**: `name contains '...'` s podporou zástupných znaků.
  - **Fulltextové prohledávání obsahu**: `fullText contains '...'` uvnitř Docs, Sheets, Slides, PDF, textů i OCR obrázků.
  - **Přejít do složky (Focus)**: Dvojklik nebo `Enter` okamžitě přepne panel Salamandera do složky souboru a vybere danou položku.
  - Asynchronní vlákno na pozadí, okamžité přerušení klávesou `Esc` a plná podpora Dark Mode.

### ⭐ Virtuální složky a kontextové menu
- **Virtuální pohledy**:
  - `/Starred` (S hvězdičkou / Oblíbené)
  - `/Recent` (Nedávné soubory)
  - `/Trash` (Koš s možností obnovení a vysypání)
- **Bohaté kontextové menu**:
  - *Otevřít ve webovém prohlížeči*
  - *Kopírovat odkaz do schránky*
  - *Přidat / Odebrat hvězdičku*
  - *Obnovit z koše* / *Vysypat koš...*
  - *Spočítat velikost složky...* (Rekurzivní skener s možností přerušení klávesou Esc)

### 📄 Automatický export Google Dokumentů
- Automatický převod formátů Google Dokumenty, Tabulky a Prezentace do standardních formátů (`.docx`, `.xlsx`, `.pptx`, `.pdf`) při stahování nebo náhledu.

### 🌙 Plná podpora Dark Mode
- Tmavý režim plně ladící s barevným schématem tmavého motivu Open Salamandera.

---

## 🚀 Instalace

1. Stáhněte nejnovější archiv `salamander-gdrive-plugin-v0.3.zip` ze sekce [GitHub Releases](https://github.com/fila73/salamander-gdrive-plugin/releases).
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
5. V menu **Plugins → Google Disk → Nastavení...** přidejte svůj Google účet.

---

## ⚙️ Vytvoření přístupových údajů v Google Cloud Console

Pro připojení k vašemu Google Disku si v [Google Cloud Console](https://console.cloud.google.com/) vytvořte bezplatné OAuth klíče typu Desktop:

1. V Google Cloud Console vytvořte nový projekt.
2. V sekci **APIs & Services → Library** povolte **Google Drive API**.
3. Nastavte **OAuth Consent Screen** (Typ uživatele: *External*, přidejte svůj e-mail jako *Test User*).
4. Přejděte do **Credentials → Create Credentials → OAuth Client ID**:
   - Typ aplikace: **Desktop App**.
5. Zkopírujte vygenerované **Client ID** a **Client Secret**.
6. V Open Salamanderu otevřete **Plugins → Google Disk → Nastavení...**, vložte oba klíče a klikněte na **Přidat účet...**.

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

- Autor: **fila73**
- Inspirováno projektem **Red Salamander**
- Integrace Dark Mode podle forku **Ondrej Kotas (KRtkovo-eu-AI)**.
