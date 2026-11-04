/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_mods_box.h"

#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "telewhite/telewhite_mods.h"
#include "telewhite/telewhite_mods_preview.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Telewhite {
namespace {

using Preview::Scene;

constexpr auto kStateDuration = crl::time(260);
constexpr auto kPanelsPadding = 14;

[[nodiscard]] double CurrentScale() {
	return style::Scale() / 100.;
}

[[nodiscard]] Preview::Palette PreviewPalette() {
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

// "Without mod" / "with mod" panels, the current one is highlighted.
class ScenePanels final : public Ui::RpWidget {
public:
	ScenePanels(QWidget *parent, Scene scene, rpl::producer<bool> value);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const Scene _scene;
	Ui::Animations::Simple _animation;
	bool _on = false;
	bool _initialized = false;

};

ScenePanels::ScenePanels(
	QWidget *parent,
	Scene scene,
	rpl::producer<bool> value)
: RpWidget(parent)
, _scene(scene) {
	std::move(
		value
	) | rpl::on_next([=](bool on) {
		if (!_initialized) {
			_initialized = true;
			_on = on;
			update();
		} else if (_on != on) {
			_on = on;
			_animation.start(
				[=] { update(); },
				on ? 0. : 1.,
				on ? 1. : 0.,
				kStateDuration,
				anim::easeOutCirc);
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

int ScenePanels::resizeGetHeight(int) {
	return Preview::PanelsHeight(CurrentScale());
}

void ScenePanels::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	Preview::PaintPanels(
		p,
		QRectF(rect()),
		_scene,
		_animation.value(_on ? 1. : 0.),
		PreviewPalette());
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

} // namespace

void HowItWorksBox(
		not_null<Ui::GenericBox*> box,
		Scene scene,
		rpl::producer<bool> value,
		Fn<void(bool)> set) {
	box->setStyle(st::layerBox);
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(Preview::SceneTitle(scene)));

	const auto state = box->lifetime().make_state<rpl::variable<bool>>(
		std::move(value));

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Preview::SceneHint(scene)),
			st::boxLabel));
	Ui::AddSkip(box->verticalLayout());

	const auto padding = style::ConvertScale(kPanelsPadding);
	box->addRow(
		object_ptr<ScenePanels>(box, scene, state->value()),
		style::margins(padding, 0, padding, 0));
	Ui::AddSkip(box->verticalLayout());

	const auto toggle = box->addRow(
		object_ptr<Ui::SettingsButton>(
			box,
			tr::lng_telewhite_preview_enabled(),
			st::settingsButtonNoIcon),
		style::margins());
	toggle->toggleOn(state->value());
	toggle->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != state->current());
	}) | rpl::on_next([=](bool toggled) {
		set(toggled);
	}, toggle->lifetime());

	box->addButton(tr::lng_telewhite_preview_ok(), [=] { box->closeBox(); });
}

object_ptr<Ui::RpWidget> CreateModsBanner(
		not_null<QWidget*> parent,
		Fn<void()> open) {
	auto result = object_ptr<ModsBanner>(parent.get());
	result->setClickedCallback(std::move(open));
	return result;
}

} // namespace Telewhite
