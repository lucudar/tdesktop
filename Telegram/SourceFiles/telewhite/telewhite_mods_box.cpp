/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_mods_box.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "telewhite/telewhite_amoled_theme.h"
#include "telewhite/telewhite_light_theme.h"
#include "telewhite/telewhite_mods.h"
#include "telewhite/telewhite_mods_preview.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Telewhite {
namespace {

using Preview::Scene;

constexpr auto kSceneDuration = crl::time(180);
constexpr auto kStateDuration = crl::time(260);

constexpr auto kGhostMods = std::array{
	Mod::GhostRead,
	Mod::GhostTyping,
	Mod::GhostOnline,
	Mod::GhostStories,
};

[[nodiscard]] double CurrentScale() {
	return style::Scale() / 100.;
}

// "How it works" card pinned above the list of mods.
class ModsPreview final : public Ui::RpWidget {
public:
	ModsPreview(QWidget *parent, Scene scene, bool on);

	void showScene(Scene scene, bool on);
	void setFrozen(bool frozen);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] Preview::Palette previewPalette() const;

	Scene _scene = Scene::GhostAll;
	Scene _was = Scene::GhostAll;
	double _wasState = 0.;
	bool _on = false;
	bool _frozen = false;
	Ui::Animations::Simple _sceneAnimation;
	Ui::Animations::Simple _stateAnimation;

};

ModsPreview::ModsPreview(QWidget *parent, Scene scene, bool on)
: RpWidget(parent)
, _scene(scene)
, _was(scene)
, _on(on) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void ModsPreview::showScene(Scene scene, bool on) {
	if (_frozen) {
		return;
	}
	const auto state = _stateAnimation.value(_on ? 1. : 0.);
	if (scene != _scene) {
		_was = _scene;
		_wasState = state;
		_scene = scene;
		_on = on;
		_stateAnimation.stop();
		_sceneAnimation.stop();
		_sceneAnimation.start([=] { update(); }, 0., 1., kSceneDuration);
		update();
	} else if (on != _on) {
		_on = on;
		_stateAnimation.start(
			[=] { update(); },
			state,
			on ? 1. : 0.,
			kStateDuration,
			anim::easeOutCirc);
	}
}

void ModsPreview::setFrozen(bool frozen) {
	_frozen = frozen;
}

int ModsPreview::resizeGetHeight(int) {
	return Preview::CardHeight(CurrentScale()) + st::lineWidth;
}

void ModsPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::boxBg);
	p.fillRect(
		0,
		height() - st::lineWidth,
		width(),
		st::lineWidth,
		st::shadowFg);

	const auto palette = previewPalette();
	const auto card = QRectF(0., 0., width(), height() - st::lineWidth);
	const auto progress = _sceneAnimation.value(1.);
	if (progress < 1.) {
		p.setOpacity(1. - progress);
		Preview::PaintCard(p, card, _was, _wasState, palette);
		p.setOpacity(progress);
	}
	Preview::PaintCard(
		p,
		card,
		_scene,
		_stateAnimation.value(_on ? 1. : 0.),
		palette);
}

Preview::Palette ModsPreview::previewPalette() const {
	return {
		.bg = st::boxBg->c,
		.fg = st::windowFg->c,
		.sub = st::windowSubTextFg->c,
		.accent = st::windowBgActive->c,
		.accentFg = st::windowFgActive->c,
		.inBg = st::msgInBg->c,
		.inFg = st::historyTextInFg->c,
		.inSub = st::msgInDateFg->c,
		.outBg = st::msgOutBg->c,
		.outFg = st::historyTextOutFg->c,
		.outSub = st::msgOutDateFg->c,
		.attention = st::attentionButtonFg->c,
		.online = st::dialogsOnlineBadgeFg->c,
		.font = st::normalFont->f,
		.scale = CurrentScale(),
	};
}

// Bright card in the main settings that opens the mods box.
class ModsBanner final : public Ui::RippleButton {
public:
	explicit ModsBanner(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

private:
	QString _about;
	Ui::Animations::Simple _overAnimation;
	bool _over = false;

};

ModsBanner::ModsBanner(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation) {
	setCursor(style::cur_pointer);

	auto values = std::vector<rpl::producer<int>>();
	for (auto i = 0; i != int(Mod::kCount); ++i) {
		const auto mod = Mod(i);
		// Only the Windows platform code consults streamer mode.
		if (mod != Mod::StreamerMode || Platform::IsWindows()) {
			values.push_back(EnabledValue(mod) | rpl::map([](bool value) {
				return value ? 1 : 0;
			}));
		}
	}
	rpl::combine(
		std::move(values)
	) | rpl::on_next([=](const std::vector<int> &list) {
		auto enabled = 0;
		for (const auto value : list) {
			enabled += value;
		}
		_about = enabled
			? tr::lng_telewhite_mods_banner_enabled(
				tr::now,
				lt_amount,
				QString::number(enabled),
				lt_total,
				QString::number(list.size()))
			: tr::lng_telewhite_mods_banner_about(tr::now);
		update();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

int ModsBanner::resizeGetHeight(int) {
	return Preview::BannerHeight(CurrentScale());
}

void ModsBanner::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	Preview::PaintBanner(p, QRectF(rect()), {
		.title = tr::lng_telewhite_mods_button(tr::now),
		.about = _about,
		.font = st::normalFont->f,
		.scale = CurrentScale(),
		.over = _overAnimation.value(_over ? 1. : 0.),
	});
	const auto ripple = QColor(255, 255, 255, 56);
	paintRipple(p, 0, 0, &ripple);
}

void ModsBanner::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	const auto over = isOver();
	if (_over != over) {
		_over = over;
		_overAnimation.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			st::universalDuration);
	}
}

QImage ModsBanner::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		Preview::BannerRadius(CurrentScale()));
}

