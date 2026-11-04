/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_settings.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "telewhite/telewhite_mods.h"
#include "telewhite/telewhite_mods_box.h"
#include "telewhite/telewhite_mods_preview.h"
#include "telewhite/telewhite_themes.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
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
	Premium,
	Clean,
	Profile,
	Streamer,
	Design,
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
};

struct GroupStatus {
	QString text;
	bool active = false;
};

// Toggles changed from Telewhite, Telegram settings don't notify about it.
rpl::event_stream<bool> ShowSecondsChanges;

[[nodiscard]] not_null<const tr::phrase<>*> GroupTitle(Group group) {
	switch (group) {
	case Group::Ghost: return &tr::lng_telewhite_mods_ghost;
	case Group::Deleted: return &tr::lng_telewhite_mods_deleted;
	case Group::Forward: return &tr::lng_telewhite_mods_forward;
	case Group::Sending: return &tr::lng_telewhite_mods_sending;
	case Group::Premium: return &tr::lng_telewhite_mods_premium;
	case Group::Clean: return &tr::lng_telewhite_mods_clean;
	case Group::Profile: return &tr::lng_telewhite_mods_profile;
	case Group::Streamer: return &tr::lng_telewhite_mods_streamer;
	case Group::Design: return &tr::lng_telewhite_mods_design;
	}
	Unexpected("Group in Telewhite::GroupTitle.");
}

[[nodiscard]] not_null<const style::icon*> GroupIcon(Group group) {
	switch (group) {
	case Group::Ghost: return &st::menuIconStealth;
	case Group::Deleted: return &st::menuIconDelete;
	case Group::Forward: return &st::menuIconForward;
	case Group::Sending: return &st::menuIconSend;
	case Group::Premium: return &st::menuIconPremium;
	case Group::Clean: return &st::menuIconCaptionHide;
	case Group::Profile: return &st::menuIconTimer;
	case Group::Streamer: return &st::menuIconStartStream;
	case Group::Design: return &st::menuIconPalette;
	}
	Unexpected("Group in Telewhite::GroupIcon.");
}

[[nodiscard]] rpl::producer<QString> GroupAbout(Group group) {
	switch (group) {
	case Group::Ghost: return tr::lng_telewhite_mods_ghost_about();
	case Group::Deleted: return tr::lng_telewhite_mods_deleted_about();
	case Group::Forward: return tr::lng_telewhite_mods_forward_about();
	case Group::Sending: return tr::lng_telewhite_mods_sending_about();
	case Group::Premium: return tr::lng_telewhite_mods_local_pins_about();
	case Group::Clean: return tr::lng_telewhite_mods_hide_phone_about();
	case Group::Profile: return tr::lng_telewhite_mods_profile_about();
	case Group::Streamer: return tr::lng_telewhite_mods_streamer_about();
	case Group::Design: return tr::lng_telewhite_mods_themes_about();
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
			u"deleted/edits"_q,
			&tr::lng_telewhite_mods_edit_history,
			Mod::EditHistory,
			Scene::EditHistory,
			{ u"изменённые"_q, u"измененные"_q, u"edit"_q });
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
		break;
	case Group::Premium:
		mod(
			u"premium/pins"_q,
			&tr::lng_telewhite_mods_local_pins,
			Mod::LocalPins,
			Scene::LocalPins,
			{ u"закреп"_q, u"лимит"_q, u"pin"_q });
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
	case Group::Streamer:
		mod(
			u"streamer/hide"_q,
			&tr::lng_telewhite_mods_streamer_hide,
			Mod::StreamerMode,
			Scene::Streamer,
			{ u"стример"_q, u"запись экрана"_q, u"скриншот"_q, u"stream"_q });
		break;
	case Group::Design:
		mod(
			u"design/snowfall"_q,
			&tr::lng_telewhite_mods_snowfall,
			Mod::Snowfall,
			Scene::Snowfall,
			{ u"снег"_q, u"снегопад"_q, u"snow"_q });
		break;
	}
	return result;
}

[[nodiscard]] rpl::producer<GroupStatus> GroupStatusValue(
		Group group,
		not_null<Main::Session*> session) {
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
		if (!toggle.master) {
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
	// Only the Windows platform code consults streamer mode.
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

	const auto help = Ui::CreateChild<HelpButton>(button);
	const auto gap = style::ConvertScale(4);
	auto padding = st.padding;
	padding.setRight(gap + help->width() + st.padding.right() / 2);
	button->setPaddingOverride(padding);

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
	builder.addSubsectionTitle(tr::lng_telewhite_mods_effects());
	for (auto &toggle : GroupToggles(Group::Design, builder.session())) {
		AddToggle(builder, std::move(toggle));
	}
	builder.addSkip();
}

void BuildGroup(SectionBuilder &builder, Group group) {
	if (group == Group::Design) {
		BuildDesign(builder);
		return;
	}
	builder.addSkip();
	for (auto &toggle : GroupToggles(group, builder.session())) {
		AddToggle(builder, std::move(toggle));
	}
	builder.addSkip();
	builder.addDividerText(GroupAbout(group));
}

void BuildMods(SectionBuilder &builder) {
	builder.addSkip();
	for (const auto group : {
		Group::Ghost,
		Group::Deleted,
		Group::Forward,
		Group::Sending,
		Group::Premium,
		Group::Clean,
		Group::Profile,
		Group::Streamer,
	}) {
		AddGroupRow(builder, group);
	}
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	AddGroupRow(builder, Group::Design);
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
const auto kPremiumMeta = MakeGroupMeta<Group::Premium>();
const auto kCleanMeta = MakeGroupMeta<Group::Clean>();
const auto kProfileMeta = MakeGroupMeta<Group::Profile>();
const auto kStreamerMeta = MakeGroupMeta<Group::Streamer>();
const auto kDesignMeta = MakeGroupMeta<Group::Design>();

[[nodiscard]] const BuildHelper &GroupMeta(Group group) {
	switch (group) {
	case Group::Ghost: return kGhostMeta;
	case Group::Deleted: return kDeletedMeta;
	case Group::Forward: return kForwardMeta;
	case Group::Sending: return kSendingMeta;
	case Group::Premium: return kPremiumMeta;
	case Group::Clean: return kCleanMeta;
	case Group::Profile: return kProfileMeta;
	case Group::Streamer: return kStreamerMeta;
	case Group::Design: return kDesignMeta;
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
	case Group::Premium: return GroupSection<Group::Premium>::Id();
	case Group::Clean: return GroupSection<Group::Clean>::Id();
	case Group::Profile: return GroupSection<Group::Profile>::Id();
	case Group::Streamer: return GroupSection<Group::Streamer>::Id();
	case Group::Design: return GroupSection<Group::Design>::Id();
	}
	Unexpected("Group in Telewhite::GroupId.");
}

} // namespace

Settings::Type ModsSectionId() {
	return ModsSection::Id();
}

} // namespace Telewhite
