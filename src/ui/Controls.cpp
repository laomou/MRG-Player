#include "ui/Controls.hpp"

#include <cstddef>
#include <utility>

namespace launcher::ui {

void ControlList::reset(std::vector<Control> controls, bool wrap) {
  controls_ = std::move(controls);
  wrap_ = wrap;
  focusFirstEnabled();
}

int ControlList::count() const { return static_cast<int>(controls_.size()); }

bool ControlList::empty() const { return controls_.empty(); }

int ControlList::focusedIndex() const { return focus_; }

int ControlList::focusedId() const {
  if (focus_ < 0 || focus_ >= count())
    return -1;
  return controls_[static_cast<std::size_t>(focus_)].id;
}

int ControlList::focusedRow() const {
  if (focus_ < 0 || focus_ >= count())
    return -1;
  return controls_[static_cast<std::size_t>(focus_)].row;
}

bool ControlList::focusRow(int row) {
  for (int index = 0; index < count(); ++index) {
    const Control &control = controls_[static_cast<std::size_t>(index)];
    if (control.row == row && control.enabled) {
      focus_ = index;
      return true;
    }
  }
  return false;
}

void ControlList::focusIndex(int index) {
  if (index < 0 || index >= count())
    return;
  focus_ = index;
}

void ControlList::focusFirstEnabled() {
  focus_ = -1;
  for (int index = 0; index < count(); ++index) {
    if (controls_[static_cast<std::size_t>(index)].enabled) {
      focus_ = index;
      return;
    }
  }
}

bool ControlList::move(int direction) {
  if (direction == 0 || count() == 0)
    return false;
  // A list without focus starts at the edge the key comes from, so the first
  // press enters the list instead of skipping a control.
  const int start = focus_ >= 0 && focus_ < count()
                        ? focus_
                        : (direction > 0 ? -1 : count());
  for (int step = 1; step <= count(); ++step) {
    int index = start + direction * step;
    if (index < 0 || index >= count()) {
      if (!wrap_)
        return false;
      index = ((index % count()) + count()) % count();
    }
    if (!controls_[static_cast<std::size_t>(index)].enabled)
      continue;
    const bool changed = index != focus_;
    focus_ = index;
    return changed;
  }
  return false;
}

int ControlList::indexOfId(int id) const {
  for (int index = 0; index < count(); ++index) {
    if (controls_[static_cast<std::size_t>(index)].id == id)
      return index;
  }
  return -1;
}

void FocusGrid::reset(std::vector<ControlList> lists) {
  lists_ = std::move(lists);
  active_ = 0;
}

int FocusGrid::listCount() const { return static_cast<int>(lists_.size()); }

int FocusGrid::listIndex() const { return active_; }

const ControlList &FocusGrid::list(int index) const {
  return lists_[static_cast<std::size_t>(index)];
}

ControlList &FocusGrid::list(int index) {
  return lists_[static_cast<std::size_t>(index)];
}

bool FocusGrid::focusList(int index) {
  if (index < 0 || index >= listCount() || index == active_)
    return false;
  active_ = index;
  if (lists_[static_cast<std::size_t>(active_)].focusedIndex() < 0)
    lists_[static_cast<std::size_t>(active_)].focusFirstEnabled();
  return true;
}

bool FocusGrid::moveVertical(int direction) {
  if (lists_.empty())
    return false;
  return lists_[static_cast<std::size_t>(active_)].move(direction);
}

bool FocusGrid::moveHorizontal(int direction) {
  if (direction == 0 || listCount() < 2)
    return false;
  const int target = active_ + direction;
  if (target < 0 || target >= listCount())
    return false;
  // Page columns line up line by line, so a move between two of them only
  // happens when the target really holds that line. That keeps the right key
  // from dropping focus onto an unrelated row, and leaves the two lanes of the
  // controller page reachable from each other. The rail has no lines of its
  // own: crossing it hands the keys to the page and keeps the row it remembers.
  if (active_ != 0 && target != 0) {
    const int row = lists_[static_cast<std::size_t>(active_)].focusedRow();
    if (row < 0 || !lists_[static_cast<std::size_t>(target)].focusRow(row))
      return false;
  } else if (lists_[static_cast<std::size_t>(target)].count() == 0) {
    return false;
  }
  active_ = target;
  if (lists_[static_cast<std::size_t>(active_)].focusedIndex() < 0)
    lists_[static_cast<std::size_t>(active_)].focusFirstEnabled();
  return true;
}

int FocusGrid::focusedId() const {
  if (lists_.empty())
    return -1;
  return lists_[static_cast<std::size_t>(active_)].focusedId();
}

bool FocusGrid::hasFocus() const {
  return !lists_.empty() &&
         lists_[static_cast<std::size_t>(active_)].focusedIndex() >= 0;
}

} // namespace launcher::ui
