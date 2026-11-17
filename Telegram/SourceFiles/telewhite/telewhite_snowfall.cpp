/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_snowfall.h"

#include "base/random.h"
#include "base/timer.h"
#include "telewhite/telewhite_glass_backdrop.h"
#include "telewhite/telewhite_mods.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_basic.h"
#include "styles/palette.h"

#include <QtCore/QPointer>

#include <cmath>

namespace Telewhite {
namespace {

constexpr auto kFrameDelay = crl::time(33);
constexpr auto kRaiseEveryFrames = 30;

// By the "density" option: 0 - light snow .. 3 - blizzard.
constexpr auto kFlakesPerMegapixel = std::array{ 45., 90., 160., 260. };
constexpr auto kMaxFlakes = std::array{ 120, 220, 380, 600 };

// By the "speed" option: 0 - slow .. 3 - very fast.
constexpr auto kSpeeds = std::array{ 0.5, 1., 1.6, 2.4 };

// The input area capsule: margins to the wallpaper and the corners.
constexpr auto kComposeSide = 6;
constexpr auto kComposeTop = 4;
constexpr auto kComposeBottom = 5;
constexpr auto kComposeRadius = 21;
constexpr auto kComposeMinHeight = 24;

[[nodiscard]] double RandomUnit() {
	return base::RandomValue<uint32>() / double(0xFFFFFFFFU);
}

[[nodiscard]] int OptionIndex(Option option) {
	return std::clamp(OptionNow(option), 0, 3);
}

[[nodiscard]] QColor FlakeColor() {
	const auto white = QColor(255, 255, 255);
	if (st::windowBg->c.lightnessF() < 0.5) {
		return white;
	}
	// White snow is invisible in light themes, it gets the accent tint.
	const auto accent = st::windowBgActive->c;
	const auto mix = [](int a, int b) {
		return a + (b - a) * 3 / 10;
	};
	return QColor(
		mix(accent.red(), 255),
		mix(accent.green(), 255),
		mix(accent.blue(), 255));
}

[[nodiscard]] float64 ComposeRadius(QRect capsule) {
	return std::min(
		capsule.height() / 2.,
		float64(style::ConvertScale(kComposeRadius)));
}

[[nodiscard]] bool ComposeDark() {
	return (st::historyComposeAreaBg->c.lightnessF() < 0.5);
}

class SnowOverlay final : public Ui::RpWidget {
public:
	SnowOverlay(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<bool> active,
		rpl::producer<QRect> geometry);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Flake {
		double x = 0.;
		double y = 0.;
		double speed = 0.;
		double sway = 0.;
		double phase = 0.;
		double radius = 0.;
		double opacity = 0.;
	};

	void setActive(bool active);
	void refill();
	void respawn(Flake &flake, bool anywhere);
	void tick();
	[[nodiscard]] QRect flakeRect(const Flake &flake) const;

	std::vector<Flake> _flakes;
	QColor _color;
	base::Timer _timer;
	crl::time _last = 0;
	int _frame = 0;
	bool _active = false;

};

// The input area of the chat becomes a floating capsule: the wallpaper
// is painted around it under all the controls, they stay where they are.
class ComposeGlass final : public Ui::RpWidget {
public:
	ComposeGlass(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QRect> scrollGeometry);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void ensureField();
	void refresh();
	[[nodiscard]] bool shouldPaint() const;
	[[nodiscard]] QRect capsuleRect() const;
	void validateFrame(QRect capsule);
	void paintRim(QPainter &p, QRect capsule) const;

