/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_snowfall.h"

#include "base/random.h"
#include "base/timer.h"
#include "telewhite/telewhite_glass_backdrop.h"
#include "telewhite/telewhite_glass_scroll.h"
#include "telewhite/telewhite_mods.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_basic.h"
#include "styles/palette.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QtEvents>

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

// The wallpaper around the input capsule with its soft shadow, the capsule
// itself is cut out: the controls under it are seen through.
struct ComposeFrame {
	QImage image;
	QRect capsule;
	QSize size;
	QSize fill;
	QPoint shift;
	quint64 key = 0;
};

void ValidateComposeFrame(
		ComposeFrame &frame,
		not_null<QWidget*> widget,
		QRect capsule) {
	const auto source = ResolveGlassSource(widget);
	if (!source) {
		frame.image = QImage();
		return;
	}
	const auto outer = widget->size();
	if (!frame.image.isNull()
		&& (frame.key == source->key)
		&& (frame.shift == source->shift)
		&& (frame.fill == source->fill)
		&& (frame.capsule == capsule)
		&& (frame.size == outer)) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		PaintGlassWallpaper(q, *source, QRect(QPoint(), outer));

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
	frame.image = std::move(image);
	frame.key = source->key;
	frame.shift = source->shift;
	frame.fill = source->fill;
	frame.capsule = capsule;
	frame.size = outer;
}

void PaintComposeRim(QPainter &p, QRect capsule) {
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

	const not_null<Ui::RpWidget*> _owner;
	QPointer<Ui::InputField> _field;
	QRect _scroll;
	ComposeFrame _frame;

};

// The compose controls of topics, replies and scheduled messages paint
// their own background, so the wallpaper around the capsule is painted
// over them: the overlay stays above the controls of the input area.
class ComposeWrapGlass final : public Ui::RpWidget {
public:
	explicit ComposeWrapGlass(not_null<Ui::RpWidget*> wrap);

protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void watch(not_null<QObject*> child);
	void ensureField();
	void refresh();
	void raiseLater();
	[[nodiscard]] bool shouldPaint() const;
	[[nodiscard]] QRect capsuleRect() const;

	const not_null<Ui::RpWidget*> _wrap;
	QPointer<Ui::InputField> _field;
	ComposeFrame _frame;
	bool _raiseScheduled = false;

};

// The messages under the glass header: the list is lowered under the bars
// and its covered part is rendered for their frosted backdrop on each move.
class GlassScroll final {
public:
	GlassScroll(
		not_null<Ui::RpWidget*> section,
		not_null<Ui::ElasticScroll*> scroll);
	~GlassScroll();

private:
	void schedule();
	void capture();
	void updateBars();
	void relayout();

	const not_null<Ui::RpWidget*> _section;
	const not_null<Ui::ElasticScroll*> _scroll;
	GlassContent _content;
	int _inset = 0;
	bool _scheduled = false;
	rpl::lifetime _lifetime;

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
		_frame.image = QImage();
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

void ComposeGlass::paintEvent(QPaintEvent *) {
	if (!shouldPaint()) {
		return;
	}
	const auto capsule = capsuleRect();
	if (capsule.isEmpty()) {
		return;
	}
	ValidateComposeFrame(_frame, this, capsule);
	if (_frame.image.isNull()) {
		return;
	}
	auto p = QPainter(this);
	p.drawImage(0, 0, _frame.image);
	PaintComposeRim(p, capsule);
}

ComposeWrapGlass::ComposeWrapGlass(not_null<Ui::RpWidget*> wrap)
: RpWidget(wrap.get())
, _wrap(wrap) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	hide();

	_wrap->installEventFilter(this);
	for (const auto child : _wrap->children()) {
		watch(child);
	}
	ensureField();

	_wrap->sizeValue(
	) | rpl::on_next([=](QSize outer) {
		setGeometry(QRect(QPoint(), outer));
		refresh();
	}, lifetime());

	EnabledValue(
		Mod::GlassUi
	) | rpl::skip(1) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_frame.image = QImage();
		update();
	}, lifetime());
}

void ComposeWrapGlass::watch(not_null<QObject*> child) {
	if (child.get() != this && child->isWidgetType()) {
		child->installEventFilter(this);
	}
}

bool ComposeWrapGlass::eventFilter(QObject *o, QEvent *e) {
	if (o == _wrap.get()) {
		if (e->type() == QEvent::ChildAdded) {
			if (const auto child = static_cast<QChildEvent*>(e)->child()) {
				watch(child);
			}
			raiseLater();
		}
	} else if ((e->type() == QEvent::ZOrderChange)
		|| (e->type() == QEvent::Show)) {
		raiseLater();
	}
	return false;
}

void ComposeWrapGlass::raiseLater() {
	if (_raiseScheduled) {
		return;
	}
	_raiseScheduled = true;
	crl::on_main(this, [=] {
		_raiseScheduled = false;
		if (!isHidden()) {
			raise();
		}
	});
}

