# Google Drive API v3 — Možnosti a plán dalších funkcí

Tento dokument shrnuje funkce **Google Drive API v3**, které zatím v pluginu pro Open Salamander nejsou implementovány, rozdělené podle priorit a technické náročnosti.

---

## 1. Souborové operace zápisu (Zápis a úpravy na disku)

Aktuální verze pluginu je pouze pro čtení (Download, View, Calc Size). Google Drive API podporuje plné CRUD operace:

### 1.1 Nahrávání na Google Disk (Upload / F5 Copy do pluginu)
- **API endpoint:** `POST https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable` nebo `multipart`
- **Salamander rozhraní:** `CPluginFS::CopyOrMoveFromDiskToFS`
- **Funkcionalita:**
  - Nahrávání jednotlivých souborů i celých adresářových stromů z lokálního disku do Google Disku.
  - Podpora *Resumable Upload* pro velké soubory (> 5 MB) s možností navázání při výpadku sítě a live progress dialogem.
  - Řešení kolizí (Přepsat / Přeskočit / Přejmenovat).

### 1.2 Vytváření nových složek (F7 Create Directory)
- **API endpoint:** `POST https://www.googleapis.com/drive/v3/files` s tělem `{"name": "...", "mimeType": "application/vnd.google-apps.folder", "parents": ["<parent_id>"]}`
- **Salamander rozhraní:** `CPluginFS::CreateDir`
- **Funkcionalita:** Vytvoření nové podsložky v aktuální složce na disku (včetně Můj disk i Sdílené disky).

### 1.3 Mazání souborů a složek (F8 / Delete)
- **API endpoint:**
  - *Do koše (Trash)*: `PATCH https://www.googleapis.com/drive/v3/files/{fileId}` s `{"trashed": true}`
  - *Trvalé smazání*: `DELETE https://www.googleapis.com/drive/v3/files/{fileId}`
- **Salamander rozhraní:** `CPluginFS::Delete`
- **Funkcionalita:** Bezpečné přesunutí vybraných souborů/složek do Koše Google Disku, případně volba trvalého smazání s klávesou Shift.

### 1.4 Přejmenování a přesun (F6 Move / Shift+F6 Quick Rename)
- **API endpoint:** `PATCH https://www.googleapis.com/drive/v3/files/{fileId}` s `{"name": "novy_nazev"}` nebo `addParents=...&removeParents=...`
- **Salamander rozhraní:** `CPluginFS::QuickRename`, `CPluginFS::CopyOrMoveFromFS` (při přesunu v rámci stejného FS)
- **Funkcionalita:** Rychlé přejmenování na místě i přesun souborů mezi složkami bez nutnosti stahování a opětovného nahrávání.

---

## 2. Speciální virtuální pohledy (Virtual Folders)

Google Drive API umožňuje filtrovat položky pomocí bohatých dotazů (`q` parametr):

### 2.1 S hvězdičkou / Oblíbené (`Starred`)
- **API dotaz:** `starred = true and trashed = false`
- **Umístění:** Virtuální uzel v kořeni `/Starred` (česky *S hvězdičkou*).
- **Funkcionalita:** Rychlý přístup ke klíčovým dokumentům a složkám označeným hvězdičkou.
- **Kontextové menu:** Možnost položce hvězdičku přidat / odebrat (`PATCH files/{id}` s `{"starred": true/false}`).

### 2.2 Koš (`Trash`)
- **API dotaz:** `trashed = true`
- **Umístění:** Virtuální uzel `/Trash` (česky *Koš*).
- **Funkcionalita:** 
  - Procházení smazaných položek.
  - Obnovení souborů z koše (`{"trashed": false}`).
  - Příkaz „Vysypat koš“ (`DELETE https://www.googleapis.com/drive/v3/files/trash`).

### 2.3 Poslední / Nedávné (`Recent`)
- **API dotaz:** `trashed = false` se řazením `orderBy=viewedByMeTime desc` nebo `modifiedTime desc` (omezeno např. na 100 položek).
- **Umístění:** Virtuální uzel `/Recent`.