void BindPreview(
		not_null<Ui::SettingsButton*> button,
		not_null<ModsPreview*> preview,
		Scene scene,
		bool toggle) {
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		preview->showScene(scene, button->toggled());
	}, button->lifetime());
	if (toggle) {
		button->toggledChanges(
		) | rpl::on_next([=](bool toggled) {
			preview->showScene(scene, toggled);
		}, button->lifetime());
	}
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

[[nodiscard]] bool GhostAllEnabled() {
	return ranges::all_of(kGhostMods, [](Mod mod) { return Enabled(mod); });
}

void ApplyThemeFile(const QString &name, const QByteArray &content) {
	const auto folder = cWorkingDir() + u"tdata"_q;
	QDir().mkpath(folder);
	const auto path = folder + u"/"_q + name;
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	const auto written = file.write(content);
	file.close();
	if (written != content.size()) {
		return;
	}
	if (Window::Theme::Apply(path)) {
		Window::Theme::KeepApplied();
	}
}

void ApplyAmoledTheme() {
	ApplyThemeFile(
		u"telewhite-amoled.tdesktop-theme"_q,
		QByteArray::fromRawData(
			reinterpret_cast<const char*>(kAmoledThemeData),
			sizeof(kAmoledThemeData)));
}

void ApplyLightTheme() {
	ApplyThemeFile(
		u"telewhite-light.tdesktop-palette"_q,
		QByteArray(kLightPalette.data(), kLightPalette.size()));
}

void ApplyDarkTheme(std::shared_ptr<Ui::Show> show) {
	using namespace Window::Theme;
	const auto schemes = EmbeddedThemes();
	const auto night = ranges::find(
		schemes,
		EmbeddedType::Night,
		&EmbeddedScheme::type);
	if (night == end(schemes)) {
		return;
	}
	const auto apply = [path = night->path] {
		auto &settings = Core::App().settings();
		settings.themesAccentColors().set(
			EmbeddedType::Night,
			QColor(0x7a, 0x5c, 0xcf));
		settings.setSystemAccentColorEnabled(false);
		Core::App().saveSettingsDelayed();
		if (IsNightMode()) {
			ApplyDefaultWithPath(path);
		} else {
			ToggleNightMode(path);
		}
		KeepApplied();
	};
	if (IsNightMode() || Background()->nightModeChangeAllowed()) {
		apply();
		return;
	}
	show->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_auto_night_warning(),
		.confirmed = [=](Fn<void()> close) {
			Core::App().settings().setSystemDarkModeEnabled(false);
			Core::App().saveSettingsDelayed();
			apply();
			close();
		},
		.confirmText = tr::lng_settings_auto_night_disable(),
	}));
}

void AddToggle(
		not_null<Ui::VerticalLayout*> container,
		not_null<ModsPreview*> preview,
		rpl::producer<QString> text,
		Scene scene,
		rpl::producer<bool> value,
		Fn<void(bool)> callback) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		std::move(text),
		st::settingsButtonNoIcon));
	button->toggleOn(std::move(value));
	button->toggledChanges(
	) | rpl::on_next(std::move(callback), button->lifetime());
	BindPreview(button, preview, scene, true);
}

void AddModToggle(
		not_null<Ui::VerticalLayout*> container,
		not_null<ModsPreview*> preview,
		rpl::producer<QString> text,
		Mod mod,
		Scene scene) {
	AddToggle(
		container,
		preview,
		std::move(text),
		scene,
		EnabledValue(mod),
		[=](bool value) { SetEnabled(mod, value); });
}

void AddGhostAllToggle(
		not_null<Ui::VerticalLayout*> container,
		not_null<ModsPreview*> preview) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_telewhite_mods_ghost_all(tr::bold),
		st::settingsButtonNoIcon));
	button->toggleOn(GhostAllValue(), true);
	button->addClickHandler([=] {
		const auto enable = !GhostAllEnabled();
		preview->setFrozen(true);
		for (const auto mod : kGhostMods) {
			SetEnabled(mod, enable);
		}
		preview->setFrozen(false);
		preview->showScene(Scene::GhostAll, enable);
	});
	BindPreview(button, preview, Scene::GhostAll, false);
}

void AddThemeButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<ModsPreview*> preview,
		rpl::producer<QString> text,
		Scene scene,
		Fn<void()> apply) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		std::move(text),
		st::settingsButtonNoIcon));
	button->addClickHandler(std::move(apply));
	BindPreview(button, preview, scene, false);
}

