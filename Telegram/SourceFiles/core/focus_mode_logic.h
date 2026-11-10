/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <algorithm>

namespace Core::Focus::Logic {

inline constexpr auto kUnlockDelaySeconds = 10 * 60;

struct Status {
	bool active = false;
	int minutesLeft = 0; // > 0 while restrictions linger after "off".

	friend inline bool operator==(const Status &, const Status &) = default;
};

[[nodiscard]] inline Status ParseState(
		const QByteArray &content,
		qint64 secondsSinceChange) {
	const auto state = QString::fromUtf8(content).trimmed();
	if (state == QStringLiteral("on")) {
		return { .active = true };
	} else if (state != QStringLiteral("off")
		|| secondsSinceChange >= kUnlockDelaySeconds) {
		return {};
	}
	const auto left = kUnlockDelaySeconds - std::max(secondsSinceChange, qint64());
	return { .active = true, .minutesLeft = int((left + 59) / 60) };
}

[[nodiscard]] inline bool StateActive(
		const QByteArray &content,
		qint64 secondsSinceChange) {
	return ParseState(content, secondsSinceChange).active;
}

// Text for the bar above the chat list, empty when focus is off.
[[nodiscard]] inline QString StatusLabel(Status status) {
	return !status.active
		? QString()
		: !status.minutesLeft
		? QStringLiteral("🎯 Фокус")
		: QStringLiteral("🎯 Фокус ще %1 хв").arg(status.minutesLeft);
}

// Toast when the phase (off / on / unlocking) changes, empty otherwise.
[[nodiscard]] inline QString PhaseToast(Status was, Status now) {
	const auto phase = [](Status s) {
		return !s.active ? 0 : !s.minutesLeft ? 1 : 2;
	};
	if (phase(was) == phase(now)) {
		return QString();
	}
	switch (phase(now)) {
	case 0: return QStringLiteral("Фокус вимкнено.");
	case 1: return QStringLiteral("Фокус увімкнено.");
	}
	return QStringLiteral("Фокус вимкнеться за %1 хв.").arg(now.minutesLeft);
}

[[nodiscard]] inline bool IsFocusFolderTitle(const QString &title) {
	const auto lower = title.toLower();
	return lower.contains(QStringLiteral("фокус"))
		|| lower.contains(QStringLiteral("focus"));
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
