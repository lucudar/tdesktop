/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_settings.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "storage/localstorage.h"
#include "telewhite/telewhite_archive.h"
#include "telewhite/telewhite_custom_texts.h"
#include "telewhite/telewhite_deleted.h"
#include "telewhite/telewhite_location.h"
#include "telewhite/telewhite_mods.h"
#include "telewhite/telewhite_mods_box.h"
#include "telewhite/telewhite_mods_preview.h"
#include "telewhite/telewhite_now_playing.h"
#include "telewhite/telewhite_themes.h"
#include "telewhite/telewhite_translate.h"
#include "telewhite/telewhite_tweaks.h"
#include "ui/boxes/choose_font_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Telewhite {
namespace {

using namespace Settings;
using namespace Settings::Builder;
using Preview::Scene;

constexpr auto kThemeColumns = 4;

enum class Group {
	Ghost,
	Deleted,
	Forward,
	Sending,
	Chats,
	Premium,
	Clean,
	Profile,
	Music,
	Location,
	Streamer,
	Design,
	Texts,
};

constexpr auto kGhostMods = std::array{
	Mod::GhostRead,
	Mod::GhostTyping,
	Mod::GhostOnline,
	Mod::GhostStories,
};

struct ToggleDescriptor {
	QString id;
	not_null<const tr::phrase<>*> title;
	Scene scene = Scene::GhostAll;
	rpl::producer<bool> value;
	Fn<void(bool)> set;
	QStringList keywords;
	bool master = false;

	// Sub-option of the previous toggle, not counted in the group status.
	bool sub = false;

	// Small "?" button with the "how it works" box.
	bool help = true;

