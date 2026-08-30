# Zpráva o realizaci a ponaučení (Jobs Done & Post-Mortem)

Tento dokument rekapituluje všechny realizované části pluginu **Google Disk pro Open Salamander**, popisuje technické výzvy a problémy, na které jsme při vývoji narazili, a shrnuje klíčová ponaučení pro další vývoj.

---

## 📋 1. Přehled realizovaných funkcí

| Oblast | Realizované funkce | Klíčové soubory |
|---|---|---|
| **Zápisové operace** | `F7` Vytvoření složky, `Shift+F6` Přejmenování, `F8` Přesun do koše, `Shift+F8` Trvalé smazání, `F5` Nahrávání na disk (jednotlivé soubory i rekurzivní stromy). | `src/gdrive_fs_ops.cpp`, `src/gdrive_fs_transfer.cpp`, `src/gdrive_api.cpp` |
| **Průběh a předkalkulace (Pass 1)** | Předběžný průchod před zahájením přenosu (přesný počet položek a celková velikost v bajtech), dva progress bary (aktuální soubor + celkový přenos s ukazatelem rychlosti). | `src/gdrive_fs_transfer.cpp`, `src/dialogs.cpp` |
| **Disambiguace duplicit** | Detekce stejnojmenných položek a přiřazení jednoznačného suffixu s ID (`[suffix]`) v panelu i cestách pro deterministickou navigaci a operace. | `src/gdrive_fs_nav.cpp`, `src/gdrive_fs_ops.cpp` |
| **Kolize při nahrávání** | Interaktivní detekce existujících souborů i složek na disku s možnostmi Sloučit/Přepsat, Ponechat oba, Přeskočit a Použít pro všechny. | `src/gdrive_fs_transfer.cpp`, `src/dialogs.cpp` |
| **Virtuální pohledy** | Virtuální složky `/Starred` (Oblíbené), `/Recent` (Nedávné), `/Trash` (Koš). | `src/gdrive_fs_nav.cpp`, `src/gdrive_api.cpp` |
| **Kontextové menu** | Otevřít ve webovém prohlížeči (`webViewLink`), Kopírovat odkaz, Přidat/Odebrat hvězdičku, Obnovit z koše, Vysypat koš, Spočítat velikost složky. | `src/gdrive_fs_ops.cpp`, `src/dialogs.cpp`, `src/menu.cpp` |
| **Kapacita disku** | Zobrazení volného a celkového místa v patičce panelu (`GetFSFreeSpace`). | `src/gdrive_fs.cpp` |
| **Inteligentní mezipaměť (RAM)** | 0ms latence při procházení, okamžité lokální mutace při úpravách. | `src/gdrive_cache.cpp`, `src/gdrive_cache.h` |
| **Changes API (`changes.list`)** | Detekce změn přes synchronizační token disku, selektivní zneplatnění pouze upravených složek bez zbytečných dotazů. | `src/gdrive_api.cpp`, `src/gdrive_cache.cpp` |
| **Perzistence na disk** | Ukládání mezipaměti do profilu uživatele (`%APPDATA%\...\cache_<hash>.bin`), automatické uložení při ukončení a načtení při startu. | `src/gdrive_cache.cpp` |
| **Multi-Account registr** | Možnost mít přihlášeno více Google účtů současně, 100% izolace cache souborů, správa v konfiguraci. | `src/gdrive_auth.cpp`, `src/gdrive_auth.h` |
| **Konfigurace (Property Sheet)** | Záložka „Účty a obecné“ (přepínání účtů, OAuth klíče) a záložka „Mezipaměť a synchronizace“ (TTL frekvence, vymazání cache). | `src/dialogs.cpp`, `src/lang/` |
| **Modularizace kódu (CR-08)** | Rozdělení monolitického `gdrive_fs.cpp` na navigaci (`gdrive_fs_nav.cpp`), přenosy (`gdrive_fs_transfer.cpp`), operace (`gdrive_fs_ops.cpp`) a jádro (`gdrive_fs.cpp`). | `src/gdrive_fs*.cpp` |
| **Dynamický Dark Mode** | Striktní respektování uživatelského nastavení schématu v Salamanderu s okamžitým přepínáním. | `src/gdrivedarkmode.cpp` |

---

## 🔍 2. Technické výzvy a jak jsme je vyřešili

### 2.1 Kompatibilita `std::fstream` a Unicode cest v MinGW-w64
- **Problém:** Konstruktor `std::ifstream(std::wstring, ...)` vyvolal při kompilaci v novějším GCC/MinGW chybu (`no matching function for call ... make_preferred`).
- **Příčina:** Rozhraní `std::basic_ifstream` v libstdc++ v některých verzích MinGW očekává buď `std::string` nebo `std::filesystem::path`, přičemž přímé předání `std::wstring` není standardně přetíženo.
- **Řešení:** Převod cesty `std::wstring` na UTF-8 úzký řetězec `std::string utf8Path = GDriveHttp::HttpClient::WideToUtf8(path)`. Tento formát je 100% přenositelný a kompilovatelný napříč všemi verzemi C++ a kompilátory.

### 2.2 Google Drive Changes API – mapování změn na složky
- **Problém:** Endpoint `changes.list` vrací změněné soubory, ale uživatel v Salamanderu prochází strom složek.
- **Příčina:** Pokud někdo smaže nebo přejmenuje soubor, je potřeba zneplatnit mezipaměť jeho *nadřazené složky* (`parents`), nikoliv pouze souboru samotného. U smazaných souborů (`removed = true`) navíc nemusí být pole `parents` vždy kompletní.
- **Řešení:** V parseru `ApiClient::GetChanges` iterujeme jak přes `fileId` (pro případ, že se změnil stav samotné složky), tak přes všechny identifikátory v poli `parents[]`. Pokud dojde k jakékoliv změně, automaticky zneplatňujeme i virtuální uzly (`/Starred`, `/Recent`, `/Trash`).

