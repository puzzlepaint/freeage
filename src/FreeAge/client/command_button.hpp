#pragma once

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
    ProduceUnit
  };
  
  /// Hides this button.
  void SetInvisible();
  
  /// Makes this a button to produce the given unit type.
  void SetProduceUnit(UnitType type);
  
  // TODO: SetResearchTechnology(TechType type);
  
  void Render(
      float x, float y, float size, float iconInset,
      const Texture& iconOverlayNormalTexture,
      UIShader* uiShader, int widgetWidth, int widgetHeight, GLuint pointBuffer);
  
  void Pressed(const std::vector<u32>& selection, GameController* gameController);
  
  /// Tests whether the given point is within the button. Only works correctly after the
  /// button has been rendered.
  bool IsPointInButton(const QPoint& point) { return type != Type::Invisible && buttonRect.contains(point); }
  
 private:
  Type type = Type::Invisible;
  UnitType unitProductionType;
  
  QRectF buttonRect;
};
