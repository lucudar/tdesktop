/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/painter.h"
#include "ui/style/style_core_scale.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>

namespace Telewhite {

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