### 2.3 Izolace dat při přepínání mezi Google účty
- **Problém:** Riziko smíchání souborů a tokenů v paměti a na disku při práci s více účty (např. pracovní a soukromý disk).
- **Příčina:** Statická mezipaměť nebo sdílený soubor cache by mohl zobrazit strukturu jiného uživatele nebo poslat dotaz s neplatným `startPageToken`.
- **Řešení:**
  1. Zavedení oddělených diskových souborů pojmenovaných podle hashe/sanitizovaného e-mailu (`cache_<email_hash>.bin`).
  2. Implementace atomického `SwitchAccount(email)` v `CacheManageru`, který nejprve uloží aktuální stav, vyčistí paměť RAM a teprve poté načte data nového účtu.
  3. Ukládání přihlašovacích údajů v registru do hierarchie `HKCU\...\Plugins\gdrive\Accounts\<email_key>`.

### 2.4 Zpracování textových a binárních dat v Resource kompilátoru (`windres`)
- **Problém:** Při rozšiřování konfiguračních dialogů v `lang_en.rc` a `lang_cs.rc` došlo k duplicitnímu ukončovacímu bloku `END` a kolizi ID ovládacích prvků.
- **Řešení:** Udržování oddělených rozsahů ID v `lang.rh` (520+ pro novou záložku cache) a `gdrive.rh2` (2150+ pro nové lokalizační řetězce). Striktní kontrola syntaxe RC souborů před spuštěním `windres`.

### 2.5 Propustnost nahrávání velkých souborů
- **Problém:** Výchozí WinHTTP buffer (64 KB) způsoboval při nahrávání na linkách s vyšší latencí zbytečné režijní zpoždění.
- **Řešení:** Zvýšení streamovacího bufferu na **256 KB** v `HttpClient::UploadMultipartFile`, což výrazně zrychlilo odesílání dat.

### 2.6 Duplicitní názvy na Google Disku a deterministická navigace
- **Problém:** Google Drive umožňuje mít v jedné složce více souborů či podsložek se zcela identickým názvem. Open Salamander však pracuje s jednoznačnými textovými cestami (`gdrive:\My Drive\Složka\Sub`).
- **Příčina:** Při stejných názvech nebylo možné určit, do které podsložky chce uživatel vstoupit, a operace vždy cílily pouze na první nalezenou instanci.
- **Řešení:**
  1. Výpočet zobrazovaných jmen v `ComputeDisplayNames` – duplicity automaticky obdrží suffix s posledními 6 znaky ID (`Složka [mK9xQ2]`, `Dokument [mK9xQ2].pdf`).
  2. V `ResolveFolderIdForPath` se z cesty detekuje suffix `[id]` a provede se přesné spárování s `item.id.endsWith(suffix)`.
  3. Vyhledávání položek v panelu přes `FindItemByPanelName` cílí na přesnou instanci i při přejmenování, mazání či otevírání vlastností.

### 2.7 Detekce a řešení kolizí existujících složek při uploadu
- **Problém:** Při kopírování lokálního adresářového stromu na Google Disk docházelo k vytváření duplicitních složek namísto sloučení obsahu.
- **Řešení:** Implementace Pass 1 předkalkulace lokálních souborů a interaktivního dialogu `COverwriteConflictDialog` pro složky (Sloučit/Přepsat, Ponechat oba, Přeskočit, Použít pro všechny).

### 2.8 Životní cyklus a okamžitý refresh panelu
- **Problém:** Po přejmenování (F2) nebo vytvoření složky (F7) zůstával panel neaktualizovaný.
- **Příčina:** Salamander FS metody `QuickRename` a `CreateDir` nejprve volají plugin s parametrem `mode == 1`. Pokud plugin nevrátí `FALSE`, Salamander neotevře svůj standardní editační řádek. Navíc po dokončení operace (`mode == 2`) je nutné explicitně zavolat `SalamanderGeneral->PostRefreshPanelFS(this)` a `RefreshPanelPath`.
- **Řešení:** Doplnění korektního zpracování `mode == 1` a volání refreshe po úspěšném provedení operace.

---

## 💡 3. Klíčová ponaučení pro budoucí vývoj

1. **Vždy ověřovat signatury a sémantiku v SDK:**
   - Předpoklad, že Salamander provede refresh panelu automaticky po vrácení `TRUE`, byl mylný. Sémantika Salamander FS rozhraní vyžaduje explicitní `PostRefreshPanelFS`.

2. **Oddělení mezipaměti podle identit již od návrhu:**
   - Návrh mezipaměti od začátku počítající s parametrem `accountKey` předchází složitému refaktorování při přidávání multi-account podpory.

3. **Modulární architektura předchází monolitům:**
   - Rozdělení rozsáhlé FS třídy na specializované moduly (navigace, přenosy, položkové operace) výrazně zlepšuje udržovatelnost a přehlednost kódu.

4. **Inkrementální commity po logických celcích:**
   - Každá fáze byla samostatně zkompilována, zkontrolována na závislosti přes `objdump` a commitnuta.

5. **Nulové externí závislosti:**
   - Důsledné dodržování pravidla `-static -static-libgcc -static-libstdc++` zaručuje, že vygenerovaný `gdrive.spl` funguje na jakémkoliv Windows systému bez nutnosti instalovat další runtime knihovny.

