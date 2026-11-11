/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_mods.h"

#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Telewhite {
namespace {

constexpr auto kModsCount = int(Mod::kCount);

[[nodiscard]] std::string_view Key(Mod mod) {
	switch (mod) {
	case Mod::GhostRead: return "telewhite-ghost-read";
	case Mod::GhostTyping: return "telewhite-ghost-typing";
	case Mod::GhostOnline: return "telewhite-ghost-online";
	case Mod::GhostStories: return "telewhite-ghost-stories";
	case Mod::SaveDeleted: return "telewhite-save-deleted";
	case Mod::EditHistory: return "telewhite-edit-history";
	case Mod::ForwardNoAuthor: return "telewhite-forward-no-author";
	case Mod::MultiForward: return "telewhite-multi-forward";
	case Mod::LocalPins: return "telewhite-local-pins";
	case Mod::HideSponsored: return "telewhite-hide-sponsored";
	case Mod::HideEmojiStatus: return "telewhite-hide-emoji-status";
	case Mod::PreciseLastSeen: return "telewhite-precise-last-seen";
	case Mod::RegistrationDate: return "telewhite-registration-date";
	case Mod::Snowfall: return "telewhite-snowfall";
	case Mod::StreamerMode: return "telewhite-streamer-mode";
	case Mod::ConfirmStickers: return "telewhite-confirm-stickers";
	case Mod::ConfirmGifs: return "telewhite-confirm-gifs";
	case Mod::ConfirmVoice: return "telewhite-confirm-voice";
	case Mod::ThemedIcon: return "telewhite-themed-icon";
	case Mod::NoSpoilers: return "telewhite-no-spoilers";
	case Mod::NoGreetingSticker: return "telewhite-no-greeting-sticker";
	case Mod::NoBubbleTails: return "telewhite-no-bubble-tails";
	case Mod::AuthorOnline: return "telewhite-author-online";
	case Mod::HideStoryRings: return "telewhite-hide-story-rings";
	case Mod::SnowfallChats: return "telewhite-snowfall-chats";
	case Mod::AntiCaps: return "telewhite-anti-caps";
	case Mod::BypassRestrictions: return "telewhite-bypass-restrictions";
	case Mod::TranslateChats: return "telewhite-translate-chats";
	case Mod::FakeLocation: return "telewhite-fake-location";
	case Mod::FakeLocationAuto: return "telewhite-fake-location-auto";
	case Mod::FakeLocationWalk: return "telewhite-fake-location-walk";
	case Mod::KeepOwnDeleted: return "telewhite-keep-own-deleted";
	case Mod::KeepTimerMessages: return "telewhite-keep-timer-messages";
	case Mod::EditHistoryInline: return "telewhite-edit-history-inline";
	case Mod::ExcludeGroups: return "telewhite-exclude-groups";
	case Mod::ExcludeBots: return "telewhite-exclude-bots";
	case Mod::ExcludeChannels: return "telewhite-exclude-channels";
	case Mod::EditsInExcluded: return "telewhite-edits-in-excluded";
	case Mod::NowPlaying: return "telewhite-now-playing";
	case Mod::NowPlayingSpotify: return "telewhite-now-playing-spotify";
	case Mod::NowPlayingYandex: return "telewhite-now-playing-yandex";
	case Mod::NowPlayingVk: return "telewhite-now-playing-vk";
	case Mod::NowPlayingOther: return "telewhite-now-playing-other";
	case Mod::GhostReadOnReply: return "telewhite-ghost-read-on-reply";
	case Mod::TranslateBeforeSend: return "telewhite-translate-before-send";
	case Mod::HidePremium: return "telewhite-hide-premium";
	case Mod::PollResults: return "telewhite-poll-results";
	case Mod::StickersWithoutSet: return "telewhite-stickers-without-set";
	case Mod::NowPlayingBio: return "telewhite-now-playing-bio";
	case Mod::NowPlayingProfile: return "telewhite-now-playing-profile";
	case Mod::NowPlayingSoundCloud: return "telewhite-now-playing-soundcloud";
	case Mod::GhostScheduled: return "telewhite-ghost-scheduled";
	case Mod::kCount: break;
	}
	Unexpected("Mod in Telewhite::Key.");
}

[[nodiscard]] bool DefaultValue(Mod mod) {
	switch (mod) {
	case Mod::ThemedIcon:
	case Mod::EditHistoryInline:
	case Mod::NowPlayingSpotify:
	case Mod::NowPlayingYandex:
	case Mod::NowPlayingVk:
	case Mod::NowPlayingBio:
	case Mod::NowPlayingProfile:
	case Mod::NowPlayingSoundCloud:
	case Mod::GhostReadOnReply: return true;
	default: return false;
	}
}

constexpr auto kOptionsCount = int(Option::kCount);

[[nodiscard]] std::string_view Key(Option option) {
	switch (option) {
	case Option::DeletedTransparency:
		return "telewhite-deleted-transparency";
	case Option::VersionsTransparency:
		return "telewhite-versions-transparency";
	case Option::AutoClear: return "telewhite-auto-clear";
	case Option::SnowDensity: return "telewhite-snow-density";
	case Option::SnowSpeed: return "telewhite-snow-speed";
	case Option::StoryRingColor: return "telewhite-story-ring-color";
	case Option::Translator: return "telewhite-translator";
	case Option::FakeCity: return "telewhite-fake-city";
	case Option::TranslateSendTo: return "telewhite-translate-send-to";
	case Option::kCount: break;
	}
	Unexpected("Option in Telewhite::Key.");
}

[[nodiscard]] int DefaultValue(Option option) {
	switch (option) {
	case Option::DeletedTransparency: return 30;
	case Option::VersionsTransparency: return 50;
	case Option::SnowDensity:
	case Option::SnowSpeed: return 1;
	default: return 0;
	}
}

[[nodiscard]] std::array<rpl::variable<bool>, kModsCount> &Values() {
	static auto result = std::array<rpl::variable<bool>, kModsCount>();
	return result;
}

[[nodiscard]] auto OptionValues()
-> std::array<rpl::variable<int>, kOptionsCount> & {
	static auto result = std::array<rpl::variable<int>, kOptionsCount>();
	return result;
}

using DeletedKey = std::pair<uint64, FullMsgId>;

[[nodiscard]] base::flat_map<DeletedKey, TimeId> &MarkedDeletedMap() {
	static auto result = base::flat_map<DeletedKey, TimeId>();
	return result;
}

[[nodiscard]] DeletedKey DeletedKeyOf(not_null<const HistoryItem*> item) {
	return { item->history()->session().uniqueId(), item->fullId() };
}

struct RegistrationPoint {
	uint64 id = 0;
	int64 date = 0;
};

// Approximate (user id -> registration unixtime) checkpoints.
constexpr auto kRegistrationPoints = std::array<RegistrationPoint, 33>{ {
	{ 1000000ULL, 1376438400 },
	{ 2768409ULL, 1383264000 },
	{ 7679610ULL, 1388448000 },
	{ 11538514ULL, 1391212000 },
	{ 15835244ULL, 1392940000 },
	{ 23646077ULL, 1393459000 },
	{ 38015510ULL, 1393632000 },
	{ 44634663ULL, 1399334000 },
	{ 54845238ULL, 1411257000 },
	{ 63263518ULL, 1414454000 },
	{ 101260938ULL, 1425600000 },
	{ 122600695ULL, 1437782000 },
	{ 133909606ULL, 1444176000 },
	{ 157242073ULL, 1446768000 },
	{ 171295414ULL, 1457481000 },
	{ 181783990ULL, 1460246000 },
	{ 222021233ULL, 1465344000 },
	{ 278941742ULL, 1473465000 },
	{ 294851037ULL, 1479600000 },
	{ 328594461ULL, 1482969000 },
	{ 352940995ULL, 1487894000 },
	{ 369669043ULL, 1490918000 },
	{ 400169472ULL, 1501459000 },
	{ 805158066ULL, 1563208000 },
	{ 1974255900ULL, 1634000000 },
	{ 2147483647ULL, 1638316800 },
	{ 5000000000ULL, 1648771200 },
	{ 5500000000ULL, 1661990400 },
	{ 6000000000ULL, 1677628800 },
	{ 6500000000ULL, 1693526400 },
	{ 7000000000ULL, 1709251200 },
	{ 7500000000ULL, 1725148800 },
	{ 8000000000ULL, 1740787200 },
} };

} // namespace