	rpl::producer<bool> shown;
};

struct GroupStatus {
	QString text;
	bool active = false;
};

// Toggles changed from Telewhite, Telegram settings don't notify about it.
rpl::event_stream<bool> ShowSecondsChanges;

rpl::event_stream<> ExcludedFoldersChanges;

[[nodiscard]] not_null<const tr::phrase<>*> GroupTitle(Group group) {
	switch (group) {
	case Group::Ghost: return &tr::lng_telewhite_mods_ghost;
	case Group::Deleted: return &tr::lng_telewhite_mods_deleted;
	case Group::Forward: return &tr::lng_telewhite_mods_forward;
	case Group::Sending: return &tr::lng_telewhite_mods_sending;
	case Group::Chats: return &tr::lng_telewhite_mods_chats;
	case Group::Premium: return &tr::lng_telewhite_mods_premium;
	case Group::Clean: return &tr::lng_telewhite_mods_clean;
	case Group::Profile: return &tr::lng_telewhite_mods_profile;
	case Group::Music: return &tr::lng_telewhite_mods_music;
	case Group::Location: return &tr::lng_telewhite_mods_location;
	case Group::Streamer: return &tr::lng_telewhite_mods_streamer;
	case Group::Design: return &tr::lng_telewhite_mods_design;
	case Group::Texts: return &tr::lng_telewhite_mods_texts;
	}
	Unexpected("Group in Telewhite::GroupTitle.");
}

[[nodiscard]] not_null<const style::icon*> GroupIcon(Group group) {
	switch (group) {
	case Group::Ghost: return &st::menuIconStealth;
	case Group::Deleted: return &st::menuIconDelete;
	case Group::Forward: return &st::menuIconForward;
	case Group::Sending: return &st::menuIconSend;
	case Group::Chats: return &st::menuIconChats;
	case Group::Premium: return &st::menuIconPremium;
	case Group::Clean: return &st::menuIconCaptionHide;
	case Group::Profile: return &st::menuIconTimer;
	case Group::Music: return &st::menuIconMusic;
	case Group::Location: return &st::menuIconAddress;
	case Group::Streamer: return &st::menuIconStartStream;
	case Group::Design: return &st::menuIconPalette;
	case Group::Texts: return &st::menuIconEdit;
	}
	Unexpected("Group in Telewhite::GroupIcon.");
}

[[nodiscard]] rpl::producer<QString> GroupAbout(Group group) {
	switch (group) {
	case Group::Ghost: return tr::lng_telewhite_mods_ghost_about();
	case Group::Deleted: return tr::lng_telewhite_mods_deleted_about();
	case Group::Forward: return tr::lng_telewhite_mods_forward_about();
	case Group::Sending: return tr::lng_telewhite_mods_sending_about();
	case Group::Chats: return tr::lng_telewhite_mods_chats_about();
	case Group::Premium: return tr::lng_telewhite_mods_premium_about();
	case Group::Clean: return tr::lng_telewhite_mods_hide_phone_about();
	case Group::Profile: return tr::lng_telewhite_mods_profile_about();
	case Group::Music: return tr::lng_telewhite_mods_music_about();
	case Group::Location: return tr::lng_telewhite_mods_location_about();
	case Group::Streamer: return tr::lng_telewhite_mods_streamer_about();
	case Group::Design: return tr::lng_telewhite_mods_themes_about();
	case Group::Texts: return tr::lng_telewhite_mods_texts_about();
	}
	Unexpected("Group in Telewhite::GroupAbout.");
}

[[nodiscard]] rpl::producer<bool> GhostAllValue() {
	return rpl::combine(
		EnabledValue(Mod::GhostRead),
		EnabledValue(Mod::GhostTyping),
		EnabledValue(Mod::GhostOnline),
		EnabledValue(Mod::GhostStories)
	) | rpl::map([](bool read, bool typing, bool online, bool stories) {
		return read && typing && online && stories;
	});
}

[[nodiscard]] ToggleDescriptor ModToggle(
		QString id,
		not_null<const tr::phrase<>*> title,
		Mod mod,
		Scene scene,
		QStringList keywords) {
	return {
		.id = std::move(id),
		.title = title,
		.scene = scene,
		.value = EnabledValue(mod),
		.set = [=](bool value) { SetEnabled(mod, value); },
		.keywords = std::move(keywords),
	};
}

[[nodiscard]] std::vector<ToggleDescriptor> GroupToggles(
		Group group,
		not_null<Main::Session*> session) {
	auto result = std::vector<ToggleDescriptor>();
	const auto mod = [&](
			const QString &id,
			not_null<const tr::phrase<>*> title,
			Mod which,
			Scene scene,
			QStringList keywords) {
		result.push_back(ModToggle(
			u"telewhite/"_q + id,
			title,
			which,
			scene,
			std::move(keywords)));
	};
	switch (group) {
	case Group::Ghost:
		result.push_back({
			.id = u"telewhite/ghost/all"_q,
			.title = &tr::lng_telewhite_mods_ghost_all,
			.scene = Scene::GhostAll,
			.value = GhostAllValue(),
			.set = [](bool value) {
				for (const auto mod : kGhostMods) {
					SetEnabled(mod, value);
				}
			},
			.keywords = { u"ghost"_q, u"призрак"_q, u"невидимка"_q },
			.master = true,
		});
		mod(
			u"ghost/read"_q,
			&tr::lng_telewhite_mods_ghost_read,
			Mod::GhostRead,
			Scene::GhostRead,
			{ u"прочитано"_q, u"галочки"_q, u"read"_q });
		mod(
			u"ghost/read-on-reply"_q,
			&tr::lng_telewhite_mods_ghost_read_on_reply,
			Mod::GhostReadOnReply,
			Scene::ReadOnReply,
			{ u"прочитано"_q, u"ответ"_q, u"reply"_q });
		result.back().sub = true;
		result.back().shown = EnabledValue(Mod::GhostRead);
		mod(
			u"ghost/typing"_q,
			&tr::lng_telewhite_mods_ghost_typing,
			Mod::GhostTyping,
			Scene::GhostTyping,
			{ u"печатает"_q, u"typing"_q });
		mod(
			u"ghost/online"_q,
			&tr::lng_telewhite_mods_ghost_online,
			Mod::GhostOnline,
			Scene::GhostOnline,
			{ u"онлайн"_q, u"в сети"_q, u"online"_q });
		mod(
			u"ghost/stories"_q,
			&tr::lng_telewhite_mods_ghost_stories,
			Mod::GhostStories,
			Scene::GhostStories,
			{ u"истории"_q, u"stories"_q });
		break;
	case Group::Deleted:
		mod(
			u"deleted/save"_q,
			&tr::lng_telewhite_mods_save_deleted,
			Mod::SaveDeleted,
			Scene::SaveDeleted,
			{ u"удалённые"_q, u"удаленные"_q, u"deleted"_q });
		mod(
			u"deleted/own"_q,
			&tr::lng_telewhite_mods_keep_own_deleted,
			Mod::KeepOwnDeleted,
			Scene::KeepOwnDeleted,
			{ u"мои"_q, u"удалённые"_q, u"у всех"_q, u"deleted"_q });
		mod(
			u"deleted/timer"_q,
			&tr::lng_telewhite_mods_keep_timer,
			Mod::KeepTimerMessages,
			Scene::KeepTimerMessages,
			{ u"таймер"_q, u"исчезающие"_q, u"автоудаление"_q, u"timer"_q });
		mod(
			u"deleted/edits"_q,
			&tr::lng_telewhite_mods_edit_history,
			Mod::EditHistory,
			Scene::EditHistory,
			{ u"изменённые"_q, u"измененные"_q, u"edit"_q });
		mod(
			u"deleted/edits-inline"_q,
			&tr::lng_telewhite_mods_edit_history_inline,
			Mod::EditHistoryInline,
			Scene::EditHistoryInline,
			{ u"изменённые"_q, u"прежний текст"_q, u"версии"_q });
		result.back().sub = true;
		result.back().shown = EnabledValue(Mod::EditHistory);
		break;
	case Group::Forward:
		mod(
			u"forward/no-author"_q,
			&tr::lng_telewhite_mods_forward_no_author,
			Mod::ForwardNoAuthor,
			Scene::ForwardNoAuthor,
			{ u"пересылка"_q, u"автор"_q, u"forward"_q });
		mod(
			u"forward/multi"_q,
			&tr::lng_telewhite_mods_multi_forward,
			Mod::MultiForward,
			Scene::MultiForward,
			{ u"пересылка"_q, u"несколько"_q, u"forward"_q });
		break;
	case Group::Sending:
		mod(
			u"sending/stickers"_q,
			&tr::lng_telewhite_mods_confirm_stickers,
			Mod::ConfirmStickers,
			Scene::ConfirmStickers,
			{ u"стикеры"_q, u"stickers"_q });
		mod(
			u"sending/gifs"_q,
			&tr::lng_telewhite_mods_confirm_gifs,
			Mod::ConfirmGifs,
			Scene::ConfirmGifs,
			{ u"гиф"_q, u"gif"_q });
		mod(
			u"sending/voice"_q,
			&tr::lng_telewhite_mods_confirm_voice,
			Mod::ConfirmVoice,
			Scene::ConfirmVoice,
			{ u"голосовые"_q, u"кружки"_q, u"voice"_q });
		mod(
			u"sending/anti-caps"_q,
			&tr::lng_telewhite_mods_anti_caps,
			Mod::AntiCaps,
			Scene::AntiCaps,
			{ u"капс"_q, u"caps"_q, u"заглавные"_q, u"регистр"_q });
		mod(
			u"sending/stickers-without-set"_q,
			&tr::lng_telewhite_mods_stickers_without_set,
			Mod::StickersWithoutSet,
			Scene::StickersWithoutSet,
			{ u"стикер"_q, u"анонимные"_q, u"набор"_q, u"sticker"_q });
		break;
	case Group::Chats:
		mod(
			u"chats/spoilers"_q,
			&tr::lng_telewhite_mods_no_spoilers,
			Mod::NoSpoilers,
			Scene::NoSpoilers,
			{ u"спойлер"_q, u"spoiler"_q });
		mod(
			u"chats/greeting"_q,
			&tr::lng_telewhite_mods_no_greeting_sticker,
			Mod::NoGreetingSticker,
			Scene::NoGreetingSticker,
			{ u"приветствие"_q, u"стикер"_q, u"greeting"_q });
		mod(
			u"chats/restrictions"_q,
			&tr::lng_telewhite_mods_bypass_restrictions,
			Mod::BypassRestrictions,
			Scene::BypassRestrictions,
			{ u"ограничения"_q, u"недоступен"_q, u"restricted"_q });
		mod(
			u"chats/poll-results"_q,
			&tr::lng_telewhite_mods_poll_results,
			Mod::PollResults,
			Scene::PollResults,
			{ u"опрос"_q, u"результаты"_q, u"poll"_q });
		break;
	case Group::Premium:
		mod(
			u"premium/pins"_q,
			&tr::lng_telewhite_mods_local_pins,
			Mod::LocalPins,
			Scene::LocalPins,
			{ u"закреп"_q, u"лимит"_q, u"pin"_q });
		result.push_back({
			.id = u"telewhite/premium/translate"_q,
			.title = &tr::lng_telewhite_mods_translate_chats,
			.scene = Scene::TranslateChats,
			.value = EnabledValue(Mod::TranslateChats),
			.set = [](bool value) {
				SetEnabled(Mod::TranslateChats, value);
				if (value && !Core::App().settings().translateChatEnabled()) {
					// The bar is hidden while "Translate entire chats"
					// is turned off in the Telegram settings.
					Core::App().settings().setTranslateChatEnabled(true);
					Core::App().saveSettingsDelayed();
				}
			},
			.keywords = { u"перевод"_q, u"перевести"_q, u"translate"_q },
		});
		mod(
			u"premium/translate-before-send"_q,
			&tr::lng_telewhite_mods_translate_before_send,
			Mod::TranslateBeforeSend,
			Scene::TranslateBeforeSend,
			{ u"перевод"_q, u"перед отправкой"_q, u"translate"_q });
		break;
	case Group::Clean:
		result.push_back({
			.id = u"telewhite/clean/stories"_q,
			.title = &tr::lng_settings_chat_hide_stories,
			.scene = Scene::HideStories,
			.value = rpl::single(
				Core::App().settings().hideStories()
			) | rpl::then(Core::App().settings().hideStoriesChanges()),
			.set = [](bool value) {
				Core::App().settings().setHideStories(value);
				Core::App().saveSettingsDelayed();
			},
			.keywords = { u"истории"_q, u"stories"_q },
		});
		mod(
			u"clean/sponsored"_q,
			&tr::lng_telewhite_mods_hide_sponsored,
			Mod::HideSponsored,
			Scene::HideSponsored,
			{ u"реклама"_q, u"спонсор"_q, u"ads"_q });
		mod(
			u"clean/emoji-status"_q,
			&tr::lng_telewhite_mods_hide_emoji_status,
			Mod::HideEmojiStatus,
			Scene::HideEmojiStatus,
			{ u"эмодзи"_q, u"статус"_q, u"emoji"_q });
		result.push_back({
			.id = u"telewhite/clean/phone"_q,
			.title = &tr::lng_telewhite_mods_hide_phone,
			.scene = Scene::HidePhone,
			.value = session->settings().phoneNumberHiddenValue(),
			.set = [=](bool value) {
				session->settings().setPhoneNumberHidden(value);
				session->saveSettingsDelayed();
			},
			.keywords = { u"номер"_q, u"телефон"_q, u"phone"_q },
		});
		mod(
			u"clean/hide-premium"_q,
			&tr::lng_telewhite_mods_hide_premium,
			Mod::HidePremium,
			Scene::HidePremium,
			{ u"premium"_q, u"премиум"_q, u"подарки"_q, u"звёзды"_q, u"stars"_q });
		break;
	case Group::Profile:
		mod(
			u"profile/last-seen"_q,
			&tr::lng_telewhite_mods_precise_last_seen,
			Mod::PreciseLastSeen,
			Scene::PreciseLastSeen,
			{ u"был в сети"_q, u"время"_q, u"last seen"_q });
		result.push_back({
			.id = u"telewhite/profile/seconds"_q,
			.title = &tr::lng_settings_chat_show_seconds,
			.scene = Scene::ShowSeconds,
			.value = rpl::single(
				Core::App().settings().showSeconds()
			) | rpl::then(ShowSecondsChanges.events()),
			.set = [](bool value) {
				Core::App().settings().setShowSeconds(value);
				Core::App().saveSettingsDelayed();
				ShowSecondsChanges.fire_copy(value);
			},
			.keywords = { u"секунды"_q, u"время"_q, u"seconds"_q },
		});
		mod(
			u"profile/registration"_q,
			&tr::lng_telewhite_mods_registration_date,
			Mod::RegistrationDate,
			Scene::RegistrationDate,
			{ u"регистрация"_q, u"дата"_q, u"создан"_q });
		break;
	case Group::Music: {
		mod(
			u"music/now-playing"_q,
			&tr::lng_telewhite_mods_now_playing,
			Mod::NowPlaying,
			Scene::NowPlaying,
			{
				u"музыка"_q,
				u"слушаю"_q,
				u"о себе"_q,
				u"spotify"_q,
				u"music"_q,
			});
		const auto source = [&](
				const QString &id,
				not_null<const tr::phrase<>*> title,
				Mod which,
				QStringList keywords) {
			mod(
				u"music/"_q + id,
				title,
				which,
				Scene::NowPlaying,
				std::move(keywords));
			result.back().sub = true;
			result.back().help = false;
			result.back().shown = EnabledValue(Mod::NowPlaying);
		};
		source(
			u"spotify"_q,
			&tr::lng_telewhite_mods_now_playing_spotify,
			Mod::NowPlayingSpotify,
			{ u"spotify"_q, u"спотифай"_q });
		source(
			u"yandex"_q,
			&tr::lng_telewhite_mods_now_playing_yandex,
			Mod::NowPlayingYandex,
			{ u"яндекс"_q, u"yandex"_q });
		source(
			u"vk"_q,
			&tr::lng_telewhite_mods_now_playing_vk,
			Mod::NowPlayingVk,
			{ u"вк"_q, u"vk"_q, u"вконтакте"_q });
		source(
			u"other"_q,
			&tr::lng_telewhite_mods_now_playing_other,
			Mod::NowPlayingOther,
			{ u"браузер"_q, u"плеер"_q, u"browser"_q });
	} break;
	case Group::Location:
		mod(
			u"location/fake"_q,
			&tr::lng_telewhite_mods_fake_location,
			Mod::FakeLocation,
			Scene::FakeLocation,
			{ u"геопозиция"_q, u"местоположение"_q, u"location"_q, u"gps"_q });
		mod(
			u"location/auto"_q,
			&tr::lng_telewhite_mods_fake_location_auto,
			Mod::FakeLocationAuto,
			Scene::FakeLocation,
			{ u"геопозиция"_q, u"недоступна"_q });
		result.back().sub = true;
		result.back().help = false;
		result.back().shown = EnabledValue(Mod::FakeLocation);
		mod(
			u"location/walk"_q,
			&tr::lng_telewhite_mods_fake_location_walk,
			Mod::FakeLocationWalk,
			Scene::FakeLocation,
			{ u"прогулка"_q, u"движение"_q, u"walk"_q });
		result.back().sub = true;
		result.back().help = false;
		result.back().shown = EnabledValue(Mod::FakeLocation);
		break;
	case Group::Streamer:
		mod(
			u"streamer/hide"_q,
			&tr::lng_telewhite_mods_streamer_hide,
			Mod::StreamerMode,
			Scene::Streamer,
			{ u"стример"_q, u"запись экрана"_q, u"скриншот"_q, u"stream"_q });
		break;
	case Group::Design:
		// Built separately in BuildDesign(), the status is the theme.
		break;
	case Group::Texts:
		// Built separately in BuildCustomTexts().
		break;
	}
	return result;
}

[[nodiscard]] rpl::producer<GroupStatus> GroupStatusValue(
		Group group,
		not_null<Main::Session*> session) {
	if (group == Group::Texts) {
		return rpl::single(rpl::empty) | rpl::then(
			CustomTextsChanges()
		) | rpl::map([] {
			const auto count = CustomTextsCount();
			const auto total = int(CustomTexts().size());
			const auto text = !count
				? tr::lng_telewhite_mods_status_off(tr::now)
				: tr::lng_telewhite_mods_status_partial(
					tr::now,
					lt_amount,
					QString::number(count),
					lt_total,
					QString::number(total));
			return GroupStatus{ .text = text, .active = (count > 0) };
		});
	}
	if (group == Group::Design) {
		return CurrentThemeValue(
		) | rpl::map([](Theme theme) {
			return ThemeName(theme);
		}) | rpl::flatten_latest(
		) | rpl::map([](const QString &name) {
			return GroupStatus{ .text = name, .active = true };
		});
	}
	auto values = std::vector<rpl::producer<int>>();
	for (auto &toggle : GroupToggles(group, session)) {
		if (!toggle.master && !toggle.sub) {
			values.push_back(std::move(toggle.value) | rpl::map([](bool on) {
				return on ? 1 : 0;
			}));
		}
	}
	return rpl::combine(
		std::move(values)
	) | rpl::map([](const std::vector<int> &list) {
		const auto total = int(list.size());
		const auto enabled = int(ranges::count(list, 1));
		const auto text = !enabled
			? tr::lng_telewhite_mods_status_off(tr::now)
			: (enabled == total)
			? tr::lng_telewhite_mods_status_on(tr::now)
			: tr::lng_telewhite_mods_status_partial(
				tr::now,
				lt_amount,
				QString::number(enabled),
				lt_total,
				QString::number(total));
		return GroupStatus{ .text = text, .active = (enabled > 0) };
	});
}

[[nodiscard]] bool GroupShown(Group group) {
	// Streamer mode and players are supported only on Windows.
	if (group == Group::Music) {
		// "Now playing" is hidden for now.
		return false;
	}
	return (group != Group::Streamer) || Platform::IsWindows();
}

// Small round "?" button near the toggle, opens the "how it works" box.
class HelpButton final : public Ui::RippleButton {
public:
	explicit HelpButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

};

HelpButton::HelpButton(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation) {
	const auto size = style::ConvertScale(30);
	resize(size, size);
	setCursor(style::cur_pointer);
	setAccessibleName(u"?"_q);
}

void HelpButton::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	paintRipple(p, 0, 0);
	const auto over = isOver() || isDown();
	st::menuIconFaq.paintInCenter(
		p,
		rect(),
		(over ? st::windowActiveTextFg : st::menuIconFg)->c);
}

void HelpButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage HelpButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(size());
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateToggle(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		ToggleDescriptor &&descriptor) {
	const auto &st = st::settingsButtonNoIcon;
	auto result = descriptor.master
		? object_ptr<Ui::SettingsButton>(
			container,
			(*descriptor.title)(tr::bold),
			st)
		: object_ptr<Ui::SettingsButton>(
			container,
			(*descriptor.title)(),
			st);
	const auto button = result.data();

	const auto state = button->lifetime().make_state<rpl::variable<bool>>(
		std::move(descriptor.value));
	const auto set = std::move(descriptor.set);
	button->toggleOn(state->value());
	button->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != state->current());
	}) | rpl::on_next([=](bool toggled) {
		set(toggled);
	}, button->lifetime());

	if (!descriptor.help) {
		return result;
	}
	const auto help = Ui::CreateChild<HelpButton>(button);
	const auto gap = style::ConvertScale(4);
	auto padding = st.padding;
	padding.setRight(gap + help->width() + st.padding.right() / 2);
	button->setPaddingOverride(padding);

	button->sizeValue(
	) | rpl::on_next([=, &st](QSize size) {
		const auto &toggle = st.toggle;
		const auto toggleWidth = 2 * toggle.border
			+ toggle.diameter
			+ toggle.width;
		help->moveToLeft(
			size.width() - st.toggleSkip - toggleWidth - gap - help->width(),
			(size.height() - help->height()) / 2);
	}, help->lifetime());

	const auto scene = descriptor.scene;
	help->setClickedCallback([=] {
		controller->show(Box(HowItWorksBox, scene, state->value(), set));
	});
	return result;
}