void ComposeWrapGlass::ensureField() {
	if (_field) {
		return;
	}
	for (const auto child : _wrap->children()) {
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

bool ComposeWrapGlass::shouldPaint() const {
	const auto input = _field.data();
	return Enabled(Mod::GlassUi) && input && !input->isHidden();
}

QRect ComposeWrapGlass::capsuleRect() const {
	const auto side = style::ConvertScale(kComposeSide);
	const auto top = st::lineWidth + style::ConvertScale(kComposeTop);
	const auto result = QRect(
		side,
		top,
		width() - 2 * side,
		height() - top - style::ConvertScale(kComposeBottom));
	return ((result.width() > 0)
		&& (result.height() >= style::ConvertScale(kComposeMinHeight)))
		? result
		: QRect();
}

void ComposeWrapGlass::refresh() {
	ensureField();
	const auto wanted = shouldPaint()
		&& !capsuleRect().isEmpty()
		&& ResolveGlassSource(this).has_value();
	if (!wanted) {
		if (!isHidden()) {
			hide();
		}
		return;
	}
	if (isHidden()) {
		show();
	}
	raise();
	update();
}

void ComposeWrapGlass::paintEvent(QPaintEvent *) {
	if (!shouldPaint()) {
		return;
	}
	const auto capsule = capsuleRect();
	if (capsule.isEmpty()) {
		return;
	}
	ValidateComposeFrame(_frame, this, capsule);
	if (_frame.image.isNull()) {
		return;
	}
	auto p = QPainter(this);
	p.drawImage(0, 0, _frame.image);
	PaintComposeRim(p, capsule);
}

GlassScroll::GlassScroll(
	not_null<Ui::RpWidget*> section,
	not_null<Ui::ElasticScroll*> scroll)
: _section(section)
, _scroll(scroll) {
	// The bars and the header are over the list now.
	_scroll->lower();
	_section->setProperty(
		kGlassContentProperty,
		QVariant::fromValue(static_cast<void*>(&_content)));

	rpl::merge(
		_scroll->scrolls(),
		_scroll->innerResizes(),
		_scroll->geometryValue() | rpl::to_empty,
		GlassScrollInsetUpdates().events(
		) | rpl::filter([=](not_null<QWidget*> which) {
			return (which.get() == _scroll.get());
		}) | rpl::to_empty,
		style::PaletteChanged()
	) | rpl::on_next([=] {
		schedule();
	}, _lifetime);

	EnabledValue(
		Mod::GlassUi
	) | rpl::skip(1) | rpl::on_next([=](bool enabled) {
		SetGlassUiEnabled(enabled);
		relayout();
	}, _lifetime);
}

GlassScroll::~GlassScroll() {
	_section->setProperty(kGlassContentProperty, QVariant());
}

void GlassScroll::schedule() {
	if (_scheduled) {
		return;
	}
	_scheduled = true;
	crl::on_main(_scroll.get(), [=] {
		capture();
	});
}

void GlassScroll::capture() {
	_scheduled = false;
	_inset = GlassScrollInset(_scroll.get());
	const auto inner = _scroll->widget();
	const auto wanted = GlassUiEnabled()
		&& (_inset > 0)
		&& inner
		&& !inner->isHidden()
		&& !_scroll->isHidden()
		&& _section->isVisible()
		&& (_scroll->width() > 0)
		&& (_scroll->height() > 0);
	if (!wanted) {
		if (!_content.image.isNull()) {
			_content.image = QImage();
			_content.rect = QRect();
			++_content.revision;
			updateBars();
		}
		return;
	}
	const auto margin = style::ConvertScale(kGlassBlurMargin);
	const auto local = QRect(
		0,
		0,
		_scroll->width(),
		std::min(_inset + margin, _scroll->height()));
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		local.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		const auto position = inner->mapTo(_scroll.get(), QPoint());
		inner->render(
			&p,
			QPoint(),
			QRegion(local.translated(-position)),
			QWidget::DrawChildren);
	}
	_content.image = std::move(image);
	_content.rect = QRect(
		_scroll->mapTo(_section.get(), QPoint()),
		local.size());
	++_content.revision;
	updateBars();
}

void GlassScroll::updateBars() {
	const auto zone = QRect(
		_scroll->mapTo(_section.get(), QPoint()),
		QSize(_scroll->width(), std::max(_inset, 1)));
	for (const auto child : _section->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (!widget
			|| (widget == _scroll.get())
			|| widget->isHidden()
			|| !widget->geometry().intersects(zone)) {
			continue;
		}
		widget->update();
	}
}

void GlassScroll::relayout() {
	QResizeEvent event(_section->size(), _section->size());
	QCoreApplication::sendEvent(_section.get(), &event);
	schedule();
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

	// The snow doesn't fall over the glass header and bars.
	auto inset = rpl::single(0) | rpl::then(
		GlassScrollInsetUpdates().events(
		) | rpl::filter([=](not_null<QWidget*> which) {
			return (which->parentWidget() == parent.get());
		}) | rpl::map([](not_null<QWidget*> which) {
			return GlassScrollInset(which.get());
		}));
	Ui::CreateChild<SnowOverlay>(
		parent.get(),
		rpl::combine(
			EnabledValue(Mod::SnowfallChats),
			EnabledValue(Mod::Snowfall)
		) | rpl::map([](bool chats, bool window) {
			return chats && !window;
		}),
		rpl::combine(
			scroll->value(),
			std::move(inset)
		) | rpl::map([](QRect area, int top) {
			return area.marginsRemoved(QMargins(0, top, 0, 0));
		}));
	Ui::CreateChild<ComposeGlass>(parent.get(), scroll->value());
}

void SetupGlassScroll(
		not_null<Ui::RpWidget*> section,
		not_null<Ui::ElasticScroll*> scroll) {
	InstallGlassBackdrop();
	[[maybe_unused]] const auto state
		= section->lifetime().make_state<GlassScroll>(section, scroll);
}

void SetupComposeGlass(not_null<Ui::RpWidget*> wrap) {
	InstallGlassBackdrop();
	Ui::CreateChild<ComposeWrapGlass>(wrap.get());
}

} // namespace Telewhite
