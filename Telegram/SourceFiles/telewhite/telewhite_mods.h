/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {
class Settings;
} // namespace Core

namespace Telewhite {

enum class Mod {
	GhostRead,
	GhostTyping,
	GhostOnline,
	GhostStories,
	SaveDeleted,
	HideSponsored,
	HideEmojiStatus,
	PreciseLastSeen,
	RegistrationDate,
	Snowfall,

	kCount,
};

void LoadMods(not_null<Core::Settings*> settings);

[[nodiscard]] bool Enabled(Mod mod);
[[nodiscard]] rpl::producer<bool> EnabledValue(Mod mod);
void SetEnabled(Mod mod, bool enabled);

[[nodiscard]] QDate EstimateRegistrationDate(uint64 userId);
[[nodiscard]] QString RegistrationDateText(uint64 userId);

} // namespace Telewhite
