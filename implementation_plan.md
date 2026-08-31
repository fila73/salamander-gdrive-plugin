# Google Drive Plugin — Stav implementace a Roadmapa

Tento dokument shrnuje stav implementace jednotlivých funkcí **Google Drive API v3** v pluginu pro Open Salamander a plán budoucích rozšíření.

---

## 🟢 1. Dokončené funkce (Realizováno a otestováno)

### 1.1 Zápisové a souborové operace
- [x] **F7 – Vytvoření složky (`CreateDir`)**: `POST https://www.googleapis.com/drive/v3/files?supportsAllDrives=true`
- [x] **Shift+F6 – Rychlé přejmenování (`QuickRename`)**: `PATCH https://www.googleapis.com/drive/v3/files/{id}?supportsAllDrives=true`
- [x] **F8 – Mazání položek (`Delete`)**:
  - Standardní F8: Přesun do Koše Google Disku (`PATCH` s `{"trashed": true}`).
  - Shift+F8: Trvalé smazání položky (`DELETE`).
- [x] **F5 – Nahrávání z disku na Google Disk (`CopyOrMoveFromDiskToFS`)**:
  - Streamovaný multipart upload (256 KB buffer) pro soubory i rekurzivní adresářové stromy.
- [x] **F5 – Stahování z Google Disku na lokální disk**:
  - Podpora jednotlivých souborů i celých rekurzivních složek.
- [x] **Ukazatel volného místa na disku (`GetFSFreeSpace`)**:
  - Zobrazení volné a celkové kapacity Google Disku v patičce panelu.

### 1.2 Virtuální pohledy (Virtual Folders) & Kontextové menu
- [x] **`/Starred`** (S hvězdičkou / Oblíbené)
- [x] **`/Recent`** (Poslední / Nedávné soubory)
- [x] **`/Trash`** (Koš s možností obnovení souborů i vysypání celého koše)
- [x] **Kontextové menu**:
  - *Otevřít ve webovém prohlížeči* (`webViewLink`)
  - *Kopírovat odkaz do schránky*
  - *Přidat / Odebrat hvězdičku*
  - *Obnovit z koše* / *Vysypat koš...*
  - *Spočítat velikost složky...* (`CCalcSizeProgressDialog` s možností přerušení klávesou Esc)

### 1.3 Inteligentní mezipaměť (Smart Cache) & Changes API
- [x] **Okamžité procházení (0 ms latence)**: Obsah složek v RAM mezipaměti.
- [x] **Detekce změn pomocí Changes API (`changes.list`)**:
  - Periodická kontrola podle nastavitelného intervalu (TTL).
  - Selektivní zneplatnění pouze změněných složek namísto plošného stahování celých stromů.
- [x] **Okamžité lokální mutace**: Zápisové operace v Salamanderu okamžitě aktualizují cache.
- [x] **Perzistentní disková mezipaměť**:
  - Ukládání mezipaměti na disk do `%APPDATA%\Open Salamander\plugins\gdrive\cache_<email_hash>.bin`.
  - Automatické uložení při ukončení a načtení při startu Salamandera.

### 1.4 Správa více účtů (Multi-Account)
- [x] **Registr Google účtů**: Možnost přihlásit více Google účtů současně (např. osobní i firemní).
- [x] **100% oddělení dat**: Každý účet má vlastní nezávislou diskovou i paměťovou cache.
- [x] **Přepínání účtů**: Možnost okamžitého přepnutí aktivního účtu v dialogu Konfigurace.
- [x] **Šifrování tokenů**: Refresh tokeny chráněny pomocí Windows DPAPI.

### 1.5 Konfigurační dialog (Property Sheet)
- [x] **Záložka „Účty a obecné“**: Správa přihlášených účtů (Přidat, Aktivovat, Odebrat), volba zobrazení Sdílených disků, vlastní OAuth klíče.
- [x] **Záložka „Mezipaměť a synchronizace“**: Zapnutí/vypnutí cache, nastavení TTL intervalu kontroly změn (10 s, 30 s, 1 min, 5 min, ručně), tlačítko pro vymazání cache.

### 1.6 Duplicity, řešení kolizí a modularizace (v0.3)
- [x] **Předběžný průchod (Pass 1 Pre-scan)**:
  - Předběžná kalkulace celkového počtu souborů a velikosti v bajtech před zahájením přenosu (pro upload i download).
  - Dva progress bary (jeden pro aktuální soubor a druhý pro celou dávku přenosu včetně rychlosti).
- [x] **Disambiguace duplicitních názvů položek**:
  - Detekce stejnojmenných souborů a složek v rámci jedné úrovně.
  - Přiřazení suffixu s posledními 6 znaky Google Drive ID (`[suffix]`) do zobrazení v panelu.
  - Deterministické mapování cest i přímé vyhledávání podle ID v `ResolveFolderIdForPath` a `FindItemByPanelName`.
