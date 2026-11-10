/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/focus_mode.h"

#include "base/timer.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

namespace Core::Focus {
namespace {

constexpr auto kPollTimeout = crl::time(1000);

[[nodiscard]] QString FocusPath(const QString &name) {
	return QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + u"/tg-focus/"_q + name;
}

[[nodiscard]] QByteArray ReadFocusFile(const QString &name) {
	auto file = QFile(FocusPath(name));
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void WriteFocusFile(const QString &name, const QByteArray &content) {
	QDir().mkpath(FocusPath(QString()));
	auto file = QSaveFile(FocusPath(name));
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(content) != content.size()
		|| !file.commit()) {
		LOG(("Focus Error: could not write tg-focus/%1.").arg(name));
	}
}

[[nodiscard]] Logic::SystemState ReadSystemState() {
	auto file = QFile(FocusPath(u"state"_q));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return Logic::ParseSystemState(
		file.read(16),
		QFileInfo(file).lastModified().toSecsSinceEpoch());
}

class Watcher final {
public:
	Watcher();

	[[nodiscard]] const Logic::Settings &settings() const;
	[[nodiscard]] rpl::producer<> settingsChanges() const;
	[[nodiscard]] Logic::Status status() const;
	[[nodiscard]] rpl::producer<Logic::Status> statusValue() const;
	[[nodiscard]] bool unlockPending() const;

	void updateSettings(Fn<void(Logic::Settings&)> change);
	void startNow();
	void startUnlock();

private:
	void check();
	void setRuntime(Logic::Runtime runtime);

