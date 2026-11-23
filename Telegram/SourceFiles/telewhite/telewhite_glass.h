/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/painter.h"
#include "ui/style/style_core_scale.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QImage>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <functional>

class QWidget;

namespace Telewhite {

// Tints of the glass: over the frosted wallpaper and over the messages.
inline constexpr auto kGlassBarTint = 0.72;
inline constexpr auto kGlassCircleTint = 0.9;

// The "Glass interface" mod for the widgets that can't see the mods.
[[nodiscard]] inline bool &GlassUiFlag() {
	static auto result = false;
	return result;
}

[[nodiscard]] inline bool GlassUiEnabled() {
	return GlassUiFlag();
}

inline void SetGlassUiEnabled(bool enabled) {
	GlassUiFlag() = enabled;
	// The menus of lib_ui can't see us, they read the application property,
	// see the "glass-menus" feature in Telegram/build/telewhite_patch.py.
	if (const auto app = QCoreApplication::instance()) {
		app->setProperty("telewhiteGlassMenus", enabled);
	}
}

struct GlassBackdropCache {
	QImage image;
	QRect slice;
	QSize fill;
	quint64 key = 0;
};

using GlassBackdropPainter = std::function<bool(
	QPainter&,
	not_null<QWidget*>,
	QRect,
	GlassBackdropCache&)>;

// Installed by the chat sections: paints the frosted wallpaper
// that lies under the given rect of the widget.
[[nodiscard]] inline GlassBackdropPainter &GlassBackdropHook() {
	static auto result = GlassBackdropPainter();
	return result;
}

inline bool PaintGlassBackdrop(
		QPainter &p,
		not_null<QWidget*> widget,
		QRect rect,
		GlassBackdropCache &cache) {
	const auto &hook = GlassBackdropHook();
	return hook && hook(p, widget, rect, cache);
}

// A bar of the floating layer (the chat header, the pinned message):
// the frosted wallpaper, a tint of the bar color and a soft top shine.
inline bool PaintGlassBar(
		QPainter &p,
		not_null<QWidget*> widget,
		QRect rect,
		GlassBackdropCache &cache,
		QColor tint,
		bool shine) {
	if (rect.isEmpty() || !PaintGlassBackdrop(p, widget, rect, cache)) {
		return false;
	}
	tint.setAlphaF(float(tint.alphaF() * kGlassBarTint));
	p.fillRect(rect, tint);
	if (shine) {
		auto gradient = QLinearGradient(
			QPointF(rect.topLeft()),
			QPointF(rect.bottomLeft()));
		gradient.setColorAt(0., QColor(255, 255, 255, 28));
		gradient.setColorAt(0.6, QColor(255, 255, 255, 0));
		gradient.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(rect, QBrush(gradient));
	}
	return true;
}

// A round floating button (scroll down, mentions, reactions):
// a soft shadow ring, a tinted body, a shine on the top and a rim.
inline void PaintGlassCircle(QPainter &p, QRect circle, QColor bg) {
	if (circle.isEmpty()) {
		return;
	}
	const auto shape = QRectF(circle);
	const auto line = style::ConvertScaleExact(1.);
	const auto half = line / 2.;
	const auto dark = (bg.lightnessF() < 0.5);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);

	const auto center = shape.center() + QPointF(0., line * 1.5);
	const auto inner = shape.width() / 2.;
	const auto outer = inner + line * 3.;
	const auto shadowColor = QColor(0, 0, 0, dark ? 72 : 44);
	auto shadow = QRadialGradient(center, outer);
	shadow.setColorAt(0., shadowColor);
	shadow.setColorAt(std::clamp(inner / outer, 0., 1.), shadowColor);
	shadow.setColorAt(1., QColor(0, 0, 0, 0));
	auto ring = QPainterPath();
	ring.setFillRule(Qt::OddEvenFill);
	ring.addEllipse(center, outer, outer);
	ring.addEllipse(shape);
	p.fillPath(ring, QBrush(shadow));

	auto body = bg;
	body.setAlphaF(float(bg.alphaF() * kGlassCircleTint));
	p.setBrush(body);
	p.drawEllipse(shape);

	auto shine = QLinearGradient(shape.topLeft(), shape.bottomLeft());
	shine.setColorAt(0., QColor(255, 255, 255, dark ? 44 : 110));
	shine.setColorAt(0.5, QColor(255, 255, 255, 0));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.setBrush(QBrush(shine));
	p.drawEllipse(shape);

	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		dark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 24),
		line));
	p.drawEllipse(shape.marginsRemoved(QMarginsF(half, half, half, half)));
}

// A chip of the "Glass interface": tinted fill, a soft highlight
// on the top and a thin light rim. Short chips become capsules.
inline void PaintGlassBubble(
		QPainter &p,
		QRect rect,
		QColor bg,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	const auto shape = QRectF(rect);
	const auto r = float64(std::min(radius, rect.height() / 2));
	const auto line = style::ConvertScaleExact(1.);
	const auto half = line / 2.;

	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	path.addRoundedRect(shape, r, r);
	p.fillPath(path, bg);

	auto shine = QLinearGradient(shape.topLeft(), shape.bottomLeft());
	shine.setColorAt(0., QColor(255, 255, 255, 36));
	shine.setColorAt(0.55, QColor(255, 255, 255, 0));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillPath(path, shine);

	const auto inner = std::max(r - half, 0.);
	p.setPen(QPen(QColor(255, 255, 255, 51), line));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		shape.marginsRemoved(QMarginsF(half, half, half, half)),
		inner,
		inner);
}

} // namespace Telewhite