void LoadMods(not_null<Core::Settings*> settings) {
	auto &values = Values();
	for (auto i = 0; i != kModsCount; ++i) {
		values[i] = !Removed(Mod(i))
			&& settings->readPref<bool>(
				Key(Mod(i)),
				DefaultValue(Mod(i)));
	}
	auto &options = OptionValues();
	for (auto i = 0; i != kOptionsCount; ++i) {
		const auto option = Option(i);
		const auto serialized = settings->readPref<QByteArray>(Key(option));
		auto ok = false;
		const auto value = serialized.toInt(&ok);
		options[i] = ok ? value : DefaultValue(option);
	}

}

bool Removed(Mod mod) {
	switch (mod) {
	case Mod::LocalPins: // Unlimited pins are always on now.
	case Mod::NowPlaying:
	case Mod::NowPlayingSpotify:
	case Mod::NowPlayingYandex:
	case Mod::NowPlayingVk:
	case Mod::NowPlayingOther:
	case Mod::NowPlayingBio:
	case Mod::NowPlayingProfile:
	case Mod::NowPlayingSoundCloud:
	case Mod::FakeLocation:
	case Mod::FakeLocationAuto:
	case Mod::FakeLocationWalk: return true;
	default: return false;
	}
}

bool CountedInBanner(Mod mod) {
	if (Removed(mod)) {
		return false;
	}
	switch (mod) {
	case Mod::StreamerMode:
	case Mod::NowPlaying:
		// Streamer mode and players are supported only on Windows.
		return Platform::IsWindows();
	case Mod::ThemedIcon:
	case Mod::FakeLocationAuto:
	case Mod::FakeLocationWalk:
	case Mod::EditHistoryInline:
	case Mod::ExcludeGroups:
	case Mod::ExcludeBots:
	case Mod::ExcludeChannels:
	case Mod::EditsInExcluded:
	case Mod::NowPlayingSpotify:
	case Mod::NowPlayingYandex:
	case Mod::NowPlayingVk:
	case Mod::NowPlayingOther:
	case Mod::NowPlayingBio:
	case Mod::NowPlayingProfile:
	case Mod::NowPlayingSoundCloud:
	case Mod::GhostScheduled:
	case Mod::GhostReadOnReply: return false;
	default: return true;
	}
}

