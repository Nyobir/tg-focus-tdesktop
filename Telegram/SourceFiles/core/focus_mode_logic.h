/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <algorithm>
#include <map>

namespace Core::Focus::Logic {

enum class Mode {
	Show,
	Mute,
	Hide,
};

struct Settings {
	bool scheduleEnabled = false;
	int scheduleDays = 0x1F;
	int scheduleStart = 9 * 60;
	int scheduleEnd = 18 * 60;
	bool syncSystem = true;
	int unlockDelayMinutes = 10;
	bool starsEnabled = false;
	int starsAmount = 50;
	quint64 starsPeerId = 0;
	bool privateAlwaysShown = true;
	Mode otherChats = Mode::Hide;
	bool foldersInitialized = false;
	std::map<int, Mode> folders;

	friend inline bool operator==(
		const Settings &,
		const Settings &) = default;
};

struct Runtime {
	bool manual = false;
	qint64 unlockAt = 0;
	qint64 pausedUntil = 0;
	qint64 systemIgnoredChange = 0;
	bool systemIgnoredOn = false;

	friend inline bool operator==(const Runtime &, const Runtime &) = default;
};

struct SystemState {
	bool on = false;
	bool off = false;
	qint64 changed = 0;
};

struct Status {
	bool active = false;
	int minutesLeft = 0;

