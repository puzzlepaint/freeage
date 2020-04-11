// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/settings_dialog.hpp"

#include <QBoxLayout>
#include <QDir>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>

#include "FreeAge/client/about_dialog.hpp"
#include "FreeAge/common/logging.hpp"


void Settings::Save() {
  QSettings settings;
  
  // TODO: Potentially check the file permissions of the file we save this to.
  //       It happended on Linux that only the root could write to this file,
  //       thus any attempts to change settings silently failed.
  LOG(INFO) << "Saving settings to: " << settings.fileName().toStdString();
  
  settings.setValue("dataPath", QString::fromStdString(dataPath.string()));
  settings.setValue("modsPath", QString::fromStdString(modsPath.string()));
  settings.setValue("playerName", playerName);
  settings.setValue("fullscreen", fullscreen);
  settings.setValue("grabMouse", grabMouse);
  settings.setValue("uiScale", uiScale);
  settings.setValue("debugNetworking", debugNetworking);
}

void Settings::TryLoad() {
  QSettings settings;
  LOG(INFO) << "Trying to load settings from: " << settings.fileName().toStdString();
  
  dataPath = settings.value("dataPath", "").toString().toStdString();
  modsPath = settings.value("modsPath", "").toString().toStdString();
  if (dataPath.empty() && modsPath.empty()) {
    #ifdef WIN32
      TryToFindPathsOnWindows();
    #else
      TryToFindPathsOnLinux();
    #endif
  }
  
  playerName = settings.value("playerName", "").toString();
  if (playerName.isEmpty()) {
    // Choose a random player name
    QString randomNames[5] = {
        "Alfred the Alpaca",
        "Bleda the Hun",
        "William Wallace",
        "Tamerlane",
        "Joan of Arc"};
    playerName = randomNames[rand() % 5];
  }
  
  fullscreen = settings.value("fullscreen", true).toBool();
  grabMouse = settings.value("grabMouse", true).toBool();
  uiScale = settings.value("uiScale", 0.5f).toFloat();
  debugNetworking = settings.value("debugNetworking", false).toBool();
}