---

## 3. Hledání na Google Disku (Alt+F7 / Find Files)

- **API endpoint:** `GET https://www.googleapis.com/drive/v3/files?q=...`
- **Salamander rozhraní:** `CPluginFS::OpenFindDialog` nebo integrace do standardního vyhledávání.
- **Možnosti dotazů:**
  - Hledání podle názvu: `name contains 'rozpocet'`
  - **Fulltextové vyhledávání v obsahu**: `fullText contains 'smlouva'` (Google indexuje text uvnitř Docs, Sheets, PDF, TXT i obrázků s OCR).
  - Filtrování podle typu (dokumenty, tabulky, obrázky, složky).
  - Filtrování podle data změny / autora.

---

## 4. Sdílení a webová integrace (Permissions & Web Links)

### 4.1 Kontextové menu – Akce Google Disku
- **Otevřít v prohlížeči (Open in Browser):**
  - Využije se atribut `webViewLink` z metadat souboru. Otevře soubor přímo v Google Docs/Sheets v defaultním browseru.
- **Kopírovat odkaz pro sdílení (Copy shareable link):**
  - Zkopíruje webový odkaz na soubor do schránky Windows.
- **Správa oprávnění (Share / Permissions Dialog):**
  - **API endpointy:** `GET/POST/DELETE https://www.googleapis.com/drive/v3/files/{fileId}/permissions`
  - Vlastní dialog v pluginu zobrazující, kdo má k souboru přístup (čtenář, komentátor, editor) s možností přidat e-mail nového spolupracovníka.

---

## 5. Historie verzí (Revisions API)

- **API endpoint:** `GET https://www.googleapis.com/drive/v3/files/{fileId}/revisions`
- **Funkcionalita:**
  - Google Drive uchovává předchozí verze souborů (zejména binárních a kancelářských).
  - Dialog „Historie verzí...“ v kontextovém menu souboru.
  - Možnost zobrazit datum, autora úpravy, stáhnout kteroukoli starší verzi nebo obnovit starší verzi jako aktuální.

---

## 6. Miniatury a náhledy (Thumbnails / ThumbLoader)

- **API vlastnost:** `thumbnailLink` v metadatovém objektu souboru.
- **Salamander rozhraní:** `CPluginInterfaceForThumbLoaderAbstract`
- **Funkcionalita:**
  - Zobrazování náhledů obrázků, PDF a Google Dokumentů přímo v režimu miniatur panelu Salamandera bez nutnosti stahovat celý originální soubor.

---

## 7. Zkratky (Shortcuts API)

- **MIME typ:** `application/vnd.google-apps.shortcut`
- **API vlastnost:** `shortcutDetails.targetId`, `shortcutDetails.targetMimeType`
- **Funkcionalita:**
  - Google Drive podporuje zástupce (shortcuts) na jiné soubory a složky.
  - Plugin může zástupce transparentně otevírat nebo navigovat na cílovou složku.

---

## 8. Změny a synchronizace (Changes API)

- **API endpoint:** `GET https://www.googleapis.com/drive/v3/changes` (se `startPageToken`)
- **Funkcionalita:**
  - Umožňuje zjišťovat přírůstkové změny bez nutnosti znovu procházet celé složky (efektivní cache invalidace a notifikace o změnách).

---

## Doporučený plán implementace po fázích

| Fáze | Zaměření | Klíčové funkce |
|---|---|---|
| **Fáze 1** | **Zápis a správa souborů** | F7 Vytvořit složku, F8 Smazat (do Koše), Shift+F6 Přejmenovat, F5 Upload souborů |
| **Fáze 2** | **Virtuální složky a web integrace** | `/Starred`, `/Trash` (včetně vysypání/obnovení), kontextové menu "Otevřít na webu" |
| **Fáze 3** | **Vyhledávání a miniatury** | Vyhledávací dialog s fulltextem, ThumbLoader pro náhledy v panelech |
| **Fáze 4** | **Pokročilá správa** | Správa oprávnění (sdílení), Historie verzí (Revisions) |
