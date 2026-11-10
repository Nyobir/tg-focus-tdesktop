/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/focus_mode_logic.h"
#include "data/data_types.h"

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Core::Focus {

[[nodiscard]] bool Active();
[[nodiscard]] rpl::producer<bool> ActiveValue();
[[nodiscard]] rpl::producer<Logic::Status> StatusValue();
[[nodiscard]] FilterId ForcedFilterId(not_null<Main::Session*> session);
[[nodiscard]] bool Allowed(not_null<PeerData*> peer);
[[nodiscard]] QString BlockedText();

[[nodiscard]] int32 ApiId();
[[nodiscard]] QString ApiHash();

} // namespace Core::Focus