void Settings::TryToFindPathsOnWindows() {
  // Try to find the data folder on Windows.
  QSettings registryKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve", QSettings::NativeFormat);
  QString steamPath = registryKey.value("Steam/InstallPath").toString();
  if (steamPath.isEmpty()) {
    QSettings registryKey = QSettings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Valve", QSettings::NativeFormat);
    steamPath = registryKey.value("Steam/InstallPath").toString();
  }
  if (!steamPath.isEmpty()) {
    QDir steamDir(steamPath);
    if (steamDir.exists() && steamDir.cd("steamapps")) {
      QString libraryFoldersFilePath = steamDir.filePath("libraryfolders.vdf");
      if (steamDir.cd("common") && steamDir.cd("AoE2DE") && steamDir.exists() && !steamDir.isEmpty()) {
        dataPath = steamDir.path().toStdString();
      } else {
        LOG(INFO) << "TryToFindPathsOnWindows(): Checking additional Steam libraries ...";
        
        // Read information about other library directories from libraryfolders.vdf and search in those too
        QStringList libraryDirectories;
        
        QFile vdfFile(libraryFoldersFilePath);
        if (vdfFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
          while (true) {
            QString lineText = vdfFile.readLine();
            // Note: canReadLine() did not appear to work on Windows, so instead of checking this beforehand,
            // we break if the returned line is empty (i.e., we do not even get the line termination character(s)).
            if (lineText.isEmpty()) {
              break;
            }
            
            QStringList words = lineText.trimmed().split('\t', QString::SkipEmptyParts);
            if (words.size() == 2 &&
                words[0] == "\"1\"" &&
                words[1].size() > 2) {
              libraryDirectories.push_back(words[1].mid(1, words[1].size() - 2).replace("\\\\", "\\"));
            }
          }
        }
        
        for (QString& libraryDirectory : libraryDirectories) {
          LOG(INFO) << "TryToFindPathsOnWindows(): Checking Steam library: " << libraryDirectory.toStdString();
          
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
  
  
  QWidget* dataGroup = new QWidget();
  
  QLabel* dataFolderLabel = new QLabel(tr("AoE2DE folder path: "));
  dataFolderEdit = new QLineEdit(QString::fromStdString(settings->dataPath.string()));
  QPushButton* dataFolderButton = new QPushButton(tr("Select"));
  
  QHBoxLayout* dataFolderEditLayout = new QHBoxLayout();
  dataFolderEditLayout->setContentsMargins(0, 0, 0, 0);
  dataFolderEditLayout->setSpacing(0);
  dataFolderEditLayout->addWidget(dataFolderEdit);
  dataFolderEditLayout->addWidget(dataFolderButton);
  
  QLabel* modsFolderLabel = new QLabel(tr("Mods folder path: "));
  modsFolderEdit = new QLineEdit(QString::fromStdString(settings->modsPath.string()));
  QPushButton* modsFolderButton = new QPushButton(tr("Select"));
  
  QHBoxLayout* modsFolderEditLayout = new QHBoxLayout();
  modsFolderEditLayout->setContentsMargins(0, 0, 0, 0);
  modsFolderEditLayout->setSpacing(0);
  modsFolderEditLayout->addWidget(modsFolderEdit);
  modsFolderEditLayout->addWidget(modsFolderButton);
  
  QGridLayout* dataLayout = new QGridLayout();
  int row = 0;
  dataLayout->addWidget(dataFolderLabel, row, 0);
  dataLayout->addLayout(dataFolderEditLayout, row, 1);
  ++ row;
  dataLayout->addWidget(modsFolderLabel, row, 0);
  dataLayout->addLayout(modsFolderEditLayout, row, 1);
  dataGroup->setLayout(dataLayout);
  
  
  QWidget* preferencesGroup = new QWidget();
  
  QLabel* playerNameLabel = new QLabel(tr("Player name: "));
  playerNameEdit = new QLineEdit(settings->playerName);
  
  QLabel* uiScaleLabel = new QLabel(tr("UI Scale: "));
  uiScaleEdit = new QLineEdit(QString::number(settings->uiScale));
  uiScaleEdit->setValidator(new QDoubleValidator(0.01, 100, 2, uiScaleEdit));
  
  fullscreenCheck = new QCheckBox(tr("Fullscreen"));
  fullscreenCheck->setChecked(settings->fullscreen);
  
  grabMouseCheck = new QCheckBox(tr("Clamp cursor to game window area"));
  grabMouseCheck->setChecked(settings->grabMouse);
  
  QGridLayout* preferencesLayout = new QGridLayout();
  row = 0;
  preferencesLayout->addWidget(playerNameLabel, row, 0);
  preferencesLayout->addWidget(playerNameEdit, row, 1);
  ++ row;
  preferencesLayout->addWidget(uiScaleLabel, row, 0);
  preferencesLayout->addWidget(uiScaleEdit, row, 1);
  ++ row;
  preferencesLayout->addWidget(fullscreenCheck, row, 0, 1, 2);
  ++ row;
  preferencesLayout->addWidget(grabMouseCheck, row, 0, 1, 2);
  preferencesGroup->setLayout(preferencesLayout);
  
  
  QTabWidget* tabWidget = new QTabWidget();
  tabWidget->addTab(preferencesGroup, tr("Preferences"));
  tabWidget->addTab(dataGroup, tr("Data files"));
  
  debugNetworkingCheck = new QCheckBox(tr("Enable debug logging for networking"));
  debugNetworkingCheck->setChecked(settings->debugNetworking);
  
  QPushButton* exitButton = new QPushButton(tr("Exit"));
  QPushButton* aboutButton = new QPushButton(tr("About"));
  QPushButton* hostButton = new QPushButton(tr("Create new lobby"));
  QPushButton* hostOnServerButton = new QPushButton(tr("Create lobby on existing server"));
  QPushButton* joinButton = new QPushButton(tr("Join existing lobby"));
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->addWidget(exitButton);
  buttonsLayout->addWidget(aboutButton);
  buttonsLayout->addStretch(1);
  buttonsLayout->addWidget(hostButton);
  buttonsLayout->addWidget(hostOnServerButton);
  buttonsLayout->addWidget(joinButton);
  
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(tabWidget);
  layout->addWidget(debugNetworkingCheck);
  layout->addLayout(buttonsLayout);
  setLayout(layout);
  
  resize(std::max(600, width()), 0);
  
  // --- Connections ---
  connect(hostButton, &QPushButton::clicked, this, &SettingsDialog::HostGame);
  connect(hostOnServerButton, &QPushButton::clicked, this, &SettingsDialog::HostGameOnServer);
  connect(joinButton, &QPushButton::clicked, this, &SettingsDialog::JoinGame);
  connect(aboutButton, &QPushButton::clicked, this, &SettingsDialog::ShowAboutDialog);
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
  
  hostPassword = "";
  hostGameChosen = true;
  accept();
}

void SettingsDialog::HostGameOnServer() {
  if (!CheckSettings()) {
    return;
  }
  SaveSettings();
  
  bool ok;
  serverAddressText = QInputDialog::getText(
      this, tr("Enter server address to connect to"), tr("Address:"), QLineEdit::Normal, "127.0.0.1", &ok);
  if (!ok || serverAddressText.isEmpty()) {
    return;
  }
  
  hostPassword = QInputDialog::getText(
      this, tr("Enter host password"), tr("Host password:"), QLineEdit::Normal, "", &ok);
  if (!ok || hostPassword.isEmpty()) {
    return;
  }
  
  hostGameChosen = true;
  accept();
}

void SettingsDialog::JoinGame() {
  if (!CheckSettings()) {
    return;
  }
  SaveSettings();
  
  bool ok;
  serverAddressText = QInputDialog::getText(
      this, tr("Enter server address to connect to"), tr("Address:"), QLineEdit::Normal, "127.0.0.1", &ok);
  if (ok && !serverAddressText.isEmpty()) {
    hostGameChosen = false;
    accept();
  }
}

void SettingsDialog::ShowAboutDialog() {
  AboutDialog aboutDialog(this);
  aboutDialog.exec();
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
  settings->fullscreen = fullscreenCheck->isChecked();
  settings->grabMouse = grabMouseCheck->isChecked();
  settings->uiScale = uiScaleEdit->text().toDouble();
  settings->debugNetworking = debugNetworkingCheck->isChecked();
}