int OptionNow(Option option) {
	return OptionValues()[int(option)].current();
}

rpl::producer<int> OptionValue(Option option) {
	return OptionValues()[int(option)].value();
}

void SetOption(Option option, int value) {
	OptionValues()[int(option)] = value;
	Core::App().settings().writePref<QByteArray>(
		Key(option),
		QByteArray::number(value));
	Core::App().saveSettingsDelayed();
}

TimeId AutoClearSeconds(AutoClearPeriod period) {
	constexpr auto kDay = TimeId(86400);
	switch (period) {
	case AutoClearPeriod::Never: return 0;
	case AutoClearPeriod::Day: return kDay;
	case AutoClearPeriod::Week: return 7 * kDay;
	case AutoClearPeriod::Month: return 30 * kDay;
	case AutoClearPeriod::ThreeMonths: return 91 * kDay;
	case AutoClearPeriod::Year: return 365 * kDay;
	}
	return 0;
}

bool Enabled(Mod mod) {
	return Values()[int(mod)].current();
}

rpl::producer<bool> EnabledValue(Mod mod) {
	return Values()[int(mod)].value();
}

void SetEnabled(Mod mod, bool enabled) {
	if (Removed(mod)) {
		enabled = false;
	}
	Values()[int(mod)] = enabled;
	Core::App().settings().writePref<bool>(Key(mod), enabled);
	Core::App().saveSettingsDelayed();
}

QDate EstimateRegistrationDate(uint64 userId) {
	if (!userId) {
		return QDate();
	}
	const auto &points = kRegistrationPoints;
	auto date = int64(0);
	if (userId <= points.front().id) {
		date = points.front().date;
	} else if (userId >= points.back().id) {
		const auto &a = points[points.size() - 2];
		const auto &b = points.back();
		const auto slope = double(b.date - a.date) / double(b.id - a.id);
		date = b.date + int64(slope * double(userId - b.id));
	} else {
		for (auto i = 1; i != int(points.size()); ++i) {
			const auto &b = points[i];
			if (userId > b.id) {
				continue;
			}
			const auto &a = points[i - 1];
			const auto part = double(userId - a.id) / double(b.id - a.id);
			date = a.date + int64(part * double(b.date - a.date));
			break;
		}
	}
	date = std::min(date, int64(base::unixtime::now()));
	return base::unixtime::parse(TimeId(date)).date();
}

QString RegistrationDateText(uint64 userId) {
	const auto date = EstimateRegistrationDate(userId);
	if (!date.isValid()) {
		return QString();
	}
	return u"~ "_q + langMonthOfYearFull(date.month(), date.year());
}

QString FormatExactDateTime(TimeId date) {
	return base::unixtime::parse(date).toString(u"dd.MM.yyyy HH:mm:ss"_q);
}

bool MarkDeleted(not_null<const HistoryItem*> item) {
	return MarkedDeletedMap().emplace(
		DeletedKeyOf(item),
		base::unixtime::now()).second;
}

bool IsMarkedDeleted(not_null<const HistoryItem*> item) {
	const auto &map = MarkedDeletedMap();
	return !map.empty() && map.contains(DeletedKeyOf(item));
}

TimeId MarkedDeletedAt(not_null<const HistoryItem*> item) {
	const auto &map = MarkedDeletedMap();
	if (map.empty()) {
		return 0;
	}
	const auto i = map.find(DeletedKeyOf(item));
	return (i != map.end()) ? i->second : 0;
}

bool SkipContentsRead(not_null<HistoryItem*> item) {
	if (!Enabled(Mod::GhostRead) || !item->isIncomingUnreadMedia()) {
		return false;
	}
	const auto media = item->media();
	return !media || !media->ttlSeconds();
}

} // namespace Telewhite