void AddToggle(SectionBuilder &builder, ToggleDescriptor &&descriptor) {
	if (auto shown = base::take(descriptor.shown)) {
		builder.scope([&] {
			AddToggle(builder, std::move(descriptor));
		}, std::move(shown));
		return;
	}
	const auto id = descriptor.id;
	const auto title = descriptor.title;
	const auto keywords = descriptor.keywords;
	builder.add([taken = std::move(descriptor)](
			const WidgetContext &ctx) mutable {
		return SectionBuilder::WidgetToAdd{
			.widget = CreateToggle(
				ctx.container,
				ctx.controller,
				std::move(taken)),
			.highlight = { .rippleShape = true },
		};
	}, [=] {
		return SearchEntry{
			.id = id,
			.title = (*title)(tr::now),
			.keywords = keywords,
		};
	});
}

struct ChoiceDescriptor {
	QString id;
	not_null<const tr::phrase<>*> title;
	Option option = Option::AutoClear;
	std::vector<QString> options;
	QStringList keywords;
	Fn<void(int)> changed;
	rpl::producer<bool> shown;
};

void AddChoice(SectionBuilder &builder, ChoiceDescriptor &&descriptor) {
	const auto controller = builder.controller();
	const auto title = descriptor.title;
	const auto option = descriptor.option;
	const auto options = std::move(descriptor.options);
	const auto changed = std::move(descriptor.changed);
	const auto count = int(options.size());
	const auto index = [=](int value) {
		return std::clamp(value, 0, count - 1);
	};
	builder.addButton({
		.id = std::move(descriptor.id),
		.title = (*title)(),
		.st = &st::settingsButtonNoIcon,
		.label = OptionValue(option) | rpl::map([=](int value) {
			return options[index(value)];
		}),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = (*title)(),
					.options = options,
					.initialSelection = index(OptionNow(option)),
					.callback = [=](int chosen) {
						SetOption(option, chosen);
						if (changed) {
							changed(chosen);
						}
					},
				});
			}));
		},
		.keywords = std::move(descriptor.keywords),
		.shown = std::move(descriptor.shown),
	});
}

// Transparency in percents, 0 - 90 with 10 steps.
[[nodiscard]] object_ptr<Ui::RpWidget> CreatePercentSlider(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		Option option) {
	constexpr auto kSteps = 10;
	auto result = object_ptr<Ui::VerticalLayout>(container);
	const auto raw = result.data();
	AddButtonWithLabel(
		raw,
		std::move(title),
		OptionValue(option) | rpl::map([](int value) {
			return QString::number(value) + QChar('%');
		}),
		st::autoDownloadLimitButton
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto slider = raw->add(
		object_ptr<Ui::MediaSlider>(raw, st::autoDownloadLimitSlider),
		st::autoDownloadLimitPadding);
	slider->resize(st::autoDownloadLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		kSteps,
		[](int index) { return index * 10; },
		std::clamp(OptionNow(option), 0, 90),
		[=](int value) {
			if (OptionNow(option) != value) {
				SetOption(option, value);
			}
		});
	return result;
}

void AddPercentSlider(
		SectionBuilder &builder,
		QString id,
		not_null<const tr::phrase<>*> title,
		Option option) {
	builder.add([=](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = CreatePercentSlider(ctx.container, (*title)(), option),
		};
	}, [=] {
		return SearchEntry{
			.id = id,
			.title = (*title)(tr::now),
			.keywords = { u"прозрачность"_q, u"transparency"_q },
		};
	});
}

