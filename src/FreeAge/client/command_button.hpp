// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/client/shader_ui.hpp"
#include "FreeAge/client/texture.hpp"

class GameController;

constexpr int kCommandButtonRows = 3;
constexpr int kCommandButtonCols = 5;

class CommandButton {
 public:
  enum class Type {
    Invisible = 0,
    ConstructBuilding,
    ProduceUnit,
    Action
  };
  
  enum class ActionType {
    BuildEconomyBuilding,
    BuildMilitaryBuilding,
    ToggleBuildingsCategory,
    Quit
  };
  
  void InitializePointBuffers();
  void UnloadPointBuffers();
  
  /// Hides this button.
  void SetInvisible();
  
  /// Makes this a button to construct the given building type.
  void SetBuilding(BuildingType type, Qt::Key hotkey = Qt::Key_unknown);
  
  /// Makes this a button to produce the given unit type.
  void SetProduceUnit(UnitType type, Qt::Key hotkey = Qt::Key_unknown);
  
  // TODO: SetResearchTechnology(TechType type);
  
  void SetAction(ActionType actionType, const Texture* texture, Qt::Key hotkey = Qt::Key_unknown);
  
  void Render(
      float x, float y, float size, float iconInset,
      const Texture& iconOverlayNormalTexture,
      UIShader* uiShader, int widgetWidth, int widgetHeight, QOpenGLFunctions_3_2_Core* f);
  
  void Pressed(const std::vector<u32>& selection, GameController* gameController, bool shift);
  
  /// Tests whether the given point is within the button. Only works correctly after the
  /// button has been rendered.
  bool IsPointInButton(const QPoint& point) { return type != Type::Invisible && buttonRect.contains(point); }
  
  inline Type GetType() const { return type; }
  inline Qt::Key GetHotkey() const { return hotkey; }
  inline ActionType GetActionType() const { return actionType; }
  inline BuildingType GetBuildingConstructionType() const { return buildingConstructionType; }
  inline UnitType GetUnitProductionType() const { return unitProductionType; }
  
  inline GLuint GetIconPointBuffer() const { return iconPointBuffer; }
  inline GLuint GetOverlayPointBuffer() const { return overlayPointBuffer; }
  
 private:
  Type type = Type::Invisible;
  Qt::Key hotkey;
  
  ActionType actionType;
  BuildingType buildingConstructionType;
  UnitType unitProductionType;
  const Texture* texture = nullptr;
  QRectF buttonRect;
  
  GLuint iconPointBuffer;
  GLuint overlayPointBuffer;
};
