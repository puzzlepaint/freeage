#include "FreeAge/client/object.hpp"

#include "FreeAge/client/building.hpp"
#include "FreeAge/client/unit.hpp"

QString ClientObject::GetObjectName() const {
  if (isBuilding()) {
    const ClientBuilding* building = static_cast<const ClientBuilding*>(this);
    return building->GetBuildingName();
  } else if (isUnit()) {
    const ClientUnit* unit = static_cast<const ClientUnit*>(this);
    return unit->GetUnitName();
  }
  return "";
}

const Texture* ClientObject::GetIconTexture() const {
  if (isBuilding()) {
    const ClientBuilding* building = static_cast<const ClientBuilding*>(this);
    return building->GetIconTexture();
  } else if (isUnit()) {
    const ClientUnit* unit = static_cast<const ClientUnit*>(this);
    return unit->GetIconTexture();
  }
  return nullptr;
}
