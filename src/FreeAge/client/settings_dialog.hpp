// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <filesystem>

#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>

struct Settings {
  void Save();
  void TryLoad();
  
  std::filesystem::path dataPath;
  std::filesystem::path modsPath;
  QString playerName;
  float uiScale;
  bool fullscreen;
  bool grabMouse;
  bool debugNetworking;
  
 private:
  void TryToFindPathsOnWindows();
  void TryToFindPathsOnLinux();
};

/// A settings dialog to set the player name, file locations, etc.
/// Not intended to be used in the final game, only for the prototype.
class SettingsDialog : public QDialog {
 public:
  SettingsDialog(Settings* settings, QWidget* parent = nullptr);
  
  inline QString GetDataPath() const { return dataFolderEdit->text(); }
  inline QString GetModsPath() const { return modsFolderEdit->text(); }
  inline QString GetPlayerName() const { return playerNameEdit->text(); }
  inline bool HostGameChosen() const { return hostGameChosen; }
  inline const QString& GetServerAddress() const { return serverAddressText; }
  inline const QString& GetHostPassword() const { return hostPassword; }
  
 private slots:
  void HostGame();
  void HostGameOnServer();
  void JoinGame();
  void ShowAboutDialog();
  
 private:
  bool CheckSettings();
  void SaveSettings();
  
  QLineEdit* dataFolderEdit;
  QLineEdit* modsFolderEdit;
  QLineEdit* playerNameEdit;
  QCheckBox* fullscreenCheck;
  QCheckBox* grabMouseCheck;
  QLineEdit* uiScaleEdit;
  QCheckBox* debugNetworkingCheck;
  bool hostGameChosen;
  QString serverAddressText;
  QString hostPassword;
  
  Settings* settings;
};
