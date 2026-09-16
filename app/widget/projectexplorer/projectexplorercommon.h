/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef PROJECTEXPLORERCOMMON_H
#define PROJECTEXPLORERCOMMON_H

#include <QAbstractItemView>
#include <QMouseEvent>

namespace olive {

/**
 * @brief Shared double-click handling for the List/Icon (QListView) and Tree (QTreeView) views
 *
 * QAbstractItemView::doubleClicked() is only emitted when a double click lands on a valid item.
 * Both project explorer views additionally need to detect double clicks on empty space. The two
 * views cannot share a single QObject base because they inherit from distinct QAbstractItemView
 * subclasses, so this helper is shared instead: `default_handler` invokes the view's own base
 * class handler, and `empty_area_clicked` fires when the double click missed every item.
 */
template<typename F1, typename F2>
inline void ProjectExplorerHandleDoubleClick(QAbstractItemView* view, QMouseEvent* event, F1 default_handler, F2 empty_area_clicked)
{
  // Cache here so if the index becomes invalid after the base call, we still know the truth
  bool item_at_location = view->indexAt(event->pos()).isValid();

  // Perform default double click functions
  default_handler();

  // QAbstractItemView already has a doubleClicked() signal, but we emit another here for double clicking empty space
  if (!item_at_location) {
    empty_area_clicked();
  }
}

}

#endif // PROJECTEXPLORERCOMMON_H