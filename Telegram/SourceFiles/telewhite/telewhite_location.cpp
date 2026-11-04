/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_location.h"

#include "base/random.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/current_geo_location.h"
#include "telewhite/telewhite_mods.h"

#include <cmath>

namespace Telewhite {
namespace {

constexpr auto kCustomPointKey = "telewhite-fake-location-point";

// "Walk mode": a slow random walk around the chosen point.
constexpr auto kWalkSpeed = 1.3; // Meters per second.
constexpr auto kWalkRadius = 1500.; // Meters.
constexpr auto kMaxWalkStep = crl::time(10 * 60 * 1000);
constexpr auto kMetersInDegree = 111320.;
constexpr auto kPi = 3.14159265358979323846;

struct Walk {
	QPointF center;
	QPointF offset; // Meters to the east (x) and to the north (y).
	double heading = 0.;
	crl::time updated = 0;
};

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] double RandomUnit() {
	return base::RandomValue<uint32>() / double(0xFFFFFFFFU);
}

[[nodiscard]] Walk &CurrentWalk() {
	static auto result = Walk();
	return result;
}

rpl::event_stream<> &CustomPointChanges() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QPointF ChosenPoint() {
	const auto &cities = FakeCities();
	const auto index = OptionNow(Option::FakeCity);
	if (index < 0) {
		if (const auto custom = CustomFakePoint()) {
			return *custom;
		}
	}
	return cities[std::clamp(index, 0, int(cities.size()) - 1)].point;
}

[[nodiscard]] QPointF Walked(QPointF center) {
	auto &walk = CurrentWalk();
	const auto now = crl::now();
	if (walk.center != center || !walk.updated) {
		walk = Walk{
			.center = center,
			.heading = RandomUnit() * 2. * kPi,
			.updated = now,
		};
	}
	const auto elapsed = std::min(now - walk.updated, kMaxWalkStep);
	walk.updated = now;
	auto left = kWalkSpeed * elapsed / 1000.;
	while (left > 0.) {
		const auto step = std::min(left, 25.);
		left -= step;
		walk.heading += (RandomUnit() - 0.5) * 0.5;
		const auto distance = std::hypot(walk.offset.x(), walk.offset.y());
		if (distance > kWalkRadius) {
			// Turn back to the center.
			walk.heading = std::atan2(-walk.offset.y(), -walk.offset.x());
		}
		walk.offset += QPointF(
			std::cos(walk.heading) * step,
			std::sin(walk.heading) * step);
	}
	const auto lat = center.x() + walk.offset.y() / kMetersInDegree;
	const auto cosine = std::max(std::cos(center.x() * kPi / 180.), 0.01);
	const auto lon = center.y()
		+ walk.offset.x() / (kMetersInDegree * cosine);
	return QPointF(lat, lon);
}

[[nodiscard]] Core::GeoLocation FakeLocation() {
	const auto center = ChosenPoint();
	const auto point = Enabled(Mod::FakeLocationWalk)
		? Walked(center)
		: center;
	return {
		.point = point,
		.bounds = QRectF(point, QSizeF()),
		.accuracy = Core::GeoLocationAccuracy::Exact,
	};
}

} // namespace

const std::vector<FakeCity> &FakeCities() {
	static const auto result = std::vector<FakeCity>{
		{ U("Москва"), { 55.7558, 37.6173 } },
		{ U("Санкт-Петербург"), { 59.9343, 30.3351 } },
		{ U("Новосибирск"), { 55.0084, 82.9357 } },
		{ U("Екатеринбург"), { 56.8389, 60.6057 } },
		{ U("Казань"), { 55.7961, 49.1064 } },
		{ U("Сочи"), { 43.5855, 39.7231 } },
		{ U("Минск"), { 53.9006, 27.5590 } },
		{ U("Киев"), { 50.4501, 30.5234 } },
		{ U("Алматы"), { 43.2220, 76.8512 } },
		{ U("Ташкент"), { 41.2995, 69.2401 } },
		{ U("Тбилиси"), { 41.7151, 44.8271 } },
		{ U("Ереван"), { 40.1792, 44.4991 } },
		{ U("Стамбул"), { 41.0082, 28.9784 } },
		{ U("Дубай"), { 25.2048, 55.2708 } },
		{ U("Берлин"), { 52.5200, 13.4050 } },
		{ U("Лондон"), { 51.5072, -0.1276 } },
		{ U("Париж"), { 48.8566, 2.3522 } },
		{ U("Нью-Йорк"), { 40.7128, -74.0060 } },
		{ U("Токио"), { 35.6762, 139.6503 } },
		{ U("Бали"), { -8.6500, 115.2167 } },
	};
	return result;
}

QString FakeLocationName() {
	const auto index = OptionNow(Option::FakeCity);
	if (index < 0) {
		if (const auto custom = CustomFakePoint()) {
			return FormatFakePoint(*custom);
		}
	}
	const auto &cities = FakeCities();
	return cities[std::clamp(index, 0, int(cities.size()) - 1)].name;
}

rpl::producer<QString> FakeLocationNameValue() {
	return rpl::merge(
		OptionValue(Option::FakeCity) | rpl::to_empty,
		CustomPointChanges().events()
	) | rpl::map([] {
		return FakeLocationName();
	});
}

std::optional<QPointF> CustomFakePoint() {
	const auto serialized = Core::App().settings().readPref<QByteArray>(
		kCustomPointKey);
	return ParseFakePoint(QString::fromLatin1(serialized));
}

void SetCustomFakePoint(QPointF point) {
	Core::App().settings().writePref<QByteArray>(
		kCustomPointKey,
		FormatFakePoint(point).toLatin1());
	Core::App().saveSettingsDelayed();
	CustomPointChanges().fire({});
}

std::optional<QPointF> ParseFakePoint(const QString &text) {
	auto parts = text.split(QChar(','), Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		parts = text.split(QChar(' '), Qt::SkipEmptyParts);
	}
	if (parts.size() != 2) {
		return std::nullopt;
	}
	auto latOk = false;
	auto lonOk = false;
	const auto lat = parts[0].trimmed().toDouble(&latOk);
	const auto lon = parts[1].trimmed().toDouble(&lonOk);
	if (!latOk
		|| !lonOk
		|| std::abs(lat) > 90.
		|| std::abs(lon) > 180.) {
		return std::nullopt;
	}
	return QPointF(lat, lon);
}

QString FormatFakePoint(QPointF point) {
	return QString::number(point.x(), 'f', 5)
		+ u", "_q
		+ QString::number(point.y(), 'f', 5);
}

void SetupFakeLocation() {
	Core::SetCurrentGeoLocationOverride([](bool failed)
	-> std::optional<Core::GeoLocation> {
		if (!Enabled(Mod::FakeLocation)
			|| (Enabled(Mod::FakeLocationAuto) && !failed)) {
			return std::nullopt;
		}
		return FakeLocation();
	});
}

} // namespace Telewhite
