/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Telewhite {

// Application logo tinted in the theme accent color.
// nullptr - the original violet logo should be used.
[[nodiscard]] const QImage *ThemedLogo(bool noMargin);

// Changes each time the themed logo changes, for the icon caches.
[[nodiscard]] int ThemedLogoGeneration();

void SetupThemedIcon(rpl::lifetime &lifetime);

} // namespace Telewhite