void AddSection(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, std::move(title));
}

} // namespace

void ModsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setStyle(st::layerBox);
	box->setTitle(tr::lng_telewhite_mods_title());
	box->setWidth(st::boxWideWidth);

	const auto preview = box->setPinnedToTopContent(
		object_ptr<ModsPreview>(box, Scene::GhostAll, GhostAllEnabled()));

	const auto container = box->verticalLayout();

	AddSection(container, tr::lng_telewhite_mods_ghost());
	AddGhostAllToggle(container, preview);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_ghost_read(),
		Mod::GhostRead,
		Scene::GhostRead);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_ghost_typing(),
		Mod::GhostTyping,
		Scene::GhostTyping);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_ghost_online(),
		Mod::GhostOnline,
		Scene::GhostOnline);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_ghost_stories(),
		Mod::GhostStories,
		Scene::GhostStories);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_ghost_about());

	// Only the Windows platform code consults streamer mode.
	if (Platform::IsWindows()) {
		AddSection(container, tr::lng_telewhite_mods_streamer());
		AddModToggle(
			container,
			preview,
			tr::lng_telewhite_mods_streamer_hide(),
			Mod::StreamerMode,
			Scene::Streamer);
		Ui::AddSkip(container);
		Ui::AddDividerText(
			container,
			tr::lng_telewhite_mods_streamer_about());
	}

	AddSection(container, tr::lng_telewhite_mods_messages());
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_save_deleted(),
		Mod::SaveDeleted,
		Scene::SaveDeleted);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_edit_history(),
		Mod::EditHistory,
		Scene::EditHistory);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_forward_no_author(),
		Mod::ForwardNoAuthor,
		Scene::ForwardNoAuthor);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_multi_forward(),
		Mod::MultiForward,
		Scene::MultiForward);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_messages_about());

	AddSection(container, tr::lng_telewhite_mods_sending());
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_confirm_stickers(),
		Mod::ConfirmStickers,
		Scene::ConfirmStickers);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_confirm_gifs(),
		Mod::ConfirmGifs,
		Scene::ConfirmGifs);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_confirm_voice(),
		Mod::ConfirmVoice,
		Scene::ConfirmVoice);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_sending_about());

	AddSection(container, tr::lng_telewhite_mods_chats());
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_local_pins(),
		Mod::LocalPins,
		Scene::LocalPins);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_local_pins_about());

	AddSection(container, tr::lng_telewhite_mods_clean());
	AddToggle(
		container,
		preview,
		tr::lng_settings_chat_hide_stories(),
		Scene::HideStories,
		rpl::single(Core::App().settings().hideStories()),
		[](bool value) {
			Core::App().settings().setHideStories(value);
			Core::App().saveSettingsDelayed();
		});
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_hide_sponsored(),
		Mod::HideSponsored,
		Scene::HideSponsored);
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_hide_emoji_status(),
		Mod::HideEmojiStatus,
		Scene::HideEmojiStatus);
	AddToggle(
		container,
		preview,
		tr::lng_telewhite_mods_hide_phone(),
		Scene::HidePhone,
		rpl::single(session->settings().phoneNumberHidden()),
		[=](bool value) {
			session->settings().setPhoneNumberHidden(value);
			session->saveSettingsDelayed();
		});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_hide_phone_about());

	AddSection(container, tr::lng_telewhite_mods_info());
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_precise_last_seen(),
		Mod::PreciseLastSeen,
		Scene::PreciseLastSeen);
	AddToggle(
		container,
		preview,
		tr::lng_settings_chat_show_seconds(),
		Scene::ShowSeconds,
		rpl::single(Core::App().settings().showSeconds()),
		[](bool value) {
			Core::App().settings().setShowSeconds(value);
			Core::App().saveSettingsDelayed();
		});
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_registration_date(),
		Mod::RegistrationDate,
		Scene::RegistrationDate);

	AddSection(container, tr::lng_telewhite_mods_design());
	AddModToggle(
		container,
		preview,
		tr::lng_telewhite_mods_snowfall(),
		Mod::Snowfall,
		Scene::Snowfall);
	AddThemeButton(
		container,
		preview,
		tr::lng_telewhite_mods_light(),
		Scene::ThemeLight,
		ApplyLightTheme);
	AddThemeButton(
		container,
		preview,
		tr::lng_telewhite_mods_dark(),
		Scene::ThemeDark,
		[=] { ApplyDarkTheme(box->uiShow()); });
	AddThemeButton(
		container,
		preview,
		tr::lng_telewhite_mods_amoled(),
		Scene::ThemeAmoled,
		ApplyAmoledTheme);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_telewhite_mods_restart_about());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

object_ptr<Ui::RpWidget> CreateModsBanner(
		not_null<QWidget*> parent,
		Fn<void()> open) {
	auto result = object_ptr<ModsBanner>(parent.get());
	result->setClickedCallback(std::move(open));
	return result;
}

} // namespace Telewhite