	const not_null<Ui::RpWidget*> _owner;
	QPointer<Ui::InputField> _field;
	QRect _scroll;
	QImage _frame;
	QRect _frameCapsule;
	QSize _frameSize;
	QSize _frameFill;
	QPoint _frameShift;
	quint64 _frameKey = 0;

};

SnowOverlay::SnowOverlay(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<bool> active,
	rpl::producer<QRect> geometry)
: RpWidget(parent.get())
, _color(FlakeColor())
, _timer([=] { tick(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	hide();

	std::move(
		geometry
	) | rpl::on_next([=](QRect rect) {
		setGeometry(rect);
		refill();
	}, lifetime());

	std::move(
		active
	) | rpl::on_next([=](bool enabled) {
		setActive(enabled);
	}, lifetime());

	OptionValue(
		Option::SnowDensity
	) | rpl::skip(1) | rpl::on_next([=] {
		refill();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_color = FlakeColor();
		update();
	}, lifetime());
}

void SnowOverlay::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	if (_active) {
		refill();
		show();
		raise();
		_last = crl::now();
		_timer.callEach(kFrameDelay);
	} else {
		_timer.cancel();
		_flakes.clear();
		hide();
	}
}

void SnowOverlay::refill() {
	if (!_active || width() <= 0 || height() <= 0) {
		return;
	}
	const auto area = double(width()) * double(height()) / 1000000.;
	const auto density = OptionIndex(Option::SnowDensity);
	const auto count = std::clamp(
		int(area * kFlakesPerMegapixel[density]),
		12,
		kMaxFlakes[density]);
	const auto was = int(_flakes.size());
	_flakes.resize(count);
	for (auto i = was; i < count; ++i) {
		respawn(_flakes[i], true);
	}
	update();
}

void SnowOverlay::respawn(Flake &flake, bool anywhere) {
	const auto unit = style::Scale() / 100.;
	flake.radius = (1.2 + RandomUnit() * 2.3) * unit;
	flake.x = RandomUnit() * width();
	flake.y = anywhere
		? (RandomUnit() * height())
		: -(flake.radius * 2. + RandomUnit() * 20. * unit);
	flake.speed = (18. + RandomUnit() * 38.) * unit * (flake.radius / unit);
	flake.sway = (6. + RandomUnit() * 18.) * unit;
	flake.phase = RandomUnit() * 6.2831853;
	flake.opacity = 0.45 + RandomUnit() * 0.5;
}

QRect SnowOverlay::flakeRect(const Flake &flake) const {
	const auto x = flake.x + std::sin(flake.phase) * flake.sway;
	const auto r = int(std::ceil(flake.radius)) + 2;
	return QRect(int(x) - r, int(flake.y) - r, r * 2 + 1, r * 2 + 1);
}

void SnowOverlay::tick() {
	const auto topLevel = window();
	if (!topLevel
		|| !topLevel->isVisible()
		|| topLevel->isMinimized()
		|| !isVisible()) {
		_last = crl::now();
		return;
	}
	const auto now = crl::now();
	const auto dt = std::clamp((now - _last) / 1000., 0., 0.1);
	const auto speed = kSpeeds[OptionIndex(Option::SnowSpeed)];
	_last = now;

	auto region = QRegion();
	for (auto &flake : _flakes) {
		region += flakeRect(flake);
		flake.y += flake.speed * speed * dt;
		flake.phase += dt * 1.3 * speed;
		if (flake.y - flake.radius > height()) {
			respawn(flake, false);
		}
		region += flakeRect(flake);
	}
	if (++_frame >= kRaiseEveryFrames) {
		_frame = 0;
		raise();
	}
	update(region);
}

void SnowOverlay::paintEvent(QPaintEvent *e) {
	if (_flakes.empty()) {
		return;
	}
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	const auto clip = e->rect();
	for (const auto &flake : _flakes) {
		const auto rect = flakeRect(flake);
		if (!rect.intersects(clip)) {
			continue;
		}
		const auto x = flake.x + std::sin(flake.phase) * flake.sway;
		p.setOpacity(flake.opacity);
		p.setBrush(_color);
		p.drawEllipse(QPointF(x, flake.y), flake.radius, flake.radius);
	}
}

ComposeGlass::ComposeGlass(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QRect> scrollGeometry)
: RpWidget(parent.get())
, _owner(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	hide();

	ensureField();

	std::move(
		scrollGeometry
	) | rpl::on_next([=](QRect scroll) {
		_scroll = scroll;
		refresh();
	}, lifetime());

	parent->sizeValue(
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	EnabledValue(
		Mod::GlassUi
	) | rpl::skip(1) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_frame = QImage();
		update();
	}, lifetime());
}

void ComposeGlass::ensureField() {
	if (_field) {
		return;
	}
	for (const auto child : _owner->children()) {
		if (const auto input = dynamic_cast<Ui::InputField*>(child)) {
			_field = input;
			input->shownValue(
			) | rpl::skip(1) | rpl::on_next([=] {
				refresh();
			}, lifetime());
			return;
		}
	}
}

void ComposeGlass::refresh() {
	ensureField();
	const auto input = _field.data();
	const auto top = _scroll.y() + _scroll.height() - st::lineWidth;
	const auto bottom = _owner->height();
	const auto wanted = Enabled(Mod::GlassUi)
		&& input
		&& !input->isHidden()
		&& !_scroll.isEmpty()
		&& (top > 0)
		&& (bottom - top >= style::ConvertScale(kComposeMinHeight));
	if (!wanted) {
		if (!isHidden()) {
			hide();
		}
		return;
	}
	setGeometry(0, top, _owner->width(), bottom - top);
	if (isHidden()) {
		show();
	}
	lower();
	update();
}

bool ComposeGlass::shouldPaint() const {
	const auto input = _field.data();
	return Enabled(Mod::GlassUi) && input && !input->isHidden();
}

QRect ComposeGlass::capsuleRect() const {
	auto bottom = height();
	for (const auto child : _owner->children()) {
		const auto keyboard = dynamic_cast<Ui::ScrollArea*>(child);
		if (!keyboard
			|| keyboard->isHidden()
			|| (keyboard->geometry() == _scroll)) {
			continue;
		}
		const auto keyboardTop = keyboard->y() - y();
		if (keyboardTop > 0 && keyboardTop < bottom) {
			bottom = keyboardTop;
		}
	}
	const auto side = style::ConvertScale(kComposeSide);
	const auto top = st::lineWidth + style::ConvertScale(kComposeTop);
	const auto result = QRect(
		side,
		top,
		width() - 2 * side,
		bottom - top - style::ConvertScale(kComposeBottom));
	return ((result.width() > 0)
		&& (result.height() >= style::ConvertScale(kComposeMinHeight)))
		? result
		: QRect();
}

void ComposeGlass::validateFrame(QRect capsule) {
	const auto source = ResolveGlassSource(this);
	if (!source) {
		_frame = QImage();
		return;
	}
	if (!_frame.isNull()
		&& (_frameKey == source->key)
		&& (_frameShift == source->shift)
		&& (_frameFill == source->fill)
		&& (_frameCapsule == capsule)
		&& (_frameSize == size())) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		PaintGlassWallpaper(q, *source, QRect(QPoint(), size()));

		auto hq = PainterHighQualityEnabler(q);
		const auto radius = ComposeRadius(capsule);
		const auto dark = ComposeDark();
		const auto shape = QRectF(capsule);
		q.setPen(Qt::NoPen);
		for (auto i = 0; i != 3; ++i) {
			const auto grow = style::ConvertScaleExact(1. + i);
			q.setBrush(QColor(0, 0, 0, dark ? (28 - i * 8) : (18 - i * 5)));
			q.drawRoundedRect(
				shape.adjusted(-grow, -grow / 2., grow, grow * 1.5),
				radius + grow,
				radius + grow);
		}
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		q.setBrush(QColor(0, 0, 0));
		q.drawRoundedRect(shape, radius, radius);
	}
	_frame = std::move(frame);
	_frameKey = source->key;
	_frameShift = source->shift;
	_frameFill = source->fill;
	_frameCapsule = capsule;
	_frameSize = size();
}

void ComposeGlass::paintRim(QPainter &p, QRect capsule) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto line = style::ConvertScaleExact(1.);
	const auto half = line / 2.;
	const auto radius = std::max(ComposeRadius(capsule) - half, 0.);
	const auto shape = QRectF(capsule).marginsRemoved(
		QMarginsF(half, half, half, half));
	const auto dark = ComposeDark();
	auto gradient = QLinearGradient(shape.topLeft(), shape.bottomLeft());
	gradient.setColorAt(
		0.,
		dark ? QColor(255, 255, 255, 46) : QColor(255, 255, 255, 170));
	gradient.setColorAt(
		1.,
		dark ? QColor(255, 255, 255, 12) : QColor(0, 0, 0, 28));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(gradient), line));
	p.drawRoundedRect(shape, radius, radius);
}