[[nodiscard]] QString ExcludedFoldersText(not_null<Main::Session*> session) {
	const auto chosen = ExcludedFolders(session);
	auto names = QStringList();
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (filter.id() && chosen.contains(filter.id())) {
			names.push_back(filter.title().text.text);
		}
	}
	return names.isEmpty()
		? tr::lng_telewhite_mods_exclude_folders_none(tr::now)
		: names.join(u", "_q);
}

void ExcludedFoldersBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(tr::lng_telewhite_mods_exclude_folders());
	const auto chosen = ExcludedFolders(session);
	const auto checks = box->lifetime().make_state<
		std::vector<std::pair<FilterId, not_null<Ui::Checkbox*>>>>();
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (!filter.id()) {
			continue;
		}
		const auto checkbox = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				filter.title().text.text,
				chosen.contains(filter.id()),
				st::defaultBoxCheckbox),
			st::boxRowPadding + style::margins(
				0,
				0,
				0,
				st::boxOptionListSkip));
		checks->emplace_back(filter.id(), checkbox);
	}
	if (checks->empty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_telewhite_mods_exclude_folders_empty(),
			st::boxLabel));
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
		return;
	}
	box->addButton(tr::lng_settings_save(), [=] {
		auto ids = base::flat_set<FilterId>();
		for (const auto &[id, checkbox] : *checks) {
			if (checkbox->checked()) {
				ids.emplace(id);
			}
		}
		SetExcludedFolders(session, ids);
		ExcludedFoldersChanges.fire({});
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] QString ExcludedChatsText(not_null<Main::Session*> session) {
	const auto peers = ExcludedPeers(session);
	if (peers.empty()) {
		return tr::lng_telewhite_mods_exclude_chats_none(tr::now);
	}
	auto names = QStringList();
	for (const auto &peer : peers) {
		if (names.size() == 2) {
			break;
		}
		names.push_back(peer->name());
	}
	auto result = names.join(u", "_q);
	const auto more = int(peers.size()) - int(names.size());
	if (more > 0) {
		result += u" "_q + tr::lng_telewhite_mods_exclude_chats_more(
			tr::now,
			lt_amount,
			QString::number(more));
	}
	return result;
}

void ExcludedChatsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(tr::lng_telewhite_mods_exclude_chats());
	const auto peers = ExcludedPeers(session);
	if (peers.empty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_telewhite_mods_exclude_chats_empty(),
			st::boxLabel));
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
		return;
	}
	const auto checks = box->lifetime().make_state<
		std::vector<std::pair<not_null<PeerData*>, not_null<Ui::Checkbox*>>>>();
	for (const auto &peer : peers) {
		const auto checkbox = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				peer->name(),
				true,
				st::defaultBoxCheckbox),
			st::boxRowPadding + style::margins(
				0,
				0,
				0,
				st::boxOptionListSkip));
		checks->emplace_back(peer, checkbox);
	}
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_telewhite_mods_exclude_chats_about(),
		st::boxLabel));
	box->addButton(tr::lng_settings_save(), [=] {
		for (const auto &[peer, checkbox] : *checks) {
			if (!checkbox->checked()) {
				SetExcludedPeer(peer, false);
			}
		}
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void CustomFakePointBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_telewhite_mods_fake_location_custom());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_telewhite_mods_fake_location_custom_about(),
		st::boxLabel));
	const auto current = CustomFakePoint();
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_telewhite_mods_fake_location_custom_placeholder(),
		current ? FormatFakePoint(*current) : QString()));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		const auto point = ParseFakePoint(field->getLastText());
		if (!point) {
			field->showError();
			return;
		}
		SetCustomFakePoint(*point);
		SetOption(Option::FakeCity, -1);
		box->closeBox();
	};
	field->submits(
	) | rpl::on_next([=] {
		save();
	}, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void AddFakePlace(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"telewhite/location/place"_q,
		.title = tr::lng_telewhite_mods_fake_location_place(),
		.st = &st::settingsButtonNoIcon,
		.label = FakeLocationNameValue(),
		.onClick = [=] {
			auto options = std::vector<QString>();
			for (const auto &city : FakeCities()) {
				options.push_back(city.name);
			}
			const auto custom = int(options.size());
			options.push_back(
				tr::lng_telewhite_mods_fake_location_custom(tr::now));
			const auto now = OptionNow(Option::FakeCity);
			const auto initial = (now < 0 && CustomFakePoint())
				? custom
				: std::clamp(now, 0, custom - 1);
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::lng_telewhite_mods_fake_location_place(),
					.options = options,
					.initialSelection = initial,
					.callback = [=](int chosen) {
						if (chosen == custom) {
							controller->show(Box(CustomFakePointBox));
						} else {
							SetOption(Option::FakeCity, chosen);
						}
					},
				});
			}));
		},
		.keywords = { u"город"_q, u"координаты"_q, u"место"_q, u"city"_q },
		.shown = EnabledValue(Mod::FakeLocation),
	});
}

void AddTranslatorChoice(SectionBuilder &builder) {
	AddChoice(builder, {
		.id = u"telewhite/premium/translator"_q,
		.title = &tr::lng_telewhite_mods_translator,
		.option = Option::Translator,
		.options = {
			tr::lng_telewhite_mods_translator_telegram(tr::now),
			tr::lng_telewhite_mods_translator_google(tr::now),
		},
		.keywords = { u"перевод"_q, u"google"_q, u"гугл"_q },
	});
}

