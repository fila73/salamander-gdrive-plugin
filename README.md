# Google Drive Plugin for Open Salamander

A full-featured filesystem, management, and viewer plugin for **Open Salamander** (and the Dark Mode fork **KRtkovo-eu-AI/salamander**), allowing you to browse, manage, upload, download, and organize files and folders on **Google Drive** directly within Salamander's dual-pane interface with zero-latency caching.

---

## ✨ Features

### 🚀 Smart In-Memory & Persistent Disk Metadata Cache
- **Instant 0 ms Browsing**: Folder contents are cached in RAM and persisted across Salamander restarts to `%APPDATA%\Open Salamander\plugins\gdrive\cache_<email_hash>.bin`.
- **Google Drive Changes API Synchronization (`changes.list`)**:
  - Automatically queries changes using a lightweight page token based on a configurable TTL interval (10s, 30s, 1m, 5m, or Manual via `Ctrl+R`).
  - Selectively invalidates only modified folders instead of re-downloading whole trees.
  - Zero unnecessary network requests if nothing changed on remote storage.

### 👥 Multi-Account Registry & Seamless Switching
- Sign in to **multiple Google accounts** simultaneously (e.g. personal and work).
- **100% Data Isolation**: Each account maintains its own isolated disk cache file.
- Switch active accounts or add/remove accounts directly from the Configuration dialog or menu.
- **Encrypted Storage (Windows DPAPI)**: Refresh tokens are securely protected with Windows DPAPI per user profile.
- **Silent Background Refresh**: Access tokens renew automatically without browser popups.

### ✏️ Complete File Management & Write Operations
- 📁 **F7 – Create Folder (`CreateDir`)**: Create new folders in *My Drive*, *Shared Drives*, and subfolders with automatic panel refresh.
- ✏️ **Shift+F6 – Quick Rename (`QuickRename`)**: Rename files and folders directly in the panel with immediate refresh and focus.
- 🗑️ **F8 – Trash & Permanent Delete (`Delete`)**:
  - Standard `F8`: Move item to Google Drive Trash.
  - `Shift+F8`: Permanently delete item.
- 📤 **F5 – Upload Files & Folders (`CopyOrMoveFromDiskToFS`)**:
  - High-throughput streaming multipart upload (256 KB buffer) supporting single files and recursive directory structures.
  - **Pass 1 Pre-scan & Dual Progress Bars**: Pre-calculates exact item count and byte totals to display both current file and total batch progress.
  - **Directory & File Collision Resolution**: Interactive prompt when folders or files already exist (Merge / Overwrite, Keep Both, Skip, Apply to all).
- 📥 **F5 – Download to Local Disk**:
  - Fast download with live progress dialog and recursive folder pre-calculation.
- 🔀 **Duplicate Name Disambiguation**:
  - Automatically identifies duplicate file/folder names in the same directory and appends a deterministic suffix with the last 6 characters of the Google Drive ID (e.g. `Folder [mK9xQ2]`, `Report [mK9xQ2].pdf`) for 100% reliable navigation and operations.
- 👁️ **F3 – Internal Viewer**:
  - Instant preview of remote files with automatic temp caching.
- 📊 **Drive Quota & Free Space Indicator**:
  - Displays remaining and total storage space in Salamander's panel footer.
- 📋 **Custom Detailed Panel Columns (`CSalamanderViewAbstract`)**:
  - **`Owner`**: Displays the file/folder creator name or email (invaluable in Shared Drives and Shared with me).
  - **`Shared`**: Indicates whether an item is shared (`Yes` / `No`).
  - **`Starred`**: Visual `*` indicator for favorite items.
  - Supports column sorting and mouse width adjustments with persistent sizing.
