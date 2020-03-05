#include "FreeAge/client/settings_dialog.hpp"

#include <QBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "FreeAge/common/logging.hpp"


void Settings::InitializeWithDefaults() {
#ifdef WIN32
  TryToFindPathsOnWindows();
#else
  TryToFindPathsOnLinux();
#endif
  
  // Choose a random player name
  QString randomNames[5] = {
      "Alfred the Alpaca",
      "Bleda the Hun",
      "William Wallace",
      "Tamerlane",
      "Joan of Arc"};
  playerName = randomNames[rand() % 5];
}

void Settings::Save() {
  QSettings settings;
  settings.setValue("dataPath", QString::fromStdString(dataPath));
  settings.setValue("modsPath", QString::fromStdString(modsPath));
  settings.setValue("playerName", playerName);
}

bool Settings::TryLoad() {
  QSettings settings;
  dataPath = settings.value("dataPath").toString().toStdString();
  modsPath = settings.value("modsPath").toString().toStdString();
  playerName = settings.value("playerName").toString();
  return !dataPath.empty() || !modsPath.empty() || !playerName.isEmpty();
}

void Settings::TryToFindPathsOnWindows() {
  // Try to find the data folder on Windows.
  QSettings registryKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve", QSettings::NativeFormat);
  QString steamPath = registryKey.value("Steam").toString();
  if (steamPath.isEmpty()) {
    QSettings registryKey = QSettings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Valve", QSettings::NativeFormat);
    steamPath = registryKey.value("Steam").toString();
  }
  if (!steamPath.isEmpty()) {
    QDir steamDir(steamPath);
    if (steamDir.exists() && steamDir.cd("steamapps")) {
      QString libraryFoldersFilePath = steamDir.filePath("libraryfolders.vdf");
      if (steamDir.cd("common") && steamDir.cd("AoE2DE")) {
        if (steamDir.exists() && !steamDir.isEmpty()) {
          dataPath = steamDir.path().toStdString();
        } else {
          // Read information about other library directories from libraryfolders.vdf and search in those too
          QStringList libraryDirectories;
          
          QFile vdfFile(libraryFoldersFilePath);
          if (vdfFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (vdfFile.canReadLine()) {
              QString lineText = vdfFile.readLine();
              QStringList words = lineText.split('\t', QString::SkipEmptyParts);
              if (words.size() == 2 &&
                  words[0] == "\"1\"") {
                libraryDirectories.push_back(words[1]);
              }
            }
          }
          
          for (QString& libraryDirectory : libraryDirectories) {
            QDir testDir(libraryDirectory);
            if (!testDir.cd("steamapps")) { continue; }
            if (!testDir.cd("common")) { continue; }
            if (!testDir.cd("AoE2DE")) { continue; }
            if (testDir.exists() && !testDir.isEmpty()) {
              dataPath = testDir.path().toStdString();
              break;
            }
          }
        }
      }
    }
  }
  
  // Try to find the mods folder on Windows.
  QStringList homePaths = QStandardPaths::standardLocations(QStandardPaths::HomeLocation);
  for (QString homePath : homePaths) {
    QDir homeDir(homePath);
    if (!homeDir.cd("Games")) { continue; }
    if (!homeDir.cd("Age of Empires 2 DE")) { continue; }
    if (homeDir.exists()) {
      // Cd into the directory containing only digits in its name, but is not "0"
      QStringList dirList = homeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      for (QString dirName : dirList) {
        bool ok;
        if (dirName.toLongLong(&ok) > 0 && ok) {
          QDir testDir = homeDir;
          if (!testDir.cd(dirName)) { continue; }
          if (!testDir.cd("mods")) { continue; }
          if (testDir.exists() && !testDir.isEmpty()) {
            modsPath = testDir.path().toStdString();
            break;
          }
        }
      }
    }
  }
}

void Settings::TryToFindPathsOnLinux() {
  // Try to find the data folder on Linux.
  // Try to find the Steam directory as a subdirectory of the home dir.
  QDir steamDir;
  bool haveSteamDir = false;
  QStringList homePaths = QStandardPaths::standardLocations(QStandardPaths::HomeLocation);
  for (QString homePath : homePaths) {
    QDir homeDir(homePath);
    if (homeDir.cd(".local") && homeDir.cd("share") && homeDir.cd("Steam")) {
      if (homeDir.exists() && !homeDir.isEmpty()) {
        steamDir = homeDir;
        haveSteamDir = true;
      }
      if (homeDir.cd("steamapps") && homeDir.cd("common") && homeDir.cd("AoE2DE")) {
        if (homeDir.exists()) {
          dataPath = homeDir.path().toStdString();
          break;
        }
      }
    }
  }
  
  // Try to find the mods folder on Linux.
  bool modsFound = false;
  if (haveSteamDir) {
    QDir testDir = steamDir;
    testDir.cd("steamapps");
    testDir.cd("compatdata");
    QStringList dirList = testDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (QString dirName : dirList) {
      QDir testSubDir = testDir;
      if (!testSubDir.cd(dirName)) { continue; }
      if (!testSubDir.cd("pfx")) { continue; }
      if (!testSubDir.cd("drive_c")) { continue; }
      if (!testSubDir.cd("users")) { continue; }
      if (!testSubDir.cd("steamuser")) { continue; }
      if (!testSubDir.cd("Games")) { continue; }
      if (!testSubDir.cd("Age of Empires 2 DE")) { continue; }
      // Cd into the directory containing only digits in its name, but is not "0"
      QStringList dirList = testSubDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      for (QString dirName : dirList) {
        bool ok;
        if (dirName.toLongLong(&ok) > 0 && ok) {
          QDir testModsDir = testSubDir;
          if (!testModsDir.cd(dirName)) { continue; }
          if (!testModsDir.cd("mods")) { continue; }
          if (testModsDir.exists() && !testModsDir.isEmpty()) {
            modsPath = testModsDir.path().toStdString();
            modsFound = true;
            break;
          }
        }
      }
      if (modsFound) {
        break;
      }
    }
  }
}