void AddExclusions(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_exclusions());
	const auto exclude = [&](
			const QString &id,
			not_null<const tr::phrase<>*> title,
			Mod mod,
			QStringList keywords) {
		auto toggle = ModToggle(
			u"telewhite/deleted/"_q + id,
			title,
			mod,
			Scene::SaveDeleted,
			std::move(keywords));
		toggle.sub = true;
		toggle.help = false;
		AddToggle(builder, std::move(toggle));
	};
	exclude(
		u"exclude-groups"_q,
		&tr::lng_telewhite_mods_exclude_groups,
		Mod::ExcludeGroups,
		{ u"исключения"_q, u"группы"_q });
	exclude(
		u"exclude-bots"_q,
		&tr::lng_telewhite_mods_exclude_bots,
		Mod::ExcludeBots,
		{ u"исключения"_q, u"боты"_q });
	exclude(
		u"exclude-channels"_q,
		&tr::lng_telewhite_mods_exclude_channels,
		Mod::ExcludeChannels,
		{ u"исключения"_q, u"каналы"_q });
	builder.addButton({
		.id = u"telewhite/deleted/exclude-folders"_q,
		.title = tr::lng_telewhite_mods_exclude_folders(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(rpl::empty) | rpl::then(
			ExcludedFoldersChanges.events()
		) | rpl::map([=] {
			return ExcludedFoldersText(session);
		}),
		.onClick = [=] {
			controller->show(Box(ExcludedFoldersBox, session));
		},
		.keywords = { u"исключения"_q, u"папки"_q, u"folders"_q },
	});
	builder.addButton({
		.id = u"telewhite/deleted/exclude-chats"_q,
		.title = tr::lng_telewhite_mods_exclude_chats(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(rpl::empty) | rpl::then(
			ExcludedPeersChanges()
		) | rpl::map([=] {
			return ExcludedChatsText(session);
		}),
		.onClick = [=] {
			controller->show(Box(ExcludedChatsBox, session));
		},
		.keywords = { u"исключения"_q, u"чаты"_q, u"chats"_q },
	});
	exclude(
		u"edits-everywhere"_q,
		&tr::lng_telewhite_mods_edits_in_excluded,
		Mod::EditsInExcluded,
		{ u"исключения"_q, u"изменения"_q });
}

void BuildDeletedExtras(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_deleted_look());
	AddPercentSlider(
		builder,
		u"telewhite/deleted/transparency"_q,
		&tr::lng_telewhite_mods_deleted_transparency,
		Option::DeletedTransparency);
	AddPercentSlider(
		builder,
		u"telewhite/deleted/versions-transparency"_q,
		&tr::lng_telewhite_mods_versions_transparency,
		Option::VersionsTransparency);
	AddChoice(builder, {
		.id = u"telewhite/deleted/auto-clear"_q,
		.title = &tr::lng_telewhite_mods_auto_clear,
		.option = Option::AutoClear,
		.options = {
			tr::lng_telewhite_mods_auto_clear_never(tr::now),
			tr::lng_telewhite_mods_auto_clear_day(tr::now),
			tr::lng_telewhite_mods_auto_clear_week(tr::now),
			tr::lng_telewhite_mods_auto_clear_month(tr::now),
			tr::lng_telewhite_mods_auto_clear_three_months(tr::now),
			tr::lng_telewhite_mods_auto_clear_year(tr::now),
		},
		.keywords = { u"автоочистка"_q, u"очистка"_q, u"clear"_q },
		.changed = [](int) { PruneArchives(); },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_deleted_look_about());
	builder.addSkip();
	AddExclusions(builder);
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_exclusions_about());
}

void AddFontButton(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto family = Core::App().settings().customFontFamily();
	const auto label = family.isEmpty()
		? tr::lng_font_default(tr::now)
		: (family == style::SystemFontTag())
		? tr::lng_font_system(tr::now)
		: family;
	builder.addButton({
		.id = u"telewhite/design/font"_q,
		.title = tr::lng_settings_font_family(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(label),
		.onClick = [=] {
			struct Preview {
				std::unique_ptr<Ui::ChatTheme> theme;
				rpl::lifetime lifetime;
			};
			const auto preview = std::make_shared<Preview>();
			preview->theme = Window::Theme::DefaultChatThemeOn(
				preview->lifetime);
			const auto generateBg = [=] {
				const auto size = st::boxWidth;
				const auto ratio = style::DevicePixelRatio();
				auto result = QImage(
					QSize(size, size) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				auto p = QPainter(&result);
				Window::SectionWidget::PaintBackground(
					p,
					preview->theme.get(),
					QSize(size, size * 3),
					QRect(0, 0, size, size));
				p.end();
				return result;
			};
			const auto save = [=](QString chosen) {
				Core::App().settings().setCustomFontFamily(chosen);
				Local::writeSettings();
				Core::Restart();
			};
			controller->show(Box(
				Ui::ChooseFontBox,
				generateBg,
				Core::App().settings().customFontFamily(),
				save));
		},
		.keywords = { u"шрифт"_q, u"font"_q },
	});
}

void AddDesignToggle(
		SectionBuilder &builder,
		const QString &id,
		not_null<const tr::phrase<>*> title,
		Mod mod,
		Scene scene,
		QStringList keywords) {
	AddToggle(builder, ModToggle(
		u"telewhite/design/"_q + id,
		title,
		mod,
		scene,
		std::move(keywords)));
}

[[nodiscard]] Type GroupId(Group group);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateGroupRow(
		not_null<Ui::VerticalLayout*> container,
		Group group,
		rpl::producer<GroupStatus> status,
		Fn<void(Type)> showOther) {
	const auto &st = st::settingsButton;
	auto result = CreateButtonWithIcon(
		container,
		(*GroupTitle(group))(),
		st,
		{ .icon = GroupIcon(group) });
	const auto button = result.data();

	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto &point = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-point.x(),
			point.y() + (size.height() - arrow->height()) / 2);
	}, arrow->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		st.rightLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		button->widthValue(),
		std::move(status)
	) | rpl::on_next([=, &st](int width, const GroupStatus &now) {
		label->setText(now.text);
		label->setTextColorOverride(now.active
			? std::optional<QColor>()
			: std::make_optional(st::windowSubTextFg->c));
		const auto right = st::settingsButtonRightSkip
			+ st::settingsPremiumArrow.width();
		label->resizeToNaturalWidth(width / 2);
		label->moveToRight(right, st.padding.top(), width);
	}, label->lifetime());

	const auto id = GroupId(group);
	button->setClickedCallback([=] {
		showOther(id);
	});
	return result;
}

void AddGroupRow(SectionBuilder &builder, Group group) {
	if (!GroupShown(group)) {
		return;
	}
	builder.add([=](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = CreateGroupRow(
				ctx.container,
				group,
				GroupStatusValue(group, &ctx.controller->session()),
				ctx.showOther),
		};
	});
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateThemesGrid(
		not_null<QWidget*> parent,
		not_null<Window::Controller*> window,
		std::vector<Theme> themes) {
	using Check = Window::Theme::CloudListCheck;

	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto group = std::make_shared<Ui::RadioenumGroup<Theme>>(
		CurrentTheme());

	auto buttons = std::vector<not_null<Ui::Radioenum<Theme>*>>();
	for (const auto theme : themes) {
		auto check = std::make_unique<Check>(ThemeColors(theme), false);
		const auto weak = check.get();
		const auto button = Ui::CreateChild<Ui::Radioenum<Theme>>(
			raw,
			group,
			theme,
			QString(),
			st::settingsTheme,
			std::move(check));
		ThemeName(
			theme
		) | rpl::on_next([=](const QString &name) {
			button->setText(name);
		}, button->lifetime());
		button->addClickHandler([=] {
			ApplyTheme(window, theme);
			group->setValue(CurrentTheme());
		});
		weak->setUpdateCallback([=] { button->update(); });
		button->setCheckAlignment(style::al_top);
		button->resizeToWidth(button->width());
		buttons.push_back(button);
	}
	CurrentThemeValue(
	) | rpl::on_next([=](Theme theme) {
		group->setValue(theme);
	}, raw->lifetime());

	const auto rows = (int(buttons.size()) + kThemeColumns - 1)
		/ kThemeColumns;
	const auto rowSkip = style::ConvertScale(12);
	const auto single = buttons.empty() ? 0 : buttons.front()->height();
	raw->resize(raw->width(), rows * single + (rows - 1) * rowSkip);

	raw->widthValue(
	) | rpl::on_next([=](int width) {
		const auto padding = st::settingsButtonNoIcon.padding;
		width -= padding.left() + padding.right();
		const auto desired = st::settingsThemePreviewSize.width();
		const auto skips = kThemeColumns - 1;
		const auto minSkip = st::settingsThemeMinSkip;
		const auto size = (width >= skips * minSkip + kThemeColumns * desired)
			? desired
			: (width - skips * minSkip) / kThemeColumns;
		if (size <= 0) {
			return;
		}
		const auto skip = (width - kThemeColumns * size) / float64(skips);
		auto top = 0;
		auto rowHeight = 0;
		for (auto i = 0; i != int(buttons.size()); ++i) {
			const auto column = i % kThemeColumns;
			if (i > 0 && !column) {
				top += rowHeight + rowSkip;
				rowHeight = 0;
			}
			const auto button = buttons[i];
			button->resizeToWidth(size);
			button->moveToLeft(
				padding.left() + int(base::SafeRound(column * (size + skip))),
				top);
			rowHeight = std::max(rowHeight, button->height());
		}
		raw->resize(raw->width(), top + rowHeight);
	}, raw->lifetime());

	return result;
}