- [x] **Detekce kolizí existujících složek při uploadu**:
  - Interaktivní dialog při existenci stejnojmenné složky na Google Disku (Sloučit/Přepsat, Ponechat oba, Přeskočit, Použít pro všechny zbývající).
- [x] **Okamžitá aktualizace panelu po přejmenování a vytvoření složky**:
  - Správné ošetření `mode == 1` v `QuickRename` a `CreateDir` a volání `PostRefreshPanelFS` i `RefreshPanelPath`.
- [x] **Modularizace CPluginFS (CR-08)**:
  - Rozdělení monolitického `gdrive_fs.cpp` na `gdrive_fs_nav.cpp`, `gdrive_fs_transfer.cpp`, `gdrive_fs_ops.cpp` a `gdrive_fs.cpp`.
- [x] **Vlastní sloupce panelu (Owner, Shared, Starred)**:
  - Implementace rozhraní `CPluginDataInterfaceAbstract` a `CSalamanderViewAbstract` pro zobrazení sloupců Vlastník, Sdíleno a Hvězdička.

### 1.7 Cloudové vyhledávání a prohlížeč souborů (v0.4)
- [x] **Cloudové a fulltextové vyhledávání (`Alt+F7` / `OpenFindDialog`)**:
  - Implementace `FS_SERVICE_OPENFINDDLG` a asynchronního modeless dialogu `CGDriveFindDialog` s věrným rozložením okna Find ze Salamandera.
  - **Hledání podle názvu**: `name contains '...'` s podporou zástupných znaků (`*`, `?`).
  - **Fulltextové prohledávání obsahu**: `fullText contains '...'` uvnitř Docs, Sheets, Slides, PDF, textů i OCR obrázků.
  - **Look In**: výběr rozsahu hledání – celý disk, aktuální složka, Shared with me, Starred, Trash.
  - **Pokročilé filtry**: typ souboru (dokumenty, tabulky, obrázky...), pouze soubory/složky.
  - **Perzistence nastavení**: všechny checkboxy, seznam historií vyhledávání a Look In se ukládají do registru.
  - **Výsledky v ListView**: sloupce Název, Cesta, Velikost, Datum, Čas, Vlastník; podpora řazení.
  - **Tmavý režim (Dark Mode)**: plné přizpůsobení schématu Salamandera.
  - Asynchronní vlákno na pozadí s atomickým přerušením klávesou `Esc`.
- [x] **Funkce Focus (přechod na nalezenou položku)**:
  - Dvojklik nebo `Enter` přepne panel Salamandera přímo do složky souboru a označí ho.
  - Opravena podpora pro soubory ve *Sdíleno se mnou* a složky s českou diakritikou (UTF-8 vs. ANSI/CP1250).
  - `SearchWorker` při dohledávání cest okamžitě cachuje `folderId → path` do globální mapy, takže Focus nemusí znovu traversovat strom z API.
- [x] **View (F3) z výsledků vyhledávání**:
  - Soubor se otevře v interním prohlížeči Salamandera (`SalamanderGeneral->ViewFileInPluginViewer`) s `useCache = TRUE`.
  - Google Dokumenty jsou exportovány do standardního formátu (`.docx`, `.xlsx`, `.pptx`, `.pdf`).
- [x] **Klávesové zkratky v dialogu**:
  - `Enter` v libovolném editboxu/comboboxu spouští hledání (`DM_SETDEFID` + subclassing přes `EnumChildWindows`).
  - `Esc` zastavuje aktivní hledání (první stisk) nebo zavírá dialog (druhý stisk).
  - `Enter` v seznamu výsledků volá Focus.
- [x] **Nemodální okno bez Always-on-Top**:
  - `CreateDialogParam` s `hWndParent = NULL` (ne handle Salamandera) → dialog není *owned window* a nepřekrývá Salamander.

---

## 🟡 2. Budoucí rozšíření (Roadmapa)

### 2.1 Historie verzí (Revisions API)
- **API endpoint:** `GET https://www.googleapis.com/drive/v3/files/{fileId}/revisions`
- **Funkcionalita:**
  - Dialog „Historie verzí...“ v kontextovém menu souboru.
  - Zobrazení data úpravy, autora, možnost stažení starší verze nebo její obnovení.

### 2.3 Správa oprávnění ke sdílení (Permissions Dialog)
- **API endpointy:** `GET/POST/DELETE https://www.googleapis.com/drive/v3/files/{fileId}/permissions`
- **Funkcionalita:**
  - Dialog pro zobrazení a úpravu sdílení položky (přidání spolupracovníků, nastavení role: čtenář / komentátor / editor).

### 2.4 Miniatury v panelu (Thumbnails / ThumbLoader)
- **API vlastnost:** `thumbnailLink` v metadatovém objektu souboru.
- **Salamander rozhraní:** `CPluginInterfaceForThumbLoaderAbstract`
- **Funkcionalita:**
  - Zobrazování náhledů obrázků, PDF a Google Dokumentů přímo v režimu miniatur panelu Salamandera.
