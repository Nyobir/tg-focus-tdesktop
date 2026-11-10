/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Settings {

[[nodiscard]] Type WorkingHoursId();
[[nodiscard]] QString FormatWorkingTime(TimeId time);
void ChooseWorkingTimeBox(
	not_null<Ui::GenericBox*> box,
	TimeId value,
	Fn<void(TimeId)> save);

} // namespace Settings
