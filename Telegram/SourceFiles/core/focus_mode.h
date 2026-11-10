/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/focus_mode_logic.h"
#include "data/data_types.h"

class History;
class PeerData;

namespace Data {
class ChatFilter;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Core::Focus {

using Logic::Mode;

inline constexpr auto kListId = FilterId(-0x0F0C);
inline constexpr auto kBadgeListId = FilterId(-0x0F0D);

[[nodiscard]] bool Active();
[[nodiscard]] rpl::producer<bool> ActiveValue();
[[nodiscard]] Logic::Status CurrentStatus();
[[nodiscard]] rpl::producer<Logic::Status> StatusValue();
[[nodiscard]] bool UnlockPending();

[[nodiscard]] const Logic::Settings &CurrentSettings();
[[nodiscard]] rpl::producer<> SettingsChanges();
void UpdateSettings(Fn<void(Logic::Settings&)> change);
void StartNow();
void StartUnlock();

[[nodiscard]] Mode FolderMode(const Data::ChatFilter &filter);
[[nodiscard]] Mode HistoryMode(not_null<History*> history);
[[nodiscard]] bool Allowed(not_null<PeerData*> peer);
[[nodiscard]] bool Notify(not_null<PeerData*> peer);
[[nodiscard]] bool ShownInList(not_null<History*> history, FilterId listId);
void RefreshLists(not_null<Main::Session*> session);

[[nodiscard]] FilterId ForcedFilterId();
[[nodiscard]] QString StatusLabel(Logic::Status status);
[[nodiscard]] QString PhaseToast(Logic::Status was, Logic::Status now);
[[nodiscard]] QString BlockedText();
[[nodiscard]] QString CommandPath();

[[nodiscard]] int32 ApiId();
[[nodiscard]] QString ApiHash();

} // namespace Core::Focus
