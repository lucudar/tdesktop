/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Telewhite {

struct FakeCity {
	QString name;
	QPointF point;
};

[[nodiscard]] const std::vector<FakeCity> &FakeCities();

// Name of the chosen city or the custom coordinates.
[[nodiscard]] QString FakeLocationName();
[[nodiscard]] rpl::producer<QString> FakeLocationNameValue();

[[nodiscard]] std::optional<QPointF> CustomFakePoint();
void SetCustomFakePoint(QPointF point);

// Parses "55.7558, 37.6173".
[[nodiscard]] std::optional<QPointF> ParseFakePoint(const QString &text);
[[nodiscard]] QString FormatFakePoint(QPointF point);

void SetupFakeLocation();

} // namespace Telewhite
