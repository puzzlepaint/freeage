// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/mod_manager.hpp"

#include <yaml-cpp/yaml.h>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"

bool ModManager::LoadModStatus(const std::filesystem::path& modStatusJsonPath, const std::filesystem::path& dataDirPath) {
  mods.clear();
  std::filesystem::path modsBasePath = modStatusJsonPath.parent_path();
  this->dataDirPath = dataDirPath;
  
  YAML::Node fileNode;
  try {
    fileNode = YAML::LoadFile(modStatusJsonPath.string());
  } catch (const YAML::BadFile& badFileException) {
    LOG(ERROR) << "Cannot read file: " << modStatusJsonPath << " (YAML::BadFile exception)";
    return false;
  } catch (const YAML::ParserException& parserException) {
    LOG(ERROR) << "Cannot read file: " << modStatusJsonPath << " (YAML::ParserException exception)";
    return false;
  }
  
  if (fileNode.IsNull()) {
    LOG(ERROR) << "Cannot read file: " << modStatusJsonPath << ": The file node is null.";
    return false;
  }
  if (!fileNode.IsSequence()) {
    LOG(ERROR) << "Cannot parse file: " << modStatusJsonPath << ": The root node is not a sequence.";
    return false;
  }
  
  // Example entry:
  // {"CheckSum":"2624949055",
  //  "Enabled":true,
  //  "LastUpdate":"1582851424693",
  //  "Path":"subscribed//1062_Improved Tech Tree UI Mod",
  //  "Priority":1,
  //  "PublishID":0,
  //  "Title":"Improved Tech Tree UI Mod",
  //  "WorkshopID":1062}
  for (usize i = 0; i < fileNode.size(); ++ i) {
    YAML::Node modNode = fileNode[i];
    
    if (!modNode["Priority"].IsDefined() ||
        !modNode["Path"].IsDefined()) {
      LOG(ERROR) << "Encountered a mod entry that lacks the 'Priority' or 'Path' attribute. Skipping. Node:\n" << modNode;
      continue;
    }
    
    mods.emplace_back();
    mods.back().priority = modNode["Priority"].as<int>();
    mods.back().path = modsBasePath / modNode["Path"].as<std::string>();
  }
  
  // Sort the mods by priority.
  std::sort(mods.begin(), mods.end());
  
  return true;
}

std::filesystem::path ModManager::GetPath(const std::filesystem::path& subPath) const {
  for (const auto& mod : mods) {
    std::filesystem::path testPath = mod.path / subPath;
    if (std::filesystem::exists(testPath)) {
      return testPath;
    }
  }
  
  // Did not find a file among the loaded mods, return the path into the standard data directory.
  return dataDirPath / subPath;
}
