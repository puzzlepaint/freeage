#pragma once

#include <QPointF>

#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/server/object.hpp"

/// Represents a unit on the server.
class ServerUnit : public ServerObject {
 public:
  ServerUnit(int playerIndex, UnitType type, const QPointF& mapCoord);
  
  inline UnitType GetUnitType() const { return type; }
  inline const QPointF& GetMapCoord() const { return mapCoord; }
  
 private:
  UnitType type;
  QPointF mapCoord;
};