SettingsDialog::SettingsDialog(Settings* settings, QWidget* parent)
    : QDialog(parent),
      settings(settings) {
  setWindowIcon(QIcon(":/free_age/free_age.png"));
  setWindowTitle(tr("FreeAge - Setup"));
  
  QLabel* dataFolderLabel = new QLabel(tr("AoE2DE folder path: "));
  dataFolderEdit = new QLineEdit(QString::fromStdString(settings->dataPath));
  QPushButton* dataFolderButton = new QPushButton(tr("Select"));
  
  QHBoxLayout* dataFolderEditLayout = new QHBoxLayout();
  dataFolderEditLayout->setContentsMargins(0, 0, 0, 0);
  dataFolderEditLayout->setSpacing(0);
  dataFolderEditLayout->addWidget(dataFolderEdit);
  dataFolderEditLayout->addWidget(dataFolderButton);
  
  QLabel* modsFolderLabel = new QLabel(tr("Mods folder path: "));
  modsFolderEdit = new QLineEdit(QString::fromStdString(settings->modsPath));
  QPushButton* modsFolderButton = new QPushButton(tr("Select"));
  
  QHBoxLayout* modsFolderEditLayout = new QHBoxLayout();
  modsFolderEditLayout->setContentsMargins(0, 0, 0, 0);
  modsFolderEditLayout->setSpacing(0);
  modsFolderEditLayout->addWidget(modsFolderEdit);
  modsFolderEditLayout->addWidget(modsFolderButton);
  
  QLabel* playerNameLabel = new QLabel(tr("Player name: "));
  playerNameEdit = new QLineEdit(settings->playerName);
  
  QPushButton* exitButton = new QPushButton(tr("Exit"));
  QPushButton* hostButton = new QPushButton(tr("Host a game"));
  QPushButton* joinButton = new QPushButton(tr("Join a game"));
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->addWidget(exitButton);
  buttonsLayout->addStretch(1);
  buttonsLayout->addWidget(hostButton);
  buttonsLayout->addWidget(joinButton);
  
  QGridLayout* layout = new QGridLayout();
  int row = 0;
  layout->addWidget(dataFolderLabel, row, 0);
  layout->addLayout(dataFolderEditLayout, row, 1);
  ++ row;
  layout->addWidget(modsFolderLabel, row, 0);
  layout->addLayout(modsFolderEditLayout, row, 1);
  ++ row;
  layout->addWidget(playerNameLabel, row, 0);
  layout->addWidget(playerNameEdit, row, 1);
  ++ row;
  layout->addLayout(buttonsLayout, row, 0, 1, 2);
  setLayout(layout);
  
  resize(std::max(600, width()), 0);
  
  // --- Connections ---
  connect(hostButton, &QPushButton::clicked, this, &SettingsDialog::HostGame);
  connect(joinButton, &QPushButton::clicked, this, &SettingsDialog::JoinGame);
  connect(exitButton, &QPushButton::clicked, this, &QDialog::reject);
  
  connect(dataFolderButton, &QPushButton::clicked, [&]() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select AoE2DE folder path"), dataFolderEdit->text(), QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) {
      dataFolderEdit->setText(dir);
    }
  });
  connect(modsFolderButton, &QPushButton::clicked, [&]() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select mods folder path"), modsFolderEdit->text(), QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) {
      modsFolderEdit->setText(dir);
    }
  });
}

void SettingsDialog::HostGame() {
  if (!CheckSettings()) {
    return;
  }
  SaveSettings();
  
  hostGameChosen = true;
  accept();
}

void SettingsDialog::JoinGame() {
  if (!CheckSettings()) {
    return;
  }
  SaveSettings();
  
  bool ok;
  ipText = QInputDialog::getText(
      this, tr("Enter IP to connect to"), tr("IP:"), QLineEdit::Normal, "127.0.0.1", &ok);
  if (ok && !ipText.isEmpty()) {
    hostGameChosen = false;
    accept();
  }
}

bool SettingsDialog::CheckSettings() {
  QDir dataDir(dataFolderEdit->text());
  if (!QDir(dataDir.filePath("resources")).exists()) {
    QMessageBox::warning(this, tr("Setup"), tr("Please set the AoE2DE path to the path of a valid game installation. The \"resources\" directory was not found in the currently set path (%1).").arg(dataFolderEdit->text()));
    return false;
  }
  
  QDir modsDir(modsFolderEdit->text());
  if (!QFile(modsDir.filePath("mod-status.json")).exists()) {
    if (QMessageBox::question(this, tr("Setup"), tr("Warning: mod-status.json was not found in the given mods directory (%1). No mods will be used. Continue?").arg(modsFolderEdit->text()), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return false;
    }
  }
  
  if (playerNameEdit->text().isEmpty()) {
    QMessageBox::warning(this, tr("Setup"), tr("Please enter a player name."));
    return false;
  }
  
  return true;
}

void SettingsDialog::SaveSettings() {
  // TODO: Is Utf8 the right encoding here? What does std::filesystem::path expect?
  settings->dataPath = dataFolderEdit->text().toUtf8().data();
  settings->modsPath = modsFolderEdit->text().toUtf8().data();
  settings->playerName = playerNameEdit->text();
}
