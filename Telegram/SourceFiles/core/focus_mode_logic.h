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

namespace Core::Focus::Logic {

inline constexpr auto kUnlockDelaySeconds = 10 * 60;

[[nodiscard]] inline bool StateActive(
		const QByteArray &content,
		qint64 secondsSinceChange) {
	const auto state = QString::fromUtf8(content).trimmed();
	if (state == QStringLiteral("on")) {
		return true;
	} else if (state != QStringLiteral("off")) {
		return false;
	}
	return secondsSinceChange < kUnlockDelaySeconds;
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
