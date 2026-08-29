# Google Drive Plugin for Open Salamander

A full-featured filesystem and viewer plugin for **Open Salamander** (and the Dark Mode fork **KRtkovo-eu-AI/salamander**), allowing you to browse, view, calculate sizes, and download files and folders from **Google Drive** (including *My Drive* and *Shared Drives*) directly within Salamander's dual-pane interface.

---

## ✨ Features

- 🔒 **Secure Authentication (OAuth 2.0 with PKCE)**:
  - Sign in conveniently using your default web browser without sharing passwords with the application.
  - User-configurable **OAuth Client ID** and **OAuth Client Secret** in the plugin Configuration dialog.
- 🔑 **Encrypted Token Storage (Windows DPAPI)**:
  - Refresh tokens are securely encrypted per Windows user profile.
  - **Silent Refresh**: Access tokens are refreshed in the background without recurring browser popups.
- 📁 **Complete Google Drive Support**:
  - **My Drive**: Personal Google Drive storage.
  - **Shared Drives**: Google Workspace Team Drives.
  - **Shared with me**: Access items shared with your account.
- ⚡ **Folder Size Calculation (Space / Context Menu)**:
  - Recursive folder size and item count scanner with live progress dialog.
  - Graceful cancellation support (**Cancel** button or `Esc` key) returning partial statistics.
  - Automatically updates the panel and replaces `DIR` / `ADR` in the **Size** column with the formatted folder size.
- 📥 **Copying & Downloading (F5)**:
  - Download single files or entire directory trees recursively to the local disk.
- 👁️ **Built-in Viewer (F3)**:
  - Fast file previewing with automatic caching.
- 📄 **Google Docs Export**:
  - Automatically converts Google Docs, Sheets, and Slides into standard formats (`.docx`, `.xlsx`, `.pptx`, `.pdf`).
- 🌙 **Full Dark Mode Support**:
  - Seamless dark theme integration matching Open Salamander's dark mode palette.

---

## 🚀 Installation

1. Download the latest release `salamander-gdrive-plugin-v1.0.zip` from GitHub Releases.
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
5. In **Plugins → Google Drive → Configuration...**, enter your Google OAuth Client ID and Secret (or follow the setup guide below).

---

## ⚙️ Google Cloud OAuth Setup

To connect to your Google Drive account, create a free desktop application credential in the [Google Cloud Console](https://console.cloud.google.com/):

1. Create a new project in Google Cloud Console.
2. Enable the **Google Drive API** under **APIs & Services → Library**.
3. Configure the **OAuth Consent Screen** (User Type: *External*, add your email as a *Test User*).
4. Go to **Credentials → Create Credentials → OAuth Client ID**:
   - Application Type: **Desktop App**.
5. Copy the generated **Client ID** and **Client Secret**.
6. In Open Salamander, go to **Plugins → Google Drive → Configuration...**, paste the credentials, and click **Sign In**.

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

- Ported to Open Salamander by **fila73**.
- Dark Mode integration based on the Salamander fork by **Ondrej Kotas (KRtkovo-eu-AI)**.