	friend inline bool operator==(const Status &, const Status &) = default;
};

struct Evaluation {
	Status status;
	bool sourceOn = false;
	qint64 windowEnd = 0;
};

[[nodiscard]] inline SystemState ParseSystemState(
		const QByteArray &content,
		qint64 changed) {
	const auto state = QString::fromUtf8(content).trimmed();
	return {
		.on = (state == QStringLiteral("on")),
		.off = (state == QStringLiteral("off")),
		.changed = changed,
	};
}

[[nodiscard]] inline int MinutesLeft(qint64 now, qint64 till) {
	return int((std::max(till - now, qint64(1)) + 59) / 60);
}

[[nodiscard]] inline qint64 ScheduleWindowEnd(
		const Settings &settings,
		const QDateTime &now) {
	const auto start = settings.scheduleStart;
	const auto end = settings.scheduleEnd;
	if (!settings.scheduleEnabled || start == end) {
		return 0;
	}
	const auto minute = now.time().hour() * 60 + now.time().minute();
	const auto day = now.date().dayOfWeek() - 1;
	const auto dayOn = [&](int index) {
		return (settings.scheduleDays & (1 << ((index + 7) % 7))) != 0;
	};
	const auto endAt = [&](int addDays) {
		return QDateTime(
			now.date().addDays(addDays),
			QTime(end / 60, end % 60)).toSecsSinceEpoch();
	};
	if (start < end) {
		return (dayOn(day) && minute >= start && minute < end)
			? endAt(0)
			: 0;
	} else if (minute >= start) {
		return dayOn(day) ? endAt(1) : 0;
	} else if (minute < end) {
		return dayOn(day - 1) ? endAt(0) : 0;
	}
	return 0;
}

[[nodiscard]] inline Evaluation Evaluate(
		const Settings &settings,
		const Runtime &runtime,
		const SystemState &system,
		const QDateTime &now) {
	const auto secs = now.toSecsSinceEpoch();
	const auto windowEnd = ScheduleWindowEnd(settings, now);
	const auto scheduleOn = windowEnd && (secs >= runtime.pausedUntil);
	const auto systemFresh = settings.syncSystem
		&& (system.changed != runtime.systemIgnoredChange);
	const auto sourceOn = runtime.manual
		|| scheduleOn
		|| (systemFresh && system.on);
	auto result = Evaluation{ .sourceOn = sourceOn, .windowEnd = windowEnd };
	if (runtime.unlockAt) {
		result.status = (secs < runtime.unlockAt)
			? Status{ true, MinutesLeft(secs, runtime.unlockAt) }
			: Status{ sourceOn, 0 };
	} else if (sourceOn) {
		result.status = { true, 0 };
	} else if (systemFresh && system.off) {
		const auto till = system.changed
			+ qint64(settings.unlockDelayMinutes) * 60;
		if (secs < till) {
			result.status = { true, MinutesLeft(secs, till) };
		}
	}
	return result;
}

[[nodiscard]] inline bool UnlockFinished(
		const Runtime &runtime,
		const QDateTime &now) {
	return runtime.unlockAt
		&& (now.toSecsSinceEpoch() >= runtime.unlockAt);
}

[[nodiscard]] inline Runtime StartUnlock(
		const Settings &settings,
		Runtime runtime,
		qint64 now) {
	if (!runtime.unlockAt) {
		runtime.unlockAt = now + qint64(settings.unlockDelayMinutes) * 60;
	}
	return runtime;
}

[[nodiscard]] inline Runtime FinishUnlock(
		Runtime runtime,
		const SystemState &system,
		qint64 windowEnd) {
	runtime.manual = false;
	runtime.unlockAt = 0;
	if (windowEnd) {
		runtime.pausedUntil = windowEnd;
	}
	runtime.systemIgnoredChange = system.changed;
	runtime.systemIgnoredOn = system.on;
	return runtime;
}

[[nodiscard]] inline Runtime TrackSystem(
		Runtime runtime,
		const SystemState &system) {
	if (!runtime.systemIgnoredOn
		|| system.changed == runtime.systemIgnoredChange) {
		return runtime;
	} else if (system.off) {
		runtime.systemIgnoredChange = system.changed;
	}
	runtime.systemIgnoredOn = false;
	return runtime;
}

[[nodiscard]] inline Mode MostPermissive(Mode a, Mode b) {
	return Mode(std::min(int(a), int(b)));
}

[[nodiscard]] inline bool IsFocusFolderTitle(const QString &title) {
	const auto lower = title.toLower();
	return lower.contains(QStringLiteral("фокус"))
		|| lower.contains(QStringLiteral("focus"));
}

[[nodiscard]] inline QString ModeKey(Mode mode) {
	switch (mode) {
	case Mode::Show: return QStringLiteral("show");
	case Mode::Mute: return QStringLiteral("mute");
	case Mode::Hide: return QStringLiteral("hide");
	}
	return QString();
}

[[nodiscard]] inline Mode ModeFromKey(const QString &key) {
	return (key == QStringLiteral("mute"))
		? Mode::Mute
		: (key == QStringLiteral("hide"))
		? Mode::Hide
		: Mode::Show;
}

[[nodiscard]] inline QByteArray SerializeSettings(const Settings &s) {
	auto folders = QJsonObject();
	for (const auto &[id, mode] : s.folders) {
		folders.insert(QString::number(id), ModeKey(mode));
	}
	return QJsonDocument(QJsonObject{
		{ "scheduleEnabled", s.scheduleEnabled },
		{ "scheduleDays", s.scheduleDays },
		{ "scheduleStart", s.scheduleStart },
		{ "scheduleEnd", s.scheduleEnd },
		{ "syncSystem", s.syncSystem },
		{ "unlockDelayMinutes", s.unlockDelayMinutes },
		{ "starsEnabled", s.starsEnabled },
		{ "starsAmount", s.starsAmount },
		{ "starsPeerId", QString::number(s.starsPeerId) },
		{ "privateAlwaysShown", s.privateAlwaysShown },
		{ "otherChats", ModeKey(s.otherChats) },
		{ "foldersInitialized", s.foldersInitialized },
		{ "folders", folders },
	}).toJson();
}

[[nodiscard]] inline Settings ParseSettings(const QByteArray &json) {
	const auto o = QJsonDocument::fromJson(json).object();
	auto s = Settings();
	if (o.isEmpty()) {
		return s;
	}
	const auto minute = [&](const char *key, int fallback) {
		return std::clamp(o.value(key).toInt(fallback), 0, 24 * 60 - 1);
	};
	s.scheduleEnabled = o.value("scheduleEnabled").toBool(s.scheduleEnabled);
	s.scheduleDays = o.value("scheduleDays").toInt(s.scheduleDays) & 0x7F;
	s.scheduleStart = minute("scheduleStart", s.scheduleStart);
	s.scheduleEnd = minute("scheduleEnd", s.scheduleEnd);
	s.syncSystem = o.value("syncSystem").toBool(s.syncSystem);
	s.unlockDelayMinutes = std::clamp(
		o.value("unlockDelayMinutes").toInt(s.unlockDelayMinutes),
		0,
		24 * 60);
	s.starsEnabled = o.value("starsEnabled").toBool(s.starsEnabled);
	s.starsAmount = std::max(o.value("starsAmount").toInt(s.starsAmount), 1);
	s.starsPeerId = o.value("starsPeerId").toString().toULongLong();
	s.privateAlwaysShown = o.value("privateAlwaysShown").toBool(
		s.privateAlwaysShown);
	s.otherChats = ModeFromKey(o.value("otherChats").toString(
		ModeKey(s.otherChats)));
	s.foldersInitialized = o.value("foldersInitialized").toBool();
	const auto folders = o.value("folders").toObject();
	for (auto i = folders.begin(); i != folders.end(); ++i) {
		s.folders[i.key().toInt()] = ModeFromKey(i.value().toString());
	}
	return s;
}

[[nodiscard]] inline QByteArray SerializeRuntime(const Runtime &r) {
	return QJsonDocument(QJsonObject{
		{ "manual", r.manual },
		{ "unlockAt", QString::number(r.unlockAt) },
		{ "pausedUntil", QString::number(r.pausedUntil) },
		{ "systemIgnoredChange", QString::number(r.systemIgnoredChange) },
		{ "systemIgnoredOn", r.systemIgnoredOn },
	}).toJson();
}

[[nodiscard]] inline Runtime ParseRuntime(const QByteArray &json) {
	const auto o = QJsonDocument::fromJson(json).object();
	const auto number = [&](const char *key) {
		return o.value(key).toString().toLongLong();
	};
	return {
		.manual = o.value("manual").toBool(),
		.unlockAt = number("unlockAt"),
		.pausedUntil = number("pausedUntil"),
		.systemIgnoredChange = number("systemIgnoredChange"),
		.systemIgnoredOn = o.value("systemIgnoredOn").toBool(),
	};
}

struct ApiKeys {
	int id = 0;
	QString hash;
};

[[nodiscard]] inline ApiKeys ParseApiEnv(const QByteArray &content) {
	auto result = ApiKeys();
	const auto lines = QString::fromUtf8(content).split('\n');
	for (const auto &raw : lines) {
		const auto line = raw.trimmed();
		auto value = line.section('=', 1).trimmed();
		if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
			value = value.mid(1, value.size() - 2);
		}
		if (line.startsWith(QStringLiteral("TG_API_ID="))) {
			result.id = value.toInt();
		} else if (line.startsWith(QStringLiteral("TG_API_HASH="))) {
			result.hash = value;
		}
	}
	return result;
}

} // namespace Core::Focus::Logic
