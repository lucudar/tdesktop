/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class Controller;
} // namespace Window

namespace Window::Theme {
struct CloudListColors;
} // namespace Window::Theme

namespace Telewhite {

enum class Theme {
	TelegramDay,
	TelewhiteDay,
	Sakura,
	Mint,
	TelegramNight,
	TelewhiteNight,
	Ocean,
	Emerald,
	Sunset,
	Cherry,
	Graphite,
	Amoled,
	Custom,
};

[[nodiscard]] std::vector<Theme> LightThemes();
[[nodiscard]] std::vector<Theme> DarkThemes();

[[nodiscard]] rpl::producer<QString> ThemeName(Theme theme);
[[nodiscard]] QString ThemeNameNow(Theme theme);
[[nodiscard]] Window::Theme::CloudListColors ThemeColors(Theme theme);

[[nodiscard]] Theme CurrentTheme();
[[nodiscard]] rpl::producer<Theme> CurrentThemeValue();

void ApplyTheme(not_null<Window::Controller*> window, Theme theme);

// On the first launch of Telewhite switches the default Telegram themes
// to the Telewhite ones: the violet light palette and the violet night.
void ApplyDefaultTheme();

} // namespace Telewhite
