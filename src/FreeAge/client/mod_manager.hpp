#pragma once

#include <filesystem>

#include <QString>

/// Reads mod-status.json to determine the list of loaded mods.
/// All paths to game data files must be acquired via GetPath() of this ModManger, which will either
/// return a path pointing to the first mod directory containing that file, or to the
/// game's original file in case no mod overrides it.
class ModManager {
 public:
  static inline ModManager& Instance() {
    static ModManager instance;
    return instance;
  }
  
  /// Tries to load the file mod-status.json at the given path.
  /// Returns true if successful.
  bool LoadModStatus(const std::filesystem::path& modStatusJsonPath, const std::filesystem::path& dataDirPath);
  
  /// Returns the absolute path to the file given by the subPath.
  std::filesystem::path GetPath(const std::filesystem::path& subPath) const;
  
 private:
  struct Mod {
    std::filesystem::path path;
    int priority;
    
    inline bool operator< (const Mod& other) const {
      return priority < other.priority;
    }
  };
  
  ModManager() = default;
  
  
  /// List of mods, sorted by increasing priority value.
  /// This means that the mods that should take precedence come first.
  std::vector<Mod> mods;
  
  std::filesystem::path dataDirPath;
};

inline std::filesystem::path GetModdedPath(const std::filesystem::path& subPath) {
  return ModManager::Instance().GetPath(subPath);
}

inline QString GetModdedPathAsQString(const std::filesystem::path& subPath) {
  return QString::fromStdString(ModManager::Instance().GetPath(subPath).string());
}
