#pragma once

#include <vector>

namespace launcher::ui {

// A focusable control. Geometry stays with the screen that draws it: the focus
// list only needs to know what can take focus and in what order, which is the
// part that used to be re-implemented per screen and per input device.
struct Control {
  int id = -1;
  bool enabled = true;
};

// One column of controls. Focus skips disabled entries, so a row that is not
// actionable yet stays visible without trapping the keys. A rail wraps at both
// ends; a page column clamps instead, because jumping from the refresh button
// to the last platform is not a move a player would ask for.
class ControlList {
public:
  void reset(std::vector<Control> controls, bool wrap = true);

  [[nodiscard]] int count() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] int focusedIndex() const;
  // The id of the focused control, or -1 when the list is empty.
  [[nodiscard]] int focusedId() const;

  // Explicit focus, used by the pointer, which may land on any control.
  void focusIndex(int index);
  void focusFirstEnabled();

  // Moves one step, returning whether the focus actually moved.
  [[nodiscard]] bool move(int direction);
  [[nodiscard]] int indexOfId(int id) const;

private:
  std::vector<Control> controls_;
  int focus_ = -1;
  bool wrap_ = true;
};

// Columns laid out left to right. The direction keys move inside the active
// column; left and right cross between columns. The settings page is a rail of
// categories next to a column of rows, and its core page puts the refresh
// button at the front of the row column.
class FocusGrid {
public:
  void reset(std::vector<ControlList> lists);

  [[nodiscard]] int listCount() const;
  [[nodiscard]] int listIndex() const;
  [[nodiscard]] const ControlList &list(int index) const;
  ControlList &list(int index);

  // Returns whether the active column changed.
  [[nodiscard]] bool focusList(int index);
  [[nodiscard]] bool moveVertical(int direction);
  [[nodiscard]] bool moveHorizontal(int direction);

  [[nodiscard]] int focusedId() const;
  [[nodiscard]] bool hasFocus() const;

private:
  std::vector<ControlList> lists_;
  int active_ = 0;
};

} // namespace launcher::ui
