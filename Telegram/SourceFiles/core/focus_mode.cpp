/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/focus_mode.h"

#include "base/timer.h"
#include "core/application.h"
#include "core/focus_mode_logic.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>

namespace Core::Focus {
namespace {

constexpr auto kPollTimeout = crl::time(2000);

[[nodiscard]] QString StatePath() {
	return QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + u"/tg-focus/state"_q;
}

[[nodiscard]] bool ReadActive() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto changed = QFileInfo(file).lastModified();
	return Logic::StateActive(
		file.read(16),
		changed.secsTo(QDateTime::currentDateTime()));
}

class Watcher final {
public:
	Watcher() {
		_timer.callEach(kPollTimeout);
		_active.changes() | rpl::on_next([] {
			if (IsAppLaunched()) {
				App().domain().notifyUnreadBadgeChanged();
			}
		}, _lifetime);
	}

	[[nodiscard]] bool current() const {
		return _active.current();
	}
	[[nodiscard]] rpl::producer<bool> value() const {
		return _active.value();
	}

private:
	rpl::variable<bool> _active = ReadActive();
	base::Timer _timer{ [this] { _active = ReadActive(); } };
	rpl::lifetime _lifetime;

};

[[nodiscard]] Watcher &Instance() {
	static const auto result = new Watcher();
	return *result;
}

[[nodiscard]] const Data::ChatFilter *FindFolder(
		not_null<Main::Session*> session) {
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (Logic::IsFocusFolderTitle(filter.title().text.text)) {
			return &filter;
		}
	}
	return nullptr;
}

[[nodiscard]] bool InFolder(
		const Data::ChatFilter *folder,
		not_null<PeerData*> peer) {
	if (!folder) {
		return false;
	}
	const auto history = peer->owner().historyLoaded(peer.get());
	return history && folder->contains(history);
}

} // namespace

bool Active() {
	return Instance().current();
}

rpl::producer<bool> ActiveValue() {
	return Instance().value();
}

FilterId ForcedFilterId(not_null<Main::Session*> session) {
	if (!Active()) {
		return 0;
	}
	const auto folder = FindFolder(session);
	return folder ? folder->id() : 0;
}

bool Allowed(not_null<PeerData*> peer) {
	if (!Active() || peer->isUser()) {
		return true;
	}
	const auto folder = FindFolder(&peer->session());
	if (InFolder(folder, peer)) {
		return true;
	}
	const auto channel = peer->asChannel();
	const auto linked = (channel && channel->isMegagroup())
		? channel->discussionLink()
		: nullptr;
	return linked && InFolder(folder, linked);
}

QString BlockedText() {
	return u"Недоступно під час фокусу."_q;
}

namespace {

[[nodiscard]] const Logic::ApiKeys &RuntimeApiKeys() {
	static const auto result = [] {
		auto file = QFile(QStandardPaths::writableLocation(
			QStandardPaths::GenericDataLocation) + u"/tg-focus/api.env"_q);
		if (!file.open(QIODevice::ReadOnly)) {
			LOG(("Focus Error: could not read tg-focus/api.env."));
			return Logic::ApiKeys();
		}
		return Logic::ParseApiEnv(file.readAll());
	}();
	return result;
}

} // namespace

int32 ApiId() {
	return RuntimeApiKeys().id;
}

QString ApiHash() {
	return RuntimeApiKeys().hash;
}

} // namespace Core::Focus