void AddThemes(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		std::vector<Theme> themes) {
	auto keywords = QStringList{ u"тема"_q, u"темы"_q, u"theme"_q };
	for (const auto theme : themes) {
		keywords.push_back(ThemeNameNow(theme));
	}
	builder.addSubsectionTitle({
		.id = std::move(id),
		.title = std::move(title),
		.keywords = std::move(keywords),
	});
	builder.add([=](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = CreateThemesGrid(
				ctx.container,
				&ctx.controller->window(),
				themes),
		};
	});
}

void BuildDesign(SectionBuilder &builder) {
	builder.addSkip();
	AddThemes(
		builder,
		u"telewhite/design/light"_q,
		tr::lng_telewhite_mods_themes_light(),
		LightThemes());
	builder.addSkip();
	AddThemes(
		builder,
		u"telewhite/design/dark"_q,
		tr::lng_telewhite_mods_themes_dark(),
		DarkThemes());
	builder.addSkip();
	builder.addDividerText(GroupAbout(Group::Design));

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_app());
	AddDesignToggle(
		builder,
		u"icon"_q,
		&tr::lng_telewhite_mods_themed_icon,
		Mod::ThemedIcon,
		Scene::ThemedIcon,
		{ u"иконка"_q, u"значок"_q, u"icon"_q });
	AddFontButton(builder);
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_app_about());

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_chats_look());
	AddDesignToggle(
		builder,
		u"tails"_q,
		&tr::lng_telewhite_mods_no_bubble_tails,
		Mod::NoBubbleTails,
		Scene::NoBubbleTails,
		{ u"хвостики"_q, u"облачка"_q, u"пузыри"_q, u"bubbles"_q });
	AddDesignToggle(
		builder,
		u"author-online"_q,
		&tr::lng_telewhite_mods_author_online,
		Mod::AuthorOnline,
		Scene::AuthorOnline,
		{ u"онлайн"_q, u"в сети"_q, u"точка"_q, u"online"_q });
	AddDesignToggle(
		builder,
		u"story-rings"_q,
		&tr::lng_telewhite_mods_hide_story_rings,
		Mod::HideStoryRings,
		Scene::HideStoryRings,
		{ u"истории"_q, u"кольца"_q, u"stories"_q });
	auto ringColors = std::vector<QString>();
	for (const auto &color : StoryRingColors()) {
		ringColors.push_back(color.name);
	}
	AddChoice(builder, {
		.id = u"telewhite/design/story-ring-color"_q,
		.title = &tr::lng_telewhite_mods_story_ring_color,
		.option = Option::StoryRingColor,
		.options = std::move(ringColors),
		.keywords = { u"истории"_q, u"кольца"_q, u"цвет"_q },
		.shown = EnabledValue(
			Mod::HideStoryRings
		) | rpl::map([](bool hidden) { return !hidden; }),
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_chats_look_about());

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_effects());
	AddDesignToggle(
		builder,
		u"snowfall"_q,
		&tr::lng_telewhite_mods_snowfall,
		Mod::Snowfall,
		Scene::Snowfall,
		{ u"снег"_q, u"снегопад"_q, u"snow"_q });
	AddDesignToggle(
		builder,
		u"snowfall-chats"_q,
		&tr::lng_telewhite_mods_snowfall_chats,
		Mod::SnowfallChats,
		Scene::SnowfallChats,
		{ u"снег"_q, u"снегопад"_q, u"чаты"_q, u"snow"_q });
	const auto snowShown = [] {
		return rpl::combine(
			EnabledValue(Mod::Snowfall),
			EnabledValue(Mod::SnowfallChats)
		) | rpl::map([](bool window, bool chats) {
			return window || chats;
		});
	};
	AddChoice(builder, {
		.id = u"telewhite/design/snow-density"_q,
		.title = &tr::lng_telewhite_mods_snow_density,
		.option = Option::SnowDensity,
		.options = {
			tr::lng_telewhite_mods_snow_density_light(tr::now),
			tr::lng_telewhite_mods_snow_density_normal(tr::now),
			tr::lng_telewhite_mods_snow_density_heavy(tr::now),
			tr::lng_telewhite_mods_snow_density_blizzard(tr::now),
		},
		.keywords = { u"снег"_q, u"плотность"_q, u"метель"_q },
		.shown = snowShown(),
	});
	AddChoice(builder, {
		.id = u"telewhite/design/snow-speed"_q,
		.title = &tr::lng_telewhite_mods_snow_speed,
		.option = Option::SnowSpeed,
		.options = {
			tr::lng_telewhite_mods_snow_speed_slow(tr::now),
			tr::lng_telewhite_mods_snow_speed_normal(tr::now),
			tr::lng_telewhite_mods_snow_speed_fast(tr::now),
			tr::lng_telewhite_mods_snow_speed_very_fast(tr::now),
		},
		.keywords = { u"снег"_q, u"скорость"_q },
		.shown = snowShown(),
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_effects_about());
}

void BuildMusicExtras(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_telewhite_mods_music_now());
	builder.add([](const WidgetContext &ctx) {
		const auto skip = st::settingsButtonNoIcon.padding.left();
		return SectionBuilder::WidgetToAdd{
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				NowPlaying::StatusValue(
				) | rpl::map(NowPlaying::StatusText),
				st::boxLabel),
			.margin = QMargins(skip, 0, skip, 0),
		};
	});
	builder.addSkip();
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_music_now_about());
}

