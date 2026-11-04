/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_themes.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include "telewhite/telewhite_amoled_theme.h"
#include "telewhite/telewhite_light_theme.h"
#include "ui/boxes/confirm_box.h"
#include "ui/style/style_palette_colorizer.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Telewhite {
namespace {

using namespace Window::Theme;

constexpr auto kDefaultThemeKey = "telewhite-default-theme"_cs;

constexpr auto kNightGreenPath = ":/gui/night-green.tdesktop-theme"_cs;

constexpr auto kFileThemes = std::array{
	Theme::TelewhiteDay,
	Theme::Sakura,
	Theme::Mint,
	Theme::Amoled,
};

constexpr auto kAccentThemes = std::array{
	Theme::TelewhiteNight,
	Theme::Ocean,
	Theme::Emerald,
	Theme::Sunset,
	Theme::Cherry,
};

[[nodiscard]] QColor Rgb(uint32 rgb) {
	return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

[[nodiscard]] bool SameRgb(const QColor &a, const QColor &b) {
	return (a.rgb() & 0xFFFFFF) == (b.rgb() & 0xFFFFFF);
}

[[nodiscard]] bool IsDarkTheme(Theme theme) {
	switch (theme) {
	case Theme::TelegramNight:
	case Theme::TelewhiteNight:
	case Theme::Ocean:
	case Theme::Emerald:
	case Theme::Sunset:
	case Theme::Cherry:
	case Theme::Graphite:
	case Theme::Amoled: return true;
	case Theme::TelegramDay:
	case Theme::TelewhiteDay:
	case Theme::Sakura:
	case Theme::Mint:
	case Theme::Custom: return false;
	}
	return false;
}

[[nodiscard]] QString FileName(Theme theme) {
	switch (theme) {
	case Theme::TelewhiteDay: return u"telewhite-light.tdesktop-palette"_q;
	case Theme::Sakura: return u"telewhite-sakura.tdesktop-palette"_q;
	case Theme::Mint: return u"telewhite-mint.tdesktop-palette"_q;
	case Theme::Amoled: return u"telewhite-amoled.tdesktop-theme"_q;
	default: return QString();
	}
}

[[nodiscard]] QByteArray FileContent(Theme theme) {
	const auto bytes = [](std::string_view data) {
		return QByteArray(data.data(), int(data.size()));
	};
	switch (theme) {
	case Theme::TelewhiteDay: return bytes(kLightPalette);
	case Theme::Sakura: return bytes(kSakuraPalette);
	case Theme::Mint: return bytes(kMintPalette);
	case Theme::Amoled: return QByteArray(
		reinterpret_cast<const char*>(kAmoledThemeData),
		int(sizeof(kAmoledThemeData)));
	default: return QByteArray();
	}
}

// Embedded Telegram theme the Telewhite theme is built from.
[[nodiscard]] EmbeddedType BaseType(Theme theme) {
	switch (theme) {
	case Theme::TelegramDay: return EmbeddedType::Default;
	case Theme::Graphite: return EmbeddedType::NightGreen;
	default: return EmbeddedType::Night;
	}
}

[[nodiscard]] std::optional<QColor> Accent(Theme theme) {
	switch (theme) {
	case Theme::TelewhiteNight: return Rgb(0x7a5ccf);
	case Theme::Ocean: return Rgb(0x2ab3cf);
	case Theme::Emerald: return Rgb(0x3dbd7d);
	case Theme::Sunset: return Rgb(0xe8834e);
	case Theme::Cherry: return Rgb(0xd9507f);
	case Theme::Graphite: return Rgb(0x8f74e6);
	default: return std::nullopt;
	}
}

[[nodiscard]] std::optional<EmbeddedScheme> Scheme(EmbeddedType type) {
	const auto schemes = EmbeddedThemes();
	const auto i = ranges::find(schemes, type, &EmbeddedScheme::type);
	return (i != end(schemes)) ? std::make_optional(*i) : std::nullopt;
}

[[nodiscard]] CloudListColors SimpleColors(
		QColor background,
		QColor sent,
		QColor received,
		QColor radio) {
	auto result = CloudListColors();
	result.sent = sent;
	result.received = received;
	result.radiobuttonActive = radio;
	result.radiobuttonInactive = sent;
	result.background = QImage(
		QSize(1, 1) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.background.fill(background);
	return result;
}

// Writes the bundled theme to tdata, Telegram applies themes from files.
[[nodiscard]] QString WriteThemeFile(Theme theme) {
	const auto content = FileContent(theme);
	const auto folder = cWorkingDir() + u"tdata"_q;
	QDir().mkpath(folder);
	const auto path = folder + u"/"_q + FileName(theme);
	auto file = QFile(path);
	if (content.isEmpty() || !file.open(QIODevice::WriteOnly)) {
		return QString();
	}
	const auto written = file.write(content);
	file.close();
	return (written == content.size()) ? path : QString();
}

void ApplyPath(const QString &path, bool dark) {
	if (IsNightMode() == dark) {
		ApplyDefaultWithPath(path);
	} else {
		ToggleNightMode(path);
	}
	KeepApplied();
}

void ApplyNow(Theme theme) {
	const auto dark = IsDarkTheme(theme);
	if (ranges::contains(kFileThemes, theme)) {
		if (const auto path = WriteThemeFile(theme); !path.isEmpty()) {
			ApplyPath(path, dark);
		}
		return;
	}
	const auto type = BaseType(theme);
	const auto scheme = Scheme(type);
	if (!scheme) {
		return;
	}
	auto &settings = Core::App().settings();
	if (const auto accent = Accent(theme)) {
		settings.themesAccentColors().set(type, *accent);
	} else {
		settings.themesAccentColors().clear(type);
	}
	settings.setSystemAccentColorEnabled(false);
	Core::App().saveSettingsDelayed();
	ApplyPath(scheme->path, dark);
}

[[nodiscard]] not_null<const tr::phrase<>*> ThemePhrase(Theme theme) {
	switch (theme) {
	case Theme::TelegramDay:
	case Theme::TelegramNight: return &tr::lng_telewhite_theme_telegram;
	case Theme::TelewhiteDay:
	case Theme::TelewhiteNight: return &tr::lng_telewhite_theme_telewhite;
	case Theme::Sakura: return &tr::lng_telewhite_theme_sakura;
	case Theme::Mint: return &tr::lng_telewhite_theme_mint;
	case Theme::Ocean: return &tr::lng_telewhite_theme_ocean;
	case Theme::Emerald: return &tr::lng_telewhite_theme_emerald;
	case Theme::Sunset: return &tr::lng_telewhite_theme_sunset;
	case Theme::Cherry: return &tr::lng_telewhite_theme_cherry;
	case Theme::Graphite: return &tr::lng_telewhite_theme_graphite;
	case Theme::Amoled: return &tr::lng_telewhite_theme_amoled;
	case Theme::Custom: return &tr::lng_telewhite_theme_custom;
	}
	Unexpected("Theme in Telewhite::ThemePhrase.");
}

} // namespace

std::vector<Theme> LightThemes() {
	return {
		Theme::TelegramDay,
		Theme::TelewhiteDay,
		Theme::Sakura,
		Theme::Mint,
	};
}

std::vector<Theme> DarkThemes() {
	return {
		Theme::TelegramNight,
		Theme::TelewhiteNight,
		Theme::Ocean,
		Theme::Emerald,
		Theme::Sunset,
		Theme::Cherry,
		Theme::Graphite,
		Theme::Amoled,
	};
}

rpl::producer<QString> ThemeName(Theme theme) {
	return (*ThemePhrase(theme))();
}

QString ThemeNameNow(Theme theme) {
	return (*ThemePhrase(theme))(tr::now);
}

CloudListColors ThemeColors(Theme theme) {
	switch (theme) {
	case Theme::TelewhiteDay: return SimpleColors(
		Rgb(0xab9ae6),
		Rgb(0xe6defd),
		Rgb(0xffffff),
		Rgb(0xffffff));
	case Theme::Sakura: return SimpleColors(
		Rgb(0xeba3bf),
		Rgb(0xfcdce9),
		Rgb(0xffffff),
		Rgb(0xffffff));
	case Theme::Mint: return SimpleColors(
		Rgb(0x8fd3bd),
		Rgb(0xd5faef),
		Rgb(0xffffff),
		Rgb(0xffffff));
	case Theme::Amoled: return SimpleColors(
		Rgb(0x000000),
		Rgb(0x2b5278),
		Rgb(0x1c1d1f),
		Rgb(0x5288c1));
	case Theme::Custom: return CloudListColors();
	default: break;
	}
	const auto scheme = Scheme(BaseType(theme));
	if (!scheme) {
		return CloudListColors();
	}
	const auto accent = Accent(theme);
	return accent
		? ColorsFromScheme(*scheme, ColorizerFrom(*scheme, *accent))
		: ColorsFromScheme(*scheme);
}

Theme CurrentTheme() {
	const auto background = Background();
	if (background->editingTheme()) {
		return Theme::Custom;
	}
	const auto &object = background->themeObject();
	if (object.cloud.id) {
		return Theme::Custom;
	}
	const auto &path = object.pathAbsolute;
	if (!IsEmbeddedTheme(path)) {
		const auto name = QFileInfo(path).fileName();
		for (const auto theme : kFileThemes) {
			if (name == FileName(theme)) {
				return theme;
			}
		}
		return Theme::Custom;
	}
	auto &settings = Core::App().settings();
	if (settings.systemAccentColorEnabled() && SystemAccentColor()) {
		return Theme::Custom;
	}
	const auto &accents = settings.themesAccentColors();
	if (path.isEmpty()) {
		return accents.get(EmbeddedType::Default)
			? Theme::Custom
			: Theme::TelegramDay;
	} else if (path == NightThemePath()) {
		const auto accent = accents.get(EmbeddedType::Night);
		if (!accent) {
			return Theme::TelegramNight;
		}
		for (const auto theme : kAccentThemes) {
			if (SameRgb(*Accent(theme), *accent)) {
				return theme;
			}
		}
	} else if (path == kNightGreenPath.utf16()) {
		const auto accent = accents.get(EmbeddedType::NightGreen);
		if (accent && SameRgb(*Accent(Theme::Graphite), *accent)) {
			return Theme::Graphite;
		}
	}
	return Theme::Custom;
}

rpl::producer<Theme> CurrentThemeValue() {
	return rpl::single(
		rpl::empty
	) | rpl::then(
		Background()->updates(
		) | rpl::filter([](const BackgroundUpdate &update) {
			return (update.type == BackgroundUpdate::Type::ApplyingTheme);
		}) | rpl::to_empty
	) | rpl::map(CurrentTheme) | rpl::distinct_until_changed();
}

void ApplyTheme(not_null<Window::Controller*> window, Theme theme) {
	if (theme == Theme::Custom) {
		return;
	} else if (Background()->editingTheme()) {
		window->show(Ui::MakeInformBox(
			tr::lng_theme_editor_cant_change_theme()));
		return;
	}
	if (IsNightMode() == IsDarkTheme(theme)) {
		ApplyNow(theme);
	} else {
		ToggleNightModeWithConfirmation(window, [=] { ApplyNow(theme); });
	}
}

void ApplyDefaultTheme() {
	static auto done = false;
	if (done) {
		return;
	}
	done = true;

	auto &settings = Core::App().settings();
	if (settings.readPref<bool>(kDefaultThemeKey, false)) {
		return;
	}
	settings.writePref<bool>(kDefaultThemeKey, true);
	Core::App().saveSettingsDelayed();

	const auto background = Background();
	if (background->editingTheme() || settings.systemAccentColorEnabled()) {
		return;
	}
	const auto object = background->themeObject();
	if (object.cloud.id) {
		return;
	}
	auto &accents = settings.themesAccentColors();
	if (!accents.get(EmbeddedType::Night)) {
		accents.set(EmbeddedType::Night, *Accent(Theme::TelewhiteNight));
		if (IsNightMode() && object.pathAbsolute == NightThemePath()) {
			ApplyDefaultWithPath(NightThemePath());
			KeepApplied();
		}
	}
	if (!IsNightMode()
		&& object.pathAbsolute.isEmpty()
		&& !accents.get(EmbeddedType::Default)) {
		if (const auto path = WriteThemeFile(Theme::TelewhiteDay)
			; !path.isEmpty()) {
			ApplyDefaultWithPath(path);
			KeepApplied();
		}
	}
}

} // namespace Telewhite
