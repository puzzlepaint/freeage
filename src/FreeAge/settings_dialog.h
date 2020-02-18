#pragma once

#include <filesystem>

#include <QDialog>
#include <QLineEdit>

struct Settings {
  void InitializeWithDefaults();
  void Save();
  bool TryLoad();
  
  std::filesystem::path dataPath;
  std::filesystem::path modsPath;
  std::string playerName;
  
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
  
 private slots:
  void HostGame();
  void JoinGame();
  
 private:
  bool CheckSettings();
  
  QLineEdit* dataFolderEdit;
  QLineEdit* modsFolderEdit;
  QLineEdit* playerNameEdit;
  bool hostGameChosen;
};