void CustomTextBox(not_null<Ui::GenericBox*> box, QByteArray key) {
	auto title = QString();
	auto hint = QString();
	for (const auto &entry : CustomTexts()) {
		if (entry.key == key) {
			title = entry.title;
			hint = entry.hint;
		}
	}
	box->setTitle(rpl::single(title));
	if (!hint.isEmpty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(hint),
			st::boxLabel));
	}
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_telewhite_texts_placeholder(),
		CustomTextValue(key)));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		SetCustomText(key, field->getLastText().trimmed());
		box->closeBox();
	};
	field->submits(
	) | rpl::on_next([=] {
		save();
	}, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addLeftButton(tr::lng_telewhite_texts_reset(), [=] {
		SetCustomText(key, QString());
		box->closeBox();
	});
    box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void BuildCustomTexts(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addSkip();
	for (const auto &entry : CustomTexts()) {
		const auto key = entry.key;
		builder.addButton({
			.id = u"telewhite/texts/"_q + QString::fromLatin1(key),
			.title = rpl::single(entry.title),
			.st = &st::settingsButtonNoIcon,
			.label = rpl::single(rpl::empty) | rpl::then(
				CustomTextsChanges()
			) | rpl::map([=] {
				const auto value = CustomTextValue(key);
				return value.isEmpty()
					? tr::lng_telewhite_texts_default(tr::now)
					: value;
			}),
			.onClick = [=] {
				controller->show(Box(CustomTextBox, key));
			},
			.keywords = { u"надписи"_q, u"статус"_q, u"печатает"_q, u"в сети"_q },
		});
	}
	builder.addSkip();
	builder.addDividerText(GroupAbout(Group::Texts));
}

void BuildGroup(SectionBuilder &builder, Group group) {
	if (group == Group::Design) {
		BuildDesign(builder);
		return;
	} else if (group == Group::Texts) {
		BuildCustomTexts(builder);
		return;
	}
	builder.addSkip();
	auto toggles = GroupToggles(group, builder.session());
	for (auto i = 0; i != int(toggles.size()); ++i) {
		AddToggle(builder, std::move(toggles[i]));
		if (group == Group::Location && !i) {
			AddFakePlace(builder);
		}
	}
	if (group == Group::Premium) {
		AddTranslatorChoice(builder);
		AddChoice(builder, {
			.id = u"telewhite/premium/translate-send-to"_q,
			.title = &tr::lng_telewhite_mods_translate_send_to,
			.option = Option::TranslateSendTo,
			.options = SendLanguageNames(),
			.keywords = { u"перевод"_q, u"язык"_q, u"language"_q },
			.shown = EnabledValue(Mod::TranslateBeforeSend),
		});
	}
	builder.addSkip();
	builder.addDividerText(GroupAbout(group));
	if (group == Group::Deleted) {
		BuildDeletedExtras(builder);
	} else if (group == Group::Music) {
		BuildMusicExtras(builder);
	}
}

void BuildMods(SectionBuilder &builder) {
	builder.addSkip();
	for (const auto group : {
		Group::Ghost,
		Group::Deleted,
		Group::Forward,
		Group::Sending,
		Group::Chats,
		Group::Premium,
		Group::Clean,
		Group::Profile,
		Group::Music,
		Group::Location,
		Group::Streamer,
	}) {
		AddGroupRow(builder, group);
	}
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	AddGroupRow(builder, Group::Design);
	AddGroupRow(builder, Group::Texts);
	builder.addSkip();
	builder.addDividerText(tr::lng_telewhite_mods_hint());
}

class ModsSection final : public Section<ModsSection> {
public:
	ModsSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

template <Group G>
class GroupSection final : public Section<GroupSection<G>> {
public:
	GroupSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

const auto kModsMeta = BuildHelper({
	.id = ModsSection::Id(),
	.parentId = MainId(),
	.title = &tr::lng_telewhite_mods_title,
	.icon = &st::menuIconStealth,
}, [](SectionBuilder &builder) {
	BuildMods(builder);
});

template <Group G>
[[nodiscard]] BuildHelper MakeGroupMeta() {
	return BuildHelper({
		.id = GroupSection<G>::Id(),
		.parentId = ModsSection::Id(),
		.title = GroupTitle(G),
		.icon = GroupIcon(G),
	}, [](SectionBuilder &builder) {
		BuildGroup(builder, G);
	});
}

const auto kGhostMeta = MakeGroupMeta<Group::Ghost>();
const auto kDeletedMeta = MakeGroupMeta<Group::Deleted>();
const auto kForwardMeta = MakeGroupMeta<Group::Forward>();
const auto kSendingMeta = MakeGroupMeta<Group::Sending>();
const auto kChatsMeta = MakeGroupMeta<Group::Chats>();
const auto kPremiumMeta = MakeGroupMeta<Group::Premium>();
const auto kCleanMeta = MakeGroupMeta<Group::Clean>();
const auto kProfileMeta = MakeGroupMeta<Group::Profile>();
const auto kMusicMeta = MakeGroupMeta<Group::Music>();
const auto kLocationMeta = MakeGroupMeta<Group::Location>();
const auto kStreamerMeta = MakeGroupMeta<Group::Streamer>();
const auto kDesignMeta = MakeGroupMeta<Group::Design>();
const auto kTextsMeta = MakeGroupMeta<Group::Texts>();

[[nodiscard]] const BuildHelper &GroupMeta(Group group) {
	switch (group) {
	case Group::Ghost: return kGhostMeta;
	case Group::Deleted: return kDeletedMeta;
	case Group::Forward: return kForwardMeta;
	case Group::Sending: return kSendingMeta;
	case Group::Chats: return kChatsMeta;
	case Group::Premium: return kPremiumMeta;
	case Group::Clean: return kCleanMeta;
	case Group::Profile: return kProfileMeta;
	case Group::Music: return kMusicMeta;
	case Group::Location: return kLocationMeta;
	case Group::Streamer: return kStreamerMeta;
	case Group::Design: return kDesignMeta;
	case Group::Texts: return kTextsMeta;
	}
	Unexpected("Group in Telewhite::GroupMeta.");
}

ModsSection::ModsSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kModsMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> ModsSection::title() {
	return tr::lng_telewhite_mods_title();
}

template <Group G>
GroupSection<G>::GroupSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section<GroupSection<G>>(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	this->build(content, GroupMeta(G).build);
	Ui::ResizeFitChild(this, content);
}

template <Group G>
rpl::producer<QString> GroupSection<G>::title() {
	return (*GroupTitle(G))();
}

Type GroupId(Group group) {
	switch (group) {
	case Group::Ghost: return GroupSection<Group::Ghost>::Id();
	case Group::Deleted: return GroupSection<Group::Deleted>::Id();
	case Group::Forward: return GroupSection<Group::Forward>::Id();
	case Group::Sending: return GroupSection<Group::Sending>::Id();
	case Group::Chats: return GroupSection<Group::Chats>::Id();
	case Group::Premium: return GroupSection<Group::Premium>::Id();
	case Group::Clean: return GroupSection<Group::Clean>::Id();
	case Group::Profile: return GroupSection<Group::Profile>::Id();
	case Group::Music: return GroupSection<Group::Music>::Id();
	case Group::Location: return GroupSection<Group::Location>::Id();
	case Group::Streamer: return GroupSection<Group::Streamer>::Id();
	case Group::Design: return GroupSection<Group::Design>::Id();
	case Group::Texts: return GroupSection<Group::Texts>::Id();
	}
	Unexpected("Group in Telewhite::GroupId.");
}

} // namespace

Settings::Type ModsSectionId() {
	return ModsSection::Id();
}

} // namespace Telewhite
