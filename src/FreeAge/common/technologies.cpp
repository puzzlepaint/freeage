// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/technologies.hpp"

#include <QObject>

QString GetTechnologyName(Technology technology) {
  switch (technology) {
  case Technology::DarkAge: return QObject::tr("Dark Age");
  case Technology::FeudalAge: return QObject::tr("Feudal Age");
  case Technology::CastleAge: return QObject::tr("Castle Age");
  case Technology::ImperialAge: return QObject::tr("Imperial Age");
  case Technology::Loom: return QObject::tr("Loom");
  case Technology::Atlatl: return QObject::tr("Atlatl");
  case Technology::GreekFire: return QObject::tr("Greek Fire");
  case Technology::NumTechnologies: LOG(ERROR) << "GetTechnologyName() called on Technology::NumTechnologies"; return "";
  }
  return "";
}
