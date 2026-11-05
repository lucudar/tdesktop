/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

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
	EditHistory,
	ForwardNoAuthor,
	MultiForward,
	LocalPins,
	HideSponsored,
	HideEmojiStatus,
	PreciseLastSeen,
	RegistrationDate,
	Snowfall,
	StreamerMode,
	ConfirmStickers,
	ConfirmGifs,
	ConfirmVoice,
	ThemedIcon,
	NoSpoilers,
	NoGreetingSticker,
	NoBubbleTails,
	AuthorOnline,
	HideStoryRings,
	SnowfallChats,
	AntiCaps,
	BypassRestrictions,
	TranslateChats,
	FakeLocation,
	FakeLocationAuto,
	FakeLocationWalk,
	KeepOwnDeleted,
	KeepTimerMessages,
	EditHistoryInline,
	ExcludeGroups,
	ExcludeBots,
	ExcludeChannels,
	EditsInExcluded,
	NowPlaying,
	NowPlayingSpotify,
	NowPlayingYandex,
	NowPlayingVk,
	NowPlayingOther,

	kCount,
};

// Numeric settings of the mods.
enum class Option {
	DeletedTransparency, // Percents, 0 - 90.
	VersionsTransparency, // Percents, 0 - 90.
	AutoClear, // AutoClearPeriod.
	SnowDensity, // 0 - 3.
	SnowSpeed, // 0 - 3.
	StoryRingColor, // Index in StoryRingColors(), 0 - default.
	Translator, // TranslatorKind.
	FakeCity, // Index in the cities list, -1 - custom point.

	kCount,
};

enum class AutoClearPeriod {
	Never,
	Day,
	Week,
	Month,
	ThreeMonths,
	Year,
};

enum class TranslatorKind {
	Telegram,
	Google,
};

void LoadMods(not_null<Core::Settings*> settings);

[[nodiscard]] bool Enabled(Mod mod);
[[nodiscard]] rpl::producer<bool> EnabledValue(Mod mod);
void SetEnabled(Mod mod, bool enabled);

// Sub-options are not counted as separate mods in the settings banner.
[[nodiscard]] bool CountedInBanner(Mod mod);

[[nodiscard]] int OptionNow(Option option);
[[nodiscard]] rpl::producer<int> OptionValue(Option option);
void SetOption(Option option, int value);

[[nodiscard]] TimeId AutoClearSeconds(AutoClearPeriod period);

// Ghost mode keeps "listened" of voice / round messages local,
// but self-destructing media are still reported as opened.
[[nodiscard]] bool SkipContentsRead(not_null<HistoryItem*> item);

// Messages deleted by the other side, but kept by "Save deleted" mod.
// They are painted faded with a small trash can near the time.
bool MarkDeleted(not_null<const HistoryItem*> item);
[[nodiscard]] bool IsMarkedDeleted(not_null<const HistoryItem*> item);
[[nodiscard]] TimeId MarkedDeletedAt(not_null<const HistoryItem*> item);

[[nodiscard]] QDate EstimateRegistrationDate(uint64 userId);
[[nodiscard]] QString RegistrationDateText(uint64 userId);
[[nodiscard]] QString FormatExactDateTime(TimeId date);

} // namespace Telewhite
