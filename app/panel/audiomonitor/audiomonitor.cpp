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

#include "audiomonitor.h"

#include "panel/panelmanager.h"

namespace olive {

#define super PanelWidget

AudioMonitorPanel::AudioMonitorPanel() :
  super(QStringLiteral("AudioMonitor"))
{
  audio_monitor_ = new AudioMonitor(this);

  SetWidgetWithPadding(audio_monitor_);

  Retranslate();

  // Set fixed width for AudioMonitorPanel so it remains fixed like the toolbar
  const int kAudioMonitorFixedWidth = 50;
  setFixedWidth(kAudioMonitorFixedWidth);

  // Hide QTabBar scroll buttons (< and >) and remove tab title overflow card
  setStyleSheet(QStringLiteral(
    "QTabBar::scroller { width: 0px; height: 0px; }\n"
    "QTabBar QToolButton { width: 0px; height: 0px; max-width: 0px; max-height: 0px; margin: 0px; padding: 0px; border: none; }\n"
  ));
}

void AudioMonitorPanel::Retranslate()
{
  SetTitle(QString());
}

}