- 🔎 **Cloud & Fulltext Search (`Alt+F7` / `FS_SERVICE_OPENFINDDLG`)**:
  - Open Salamander Find window layout with instant Google Drive cloud search.
  - **Filename Search**: `name contains '...'` with wildcard support.
  - **Fulltext Content Search**: `fullText contains '...'` searching inside Docs, Sheets, Slides, PDFs, Text files, and image OCR.
  - **Focus & Jump**: Double-click or `Enter` immediately navigates the Salamander panel to the selected file's location and highlights it. Correctly handles *Shared with me* paths and folders with Czech/accented characters.
  - **View (F3) from results**: Opens files directly in Salamander's configured internal viewer (not the system-default app).
  - **Keyboard shortcuts**: `Enter` in any editbox starts the search; `Esc` closes the dialog (or stops an active search); `Enter` in the results list triggers Focus.
  - Modeless non-topmost window — Salamander remains accessible while search dialog is open.
  - Asynchronous background worker thread with instant `Esc` cancellation and Dark Mode support.

### ⭐ Virtual Views & Context Menu
- **Virtual Folders**:
  - `/Starred` (Starred / favorite items)
  - `/Recent` (Recently modified files)
  - `/Trash` (Trash view with restore capabilities)
- **Rich Context Menu**:
  - *Open in Web Browser* (Direct Google Drive web view)
  - *Copy Link to Clipboard*
  - *Add / Remove Star*
  - *Restore from Trash* / *Empty Trash...*
  - *Calculate Folder Size...* (Recursive scanner with Esc/Cancel support)

### 📄 Google Docs Auto-Export
- Automatically converts Google Docs, Sheets, and Slides into standard office formats (`.docx`, `.xlsx`, `.pptx`, `.pdf`) on download or viewing.

### 🌙 Dark Mode Support
- Fully integrated dark theme matching Open Salamander's native dark mode palette.

---

## 🚀 Installation

1. Download the latest release `salamander-gdrive-plugin-v0.4.zip` from [GitHub Releases](https://github.com/fila73/salamander-gdrive-plugin/releases).
2. Extract the files into the `plugins\gdrive` directory of your Open Salamander installation:
   ```
   Open Salamander/
   └── plugins/
       └── gdrive/
           ├── gdrive.spl
           └── lang/
               ├── english.slg
               └── czech.slg
   ```
3. Open **Open Salamander**, go to **Plugins → Plugins Manager...**, click **Add...**, and select `gdrive.spl`.
4. In either panel, press **Alt+F1** or **Alt+F2** and select **gdrive:**.
5. In **Plugins → Google Drive → Configuration...**, add your account and configure cache settings.

---

## ⚙️ Google Cloud OAuth Setup

To connect to your Google Drive account:

1. Create a project in [Google Cloud Console](https://console.cloud.google.com/).
2. Enable the **Google Drive API** under **APIs & Services → Library**.
3. Configure the **OAuth Consent Screen** (User Type: *External*, add your email as a *Test User*).
4. Go to **Credentials → Create Credentials → OAuth Client ID**:
   - Application Type: **Desktop App**.
5. Copy the generated **Client ID** and **Client Secret**.
6. In Open Salamander, go to **Plugins → Google Drive → Configuration...**, paste the credentials, and click **Add Account...**.

---

## 🛠️ Building from Source (MinGW-w64)

### Prerequisites:
- MinGW-w64 (GCC 10+ with C++17 support)
- `make` (`mingw32-make`)
- Open Salamander SDK (in `../salamander-plugins/salamand`)

### Build Command:
```powershell
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw
```

This compiles:
- `gdrive.spl` (Main plugin library)
- `english.slg` (English language resources)
- `czech.slg` (Czech language resources)

---

## 📄 License & Attribution

Licensed under the **GNU General Public License v2.0 or later** ([GPL-2.0-or-later](LICENSE)).  
SPDX headers are present in all source files.

- Author: **fila73**
- Inspired by **Red Salamander**
- Dark Mode integration based on the Salamander fork by **Ondrej Kotas (KRtkovo-eu-AI)**.
