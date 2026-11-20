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
	GhostReadOnReply,
	TranslateBeforeSend,
	HidePremium,
	PollResults,
	StickersWithoutSet,
	NowPlayingBio,
	NowPlayingProfile,
	NowPlayingSoundCloud,
	GhostScheduled,
	FolderTags,
	GlassUi,
	UnlimitedSearch,
	MentionDeletedReply,
	CompactChats, // Applied after a restart.
	ConfirmCalls,
	HideBlocked,
	DeleteJoined,
	GlassChats,
	GlassProfile,
	Dictation, // Windows only.

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
	TranslateSendTo, // Index in SendLanguageNames().
	GhostVisibleStatuses, // Bits of GhostStatus left visible to others.
	BubbleRadius, // Pixels at 100% scale, 2 - 24, applied after a restart.

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

// The statuses hidden by "Don't send typing", each can be left visible.
enum class GhostStatus {
	Typing,
	Voice,
	Round,
	Photo,
	Video,
	File,
	Sticker,
	Location,
	Game,
	Speaking,
	Emoji, // Taps on the animated emoji and watching the taps.

	kCount,
};

void LoadMods(not_null<Core::Settings*> settings);

[[nodiscard]] bool Enabled(Mod mod);
[[nodiscard]] rpl::producer<bool> EnabledValue(Mod mod);
void SetEnabled(Mod mod, bool enabled);

// Sub-options are not counted as separate mods in the settings banner.
[[nodiscard]] bool CountedInBanner(Mod mod);

// Features removed from Telewhite (music in the profile, fake location):
// they always stay off and are not shown in the settings.
[[nodiscard]] bool Removed(Mod mod);

[[nodiscard]] int OptionNow(Option option);
[[nodiscard]] rpl::producer<int> OptionValue(Option option);
void SetOption(Option option, int value);

// The values Telewhite was started with: the mods that are applied
// after a restart (compact chats, bubble rounding) compare with them.
[[nodiscard]] bool LoadedValue(Mod mod);
[[nodiscard]] int LoadedOption(Option option);

[[nodiscard]] TimeId AutoClearSeconds(AutoClearPeriod period);

// Ghost mode with "Don't send typing" doesn't send this status.
[[nodiscard]] bool GhostStatusHidden(GhostStatus status);

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
