// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/civilizations.hpp"

#include <QObject>

QString GetCivilizationName(Civilization civilization) {
  switch (civilization) {
  case Civilization::Gaia: return QObject::tr("Gaia");
  case Civilization::Aztecs: return QObject::tr("Aztecs");
  case Civilization::Byzantines: return QObject::tr("Byzantines");
  case Civilization::NumCivilizations: LOG(ERROR) << "GetCivilizationName() called on Civilization::NumCivilizations"; return "";
  }
  return "";
}