void ComposeGlass::paintEvent(QPaintEvent *) {
	if (!shouldPaint()) {
		return;
	}
	const auto capsule = capsuleRect();
	if (capsule.isEmpty()) {
		return;
	}
	validateFrame(capsule);
	if (_frame.isNull()) {
		return;
	}
	auto p = QPainter(this);
	p.drawImage(0, 0, _frame);
	paintRim(p, capsule);
}

} // namespace

void SetupSnowfall(not_null<Ui::RpWidget*> parent) {
	Ui::CreateChild<SnowOverlay>(
		parent.get(),
		EnabledValue(Mod::Snowfall),
		parent->sizeValue() | rpl::map([](QSize size) {
			return QRect(QPoint(), size);
		}));
}

void SetupChatSnowfall(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QRect> geometry) {
	InstallGlassBackdrop();
	EnabledValue(
		Mod::GlassUi
	) | rpl::on_next([](bool enabled) {
		SetGlassUiEnabled(enabled);
	}, parent->lifetime());

	const auto scroll = parent->lifetime().make_state<rpl::variable<QRect>>(
		std::move(geometry));
	Ui::CreateChild<SnowOverlay>(
		parent.get(),
		rpl::combine(
			EnabledValue(Mod::SnowfallChats),
			EnabledValue(Mod::Snowfall)
		) | rpl::map([](bool chats, bool window) {
			return chats && !window;
		}),
		scroll->value());
	Ui::CreateChild<ComposeGlass>(parent.get(), scroll->value());
}

} // namespace Telewhite