	Logic::Settings _settings;
	Logic::Runtime _runtime;
	rpl::variable<Logic::Status> _status;
	rpl::event_stream<> _settingsChanges;
	base::Timer _timer;
	rpl::lifetime _lifetime;

};

Watcher::Watcher()
: _settings(Logic::ParseSettings(ReadFocusFile(u"settings.json"_q)))
, _runtime(Logic::ParseRuntime(ReadFocusFile(u"runtime.json"_q)))
, _timer([=] { check(); }) {
	check();
	_timer.callEach(kPollTimeout);
	_status.changes(
	) | rpl::map([](const Logic::Status &status) {
		return status.active;
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([] {
		if (IsAppLaunched()) {
			App().domain().notifyUnreadBadgeChanged();
		}
	}, _lifetime);
}

const Logic::Settings &Watcher::settings() const {
	return _settings;
}

rpl::producer<> Watcher::settingsChanges() const {
	return _settingsChanges.events();
}

Logic::Status Watcher::status() const {
	return _status.current();
}

rpl::producer<Logic::Status> Watcher::statusValue() const {
	return _status.value();
}

bool Watcher::unlockPending() const {
	return _runtime.unlockAt != 0;
}

void Watcher::updateSettings(Fn<void(Logic::Settings&)> change) {
	auto updated = _settings;
	change(updated);
	if (updated == _settings) {
		return;
	}
	_settings = std::move(updated);
	WriteFocusFile(
		u"settings.json"_q,
		Logic::SerializeSettings(_settings));
	check();
	_settingsChanges.fire({});
}

void Watcher::startNow() {
	auto runtime = _runtime;
	runtime.manual = true;
	runtime.unlockAt = 0;
	setRuntime(runtime);
}

void Watcher::startUnlock() {
	setRuntime(Logic::StartUnlock(
		_settings,
		_runtime,
		QDateTime::currentSecsSinceEpoch()));
}

void Watcher::setRuntime(Logic::Runtime runtime) {
	if (runtime == _runtime) {
		return;
	}
	_runtime = runtime;
	WriteFocusFile(u"runtime.json"_q, Logic::SerializeRuntime(_runtime));
	check();
}

void Watcher::check() {
	const auto system = ReadSystemState();
	const auto tracked = Logic::TrackSystem(_runtime, system);
	if (tracked != _runtime) {
		setRuntime(tracked);
		return;
	}
	const auto now = QDateTime::currentDateTime();
	const auto evaluation = Logic::Evaluate(_settings, _runtime, system, now);
	if (Logic::UnlockFinished(_runtime, now)) {
		setRuntime(Logic::FinishUnlock(
			_runtime,
			system,
			evaluation.windowEnd));
		return;
	}
	_status = evaluation.status;
}

[[nodiscard]] Watcher &Instance() {
	static const auto result = new Watcher();
	return *result;
}

[[nodiscard]] std::optional<Mode> FoldersMode(not_null<History*> history) {
	auto result = std::optional<Mode>();
	for (const auto &filter : history->owner().chatsFilters().list()) {
		if (filter.id() && filter.contains(history)) {
			const auto mode = FolderMode(filter);
			result = result ? Logic::MostPermissive(*result, mode) : mode;
		}
	}
	return result;
}

[[nodiscard]] const Logic::ApiKeys &ApiKeys() {
	static const auto result = [] {
		auto keys = Logic::ParseApiEnv(ReadFocusFile(u"api.env"_q));
		if (!keys.id) {
			LOG(("Focus Error: could not read tg-focus/api.env."));
		}
		return keys;
	}();
	return result;
}

} // namespace

bool Active() {
	return Instance().status().active;
}

rpl::producer<bool> ActiveValue() {
	return Instance().statusValue(
	) | rpl::map([](const Logic::Status &status) {
		return status.active;
	}) | rpl::distinct_until_changed();
}

Logic::Status CurrentStatus() {
	return Instance().status();
}

rpl::producer<Logic::Status> StatusValue() {
	return Instance().statusValue();
}

bool UnlockPending() {
	return Instance().unlockPending();
}

const Logic::Settings &CurrentSettings() {
	return Instance().settings();
}

rpl::producer<> SettingsChanges() {
	return Instance().settingsChanges();
}

void UpdateSettings(Fn<void(Logic::Settings&)> change) {
	Instance().updateSettings(std::move(change));
}

void StartNow() {
	Instance().startNow();
}

void StartUnlock() {
	Instance().startUnlock();
}

Mode FolderMode(const Data::ChatFilter &filter) {
	const auto &settings = CurrentSettings();
	const auto i = settings.folders.find(filter.id());
	if (i != end(settings.folders)) {
		return i->second;
	} else if (settings.foldersInitialized) {
		return settings.otherChats;
	}
	return Logic::IsFocusFolderTitle(filter.title().text.text)
		? Mode::Show
		: Mode::Hide;
}

Mode HistoryMode(not_null<History*> history) {
	const auto peer = history->peer;
	if (peer->isUser() && CurrentSettings().privateAlwaysShown) {
		return Mode::Show;
	}
	auto result = FoldersMode(history);
	const auto channel = peer->asChannel();
	const auto linked = (channel && channel->isMegagroup())
		? channel->discussionLink()
		: nullptr;
	if (const auto linkedHistory = linked
			? linked->owner().historyLoaded(linked)
			: nullptr) {
		if (const auto mode = FoldersMode(linkedHistory)) {
			result = result ? Logic::MostPermissive(*result, *mode) : *mode;
		}
	}
	return result.value_or(CurrentSettings().otherChats);
}

bool Allowed(not_null<PeerData*> peer) {
	return !Active()
		|| (HistoryMode(peer->owner().history(peer)) != Mode::Hide);
}

bool Notify(not_null<PeerData*> peer) {
	return !Active()
		|| (HistoryMode(peer->owner().history(peer)) == Mode::Show);
}

void RefreshLists(not_null<Main::Session*> session) {
	const auto owner = &session->data();
	auto pinned = std::vector<not_null<History*>>();
	for (const auto &key : owner->chatsList()->pinned()->order()) {
		if (const auto history = key.history()) {
			if (HistoryMode(history) != Mode::Hide) {
				pinned.push_back(history);
			}
		}
	}
	owner->chatsFilters().chatsList(kListId)->pinned()->applyList(pinned);

	auto histories = std::vector<not_null<History*>>();
	const auto collect = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				histories.push_back(history);
			}
		}
	};
	collect(owner->chatsList());
	if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
		collect(folder->chatsList());
	}
	for (const auto &history : histories) {
		owner->refreshChatListEntry(history);
	}
}

FilterId ForcedFilterId() {
	return Active() ? kListId : FilterId();
}

QString StatusLabel(Logic::Status status) {
	const auto minutes = QString::number(status.minutesLeft);
	return !status.active
		? QString()
		: !status.minutesLeft
		? tr::lng_focus_bar(tr::now)
		: tr::lng_focus_bar_unlocking(tr::now, lt_minutes, minutes);
}

QString PhaseToast(Logic::Status was, Logic::Status now) {
	const auto phase = [](Logic::Status status) {
		return !status.active ? 0 : !status.minutesLeft ? 1 : 2;
	};
	if (phase(was) == phase(now)) {
		return QString();
	}
	switch (phase(now)) {
	case 0: return tr::lng_focus_toast_off(tr::now);
	case 1: return tr::lng_focus_toast_on(tr::now);
	}
	return tr::lng_focus_toast_unlocking(
		tr::now,
		lt_minutes,
		QString::number(now.minutesLeft));
}

QString BlockedText() {
	return tr::lng_focus_blocked(tr::now);
}

QString CommandPath() {
	return u"~/Projects/tg-focus/bin/tg-focus"_q;
}

int32 ApiId() {
	return ApiKeys().id;
}

QString ApiHash() {
	return ApiKeys().hash;
}

} // namespace Core::Focus
