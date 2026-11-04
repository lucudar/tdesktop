/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_icon.h"

#include "core/application.h"
#include "mainwindow.h"
#include "telewhite/telewhite_mods.h"
#include "tray.h"
#include "window/window_controller.h"
#include "styles/style_basic.h"

namespace Telewhite {
namespace {

// Hue of the original violet logo.
constexpr auto kOriginalHue = 252;

// Accents close to the original are drawn with the original logo.
constexpr auto kOriginalHueSkip = 10;

// Telegram blue accents keep the violet logo as well,
// so that Telewhite is never confused with the official app.
constexpr auto kBlueHueMin = 175;
constexpr auto kBlueHueMax = 235;

// Gray accents (white, black) keep the original logo.
constexpr auto kMinSaturation = 40;

struct State {
	bool started = false;
	int hue = -1;
	int generation = 0;
	QImage logo;
	QImage logoNoMargin;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] int WantedHue() {
	if (!Enabled(Mod::ThemedIcon)) {
		return -1;
	}
	const auto accent = st::windowBgActive->c.toHsv();
	const auto hue = accent.hsvHue();
	if (hue < 0 || accent.hsvSaturation() < kMinSaturation) {
		return -1;
	} else if (hue >= kBlueHueMin && hue <= kBlueHueMax) {
		return -1;
	} else if (std::abs(hue - kOriginalHue) <= kOriginalHueSkip) {
		return -1;
	}
	return (hue / 2) * 2;
}

[[nodiscard]] QImage Tinted(const QString &path, int hue) {
	auto result = QImage(path).convertToFormat(QImage::Format_ARGB32);
	const auto shift = hue - kOriginalHue;
	for (auto y = 0; y != result.height(); ++y) {
		const auto line = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (auto x = 0; x != result.width(); ++x) {
			const auto pixel = line[x];
			if (!qAlpha(pixel)) {
				continue;
			}
			auto color = QColor::fromRgba(pixel);
			auto h = 0;
			auto s = 0;
			auto v = 0;
			auto a = 0;
			color.getHsv(&h, &s, &v, &a);
			if (h < 0) {
				continue;
			}
			color.setHsv((h + shift + 720) % 360, s, v, a);
			line[x] = color.rgba();
		}
	}
	return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void Validate() {
	auto &state = Current();
	if (!state.started) {
		return;
	}
	const auto hue = WantedHue();
	if (state.hue == hue) {
		return;
	}
	state.hue = hue;
	++state.generation;
	if (hue < 0) {
		state.logo = QImage();
		state.logoNoMargin = QImage();
	} else {
		state.logo = Tinted(u":/gui/art/logo_256.png"_q, hue);
		state.logoNoMargin = Tinted(
			u":/gui/art/logo_256_no_margin.png"_q,
			hue);
	}
}

void Refresh() {
	static auto applied = 0;
	Validate();
	const auto generation = Current().generation;
	if (applied == generation) {
		return;
	}
	applied = generation;
	Core::App().refreshApplicationIcon();
	Core::App().enumerateWindows([](not_null<Window::Controller*> window) {
		window->widget()->updateWindowIcon();
	});
	Core::App().tray().updateIconCounters();
}

} // namespace

const QImage *ThemedLogo(bool noMargin) {
	Validate();
	const auto &state = Current();
	if (state.hue < 0) {
		return nullptr;
	}
	return noMargin ? &state.logoNoMargin : &state.logo;
}

int ThemedLogoGeneration() {
	Validate();
	return Current().generation;
}

void SetupThemedIcon(rpl::lifetime &lifetime) {
	Current().started = true;
	rpl::merge(
		style::PaletteChanged(),
		EnabledValue(Mod::ThemedIcon) | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([] {
		// Let the palette settle, windows may be not created yet.
		crl::on_main([] {
			if (Core::IsAppLaunched()) {
				Refresh();
			}
		});
	}, lifetime);
}

} // namespace Telewhite
