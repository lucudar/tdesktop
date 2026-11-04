/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_mods_preview.h"

#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QSizeF>
#include <QtCore/QStringList>
#include <QtGui/QFontMetricsF>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>

namespace Telewhite::Preview {
namespace {

constexpr auto kLeft = int(Qt::AlignLeft);
constexpr auto kCenter = int(Qt::AlignHCenter);
constexpr auto kPi = 3.14159265358979323846;

constexpr auto kCardHeight = 182.;
constexpr auto kPanelsTop = 64.;
constexpr auto kPanelHeight = 106.;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double ratio) {
	const auto r = std::clamp(ratio, 0., 1.);
	const auto mix = [&](int x, int y) {
		return int(std::lround(x + (y - x) * r));
	};
	return QColor(
		mix(a.red(), b.red()),
		mix(a.green(), b.green()),
		mix(a.blue(), b.blue()),
		mix(a.alpha(), b.alpha()));
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	const auto value = std::clamp(double(color.alphaF()) * alpha, 0., 1.);
	color.setAlphaF(float(value));
	return color;
}

[[nodiscard]] bool IsDark(const QColor &color) {
	return color.lightnessF() < 0.5;
}

struct Colors {
	QColor bg;
	QColor chat;
	QColor fg;
	QColor sub;
	QColor accent;
	QColor accentFg;
	QColor inBg;
	QColor inFg;
	QColor inSub;
	QColor outBg;
	QColor outFg;
	QColor outSub;
	QColor attention;
	QColor online;
};

[[nodiscard]] QColor ChatBackground(const QColor &bg, const QColor &accent) {
	return IsDark(bg)
		? Mix(Mix(bg, QColor(0, 0, 0), 0.4), accent, 0.1)
		: Mix(bg, accent, 0.13);
}

[[nodiscard]] Colors ColorsFromPalette(const Palette &palette) {
	return Colors{
		.bg = palette.bg,
		.chat = ChatBackground(palette.bg, palette.accent),
		.fg = palette.fg,
		.sub = palette.sub,
		.accent = palette.accent,
		.accentFg = palette.accentFg,
		.inBg = palette.inBg,
		.inFg = palette.inFg,
		.inSub = palette.inSub,
		.outBg = palette.outBg,
		.outFg = palette.outFg,
		.outSub = palette.outSub,
		.attention = palette.attention,
		.online = palette.online,
	};
}

[[nodiscard]] Colors ThemeColors(Scene scene, const Colors &current) {
	auto result = current;
	switch (scene) {
	case Scene::ThemeLight:
		result.bg = QColor(0xff, 0xff, 0xff);
		result.fg = QColor(0x00, 0x00, 0x00);
		result.sub = QColor(0x99, 0x99, 0x99);
		result.accent = QColor(0x6f, 0x4c, 0xd7);
		result.inBg = QColor(0xff, 0xff, 0xff);
		result.inFg = QColor(0x00, 0x00, 0x00);
		result.inSub = QColor(0xa0, 0xac, 0xb6);
		result.outBg = QColor(0xe6, 0xde, 0xfd);
		result.outFg = QColor(0x00, 0x00, 0x00);
		result.outSub = QColor(0x7a, 0x66, 0xb5);
		break;
	case Scene::ThemeDark:
		result.bg = QColor(0x1d, 0x1a, 0x2b);
		result.fg = QColor(0xf5, 0xf5, 0xf5);
		result.sub = QColor(0x8f, 0x89, 0xa8);
		result.accent = QColor(0x9b, 0x80, 0xff);
		result.inBg = QColor(0x26, 0x21, 0x3a);
		result.inFg = QColor(0xf5, 0xf5, 0xf5);
		result.inSub = QColor(0x8f, 0x89, 0xa8);
		result.outBg = QColor(0x4e, 0x3d, 0x8f);
		result.outFg = QColor(0xf5, 0xf5, 0xf5);
		result.outSub = QColor(0xb9, 0xa8, 0xff);
		break;
	case Scene::ThemeAmoled:
		result.bg = QColor(0x00, 0x00, 0x00);
		result.fg = QColor(0xf5, 0xf5, 0xf5);
		result.sub = QColor(0x70, 0x84, 0x99);
		result.accent = QColor(0x52, 0x88, 0xc1);
		result.inBg = QColor(0x1c, 0x1d, 0x1f);
		result.inFg = QColor(0xf5, 0xf5, 0xf5);
		result.inSub = QColor(0x6d, 0x7f, 0x8f);
		result.outBg = QColor(0x2b, 0x52, 0x78);
		result.outFg = QColor(0xe4, 0xec, 0xf2);
		result.outSub = QColor(0x7d, 0xa8, 0xd3);
		break;
	default:
		return result;
	}
	result.chat = (scene == Scene::ThemeAmoled)
		? QColor(0x00, 0x00, 0x00)
		: ChatBackground(result.bg, result.accent);
	return result;
}

struct Canvas {
	QPainter &p;
	Colors c;
	QFont base;
	double s = 1.;
};

[[nodiscard]] QFont FontOf(const Canvas &k, double size, bool bold = false) {
	auto result = k.base;
	result.setPixelSize(std::max(1, int(std::lround(size * k.s))));
	result.setBold(bold);
	return result;
}

[[nodiscard]] double TextWidth(const QFont &font, const QString &text) {
	return QFontMetricsF(font).horizontalAdvance(text);
}

void DrawText(
		const Canvas &k,
		const QRectF &box,
		const QString &text,
		const QFont &font,
		const QColor &color,
		int align = kLeft) {
	if (box.width() <= 0.) {
		return;
	}
	const auto elided = QFontMetricsF(font).elidedText(
		text,
		Qt::ElideRight,
		box.width());
	k.p.setFont(font);
	k.p.setPen(color);
	k.p.drawText(
		box,
		align | int(Qt::AlignVCenter) | int(Qt::TextSingleLine),
		elided);
}

[[nodiscard]] QStringList Wrap(
		const QString &text,
		const QFont &font,
		double width,
		int maxLines) {
	const auto metrics = QFontMetricsF(font);
	const auto words = text.split(QChar(' '), Qt::SkipEmptyParts);
	auto result = QStringList();
	auto current = QString();
	for (const auto &word : words) {
		const auto candidate = current.isEmpty()
			? word
			: (current + QChar(' ') + word);
		if (current.isEmpty() || metrics.horizontalAdvance(candidate) <= width) {
			current = candidate;
		} else {
			result.push_back(current);
			current = word;
		}
	}
	if (!current.isEmpty()) {
		result.push_back(current);
	}
	if (result.size() > maxLines) {
		auto last = result[maxLines - 1];
		for (auto i = maxLines; i < result.size(); ++i) {
			last += QChar(' ') + result[i];
		}
		result = result.mid(0, maxLines);
		result[maxLines - 1] = metrics.elidedText(
			last,
			Qt::ElideRight,
			width);
	}
	return result;
}

[[nodiscard]] QPen PenOf(const QColor &color, double width) {
	return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void FillRound(
		const Canvas &k,
		const QRectF &rect,
		double radius,
		const QColor &color) {
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(color);
	k.p.drawRoundedRect(rect, radius, radius);
}

void FillCircle(
		const Canvas &k,
		const QPointF &center,
		double radius,
		const QColor &color) {
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(color);
	k.p.drawEllipse(center, radius, radius);
}

void StrokeLine(
		const Canvas &k,
		const QPointF &a,
		const QPointF &b,
		const QColor &color,
		double width) {
	k.p.setPen(PenOf(color, width));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawLine(a, b);
}

void Caption(const Canvas &k, const QRectF &r, double top, const QString &text) {
	DrawText(
		k,
		QRectF(r.x(), top, r.width(), 12. * k.s),
		text,
		FontOf(k, 9.),
		k.c.sub);
}

void Checks(
		const Canvas &k,
		double right,
		double centerY,
		int count,
		const QColor &color) {
	if (count <= 0) {
		return;
	}
	const auto u = k.s;
	const auto w = 6.6 * u;
	const auto shift = 3.6 * u;
	const auto first = right - w - ((count > 1) ? shift : 0.);
	auto path = QPainterPath();
	path.moveTo(first, centerY + 0.2 * u);
	path.lineTo(first + 2.3 * u, centerY + 2.5 * u);
	path.lineTo(first + w, centerY - 2.6 * u);
	if (count > 1) {
		path.moveTo(first + shift + 2.3 * u, centerY + 2.5 * u);
		path.lineTo(first + shift + w, centerY - 2.6 * u);
	}
	k.p.setPen(PenOf(color, 1.2 * u));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawPath(path);
}

void Star(const Canvas &k, const QPointF &center, double radius) {
	auto polygon = QPolygonF();
	for (auto i = 0; i != 10; ++i) {
		const auto angle = (-90. + i * 36.) * kPi / 180.;
		const auto r = (i % 2) ? (radius * 0.45) : radius;
		polygon << QPointF(
			center.x() + r * std::cos(angle),
			center.y() + r * std::sin(angle));
	}
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(QColor(0xf5, 0xb5, 0x2e));
	k.p.drawPolygon(polygon);
}

void Pin(const Canvas &k, const QPointF &center, const QColor &color) {
	const auto u = k.s;
	StrokeLine(
		k,
		QPointF(center.x() - 3. * u, center.y() + 3.5 * u),
		QPointF(center.x() + 0.5 * u, center.y()),
		color,
		1.2 * u);
	k.p.save();
	k.p.translate(center.x() + 1.5 * u, center.y() - 1.5 * u);
	k.p.rotate(45.);
	FillRound(k, QRectF(-2.2 * u, -3.4 * u, 4.4 * u, 6.8 * u), 1.4 * u, color);
	k.p.restore();
}

void Eye(const Canvas &k, const QPointF &center, double width, const QColor &color) {
	const auto u = k.s;
	auto path = QPainterPath();
	path.moveTo(center.x() - width / 2., center.y());
	path.quadTo(center.x(), center.y() - width * 0.55, center.x() + width / 2., center.y());
	path.quadTo(center.x(), center.y() + width * 0.55, center.x() - width / 2., center.y());
	k.p.setPen(PenOf(color, 1.1 * u));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawPath(path);
	FillCircle(k, center, width * 0.16, color);
}

void Avatar(
		const Canvas &k,
		const QPointF &center,
		double radius,
		const QColor &color,
		const QString &letter,
		bool online) {
	auto gradient = QLinearGradient(
		center - QPointF(0., radius),
		center + QPointF(0., radius));
	gradient.setColorAt(0., color.lighter(118));
	gradient.setColorAt(1., color);
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(gradient);
	k.p.drawEllipse(center, radius, radius);
	DrawText(
		k,
		QRectF(center.x() - radius, center.y() - radius, radius * 2., radius * 2.),
		letter,
		FontOf(k, radius / k.s * 0.95, true),
		QColor(0xff, 0xff, 0xff),
		kCenter);
	if (online) {
		const auto dot = center + QPointF(radius * 0.72, radius * 0.72);
		FillCircle(k, dot, radius * 0.3 + 1.3 * k.s, k.c.chat);
		FillCircle(k, dot, radius * 0.3, k.c.online);
	}
}

struct BubbleSpec {
	QString text;
	QString time;
	QString header;
	bool out = false;
	int checks = 0;
	bool edited = false;
	bool deleted = false;
};

[[nodiscard]] double BubbleHeight(const Canvas &k, const BubbleSpec &spec) {
	return (spec.header.isEmpty() ? 22.5 : 35.) * k.s;
}

QRectF Bubble(
		const Canvas &k,
		const QRectF &area,
		double top,
		const BubbleSpec &spec) {
	const auto u = k.s;
	const auto textFont = FontOf(k, 10.5);
	const auto timeFont = FontOf(k, 8.5);
	const auto headerFont = FontOf(k, 9.5, true);
	const auto padX = 7. * u;
	const auto padTop = 4.5 * u;
	const auto lineH = 13.5 * u;
	const auto headerH = spec.header.isEmpty() ? 0. : (12.5 * u);
	const auto time = spec.edited ? (U("изм. ") + spec.time) : spec.time;
	const auto timeTextW = TextWidth(timeFont, time);
	const auto checksW = (spec.checks > 1)
		? (14. * u)
		: spec.checks
		? (10.5 * u)
		: 0.;
	const auto trashW = spec.deleted ? (9.5 * u) : 0.;
	const auto timeW = timeTextW + checksW + trashW;
	const auto textW = TextWidth(textFont, spec.text);
	const auto headerW = spec.header.isEmpty()
		? 0.
		: TextWidth(headerFont, spec.header);
	const auto maxInner = area.width() * 0.92 - 2. * padX;
	const auto innerW = std::min(
		std::max(textW + 6. * u + timeW, headerW),
		maxInner);
	const auto width = innerW + 2. * padX;
	const auto height = BubbleHeight(k, spec);
	const auto left = spec.out ? (area.x() + area.width() - width) : area.x();
	const auto rect = QRectF(left, top, width, height);

	const auto dim = [&](const QColor &color) {
		return spec.deleted
			? Alpha(Mix(color, QColor(0, 0, 0), 0.22), 0.7)
			: color;
	};
	const auto bg = spec.out ? k.c.outBg : k.c.inBg;
	const auto fg = spec.out ? k.c.outFg : k.c.inFg;
	const auto sub = spec.out ? k.c.outSub : k.c.inSub;
	FillRound(k, rect, 8. * u, dim(bg));

	auto y = top + padTop;
	if (headerH > 0.) {
		DrawText(
			k,
			QRectF(left + padX, y, innerW, headerH),
			spec.header,
			headerFont,
			dim(spec.out ? k.c.outSub : k.c.accent));
		y += headerH;
	}
	DrawText(
		k,
		QRectF(left + padX, y, innerW - timeW - 6. * u, lineH),
		spec.text,
		textFont,
		dim(fg));

	const auto timeTop = y + 2. * u;
	const auto timeCenter = timeTop + lineH / 2.;
	auto right = left + width - padX;
	if (spec.checks) {
		Checks(k, right, timeCenter, spec.checks, dim(sub));
		right -= checksW;
	}
	DrawText(
		k,
		QRectF(right - timeTextW, timeTop, timeTextW + 1. * u, lineH),
		time,
		timeFont,
		dim(sub));
	right -= timeTextW;
	if (spec.deleted) {
		const auto size = QSizeF(6.6 * u, 8. * u);
		PaintTrashIcon(
			k.p,
			QRectF(
				right - 2.6 * u - size.width(),
				timeCenter - size.height() / 2.,
				size.width(),
				size.height()),
			dim(sub),
			1. * u);
	}
	return rect;
}

struct RowSpec {
	QString letter;
	QColor avatar;
	QString name;
	QString status;
	QColor statusColor;
	bool online = false;
	bool star = false;
	bool pin = false;
	QString tag;
	int checkbox = 0;
};

double Row(
		const Canvas &k,
		const QRectF &r,
		double top,
		double radius,
		const RowSpec &row) {
	const auto u = k.s;
	const auto avatarRadius = radius * u;
	const auto height = std::max(2. * avatarRadius, 14. * u);
	const auto center = QPointF(r.x() + avatarRadius, top + height / 2.);
	Avatar(k, center, avatarRadius, row.avatar, row.letter, row.online);

	auto right = r.x() + r.width();
	if (row.checkbox) {
		const auto box = QPointF(right - 6. * u, center.y());
		if (row.checkbox == 2) {
			FillCircle(k, box, 6. * u, k.c.accent);
			auto path = QPainterPath();
			path.moveTo(box.x() - 2.6 * u, box.y() + 0.2 * u);
			path.lineTo(box.x() - 0.6 * u, box.y() + 2.2 * u);
			path.lineTo(box.x() + 2.8 * u, box.y() - 2. * u);
			k.p.setPen(PenOf(k.c.accentFg, 1.3 * u));
			k.p.setBrush(Qt::NoBrush);
			k.p.drawPath(path);
		} else {
			k.p.setPen(PenOf(Alpha(k.c.sub, 0.8), 1.2 * u));
			k.p.setBrush(Qt::NoBrush);
			k.p.drawEllipse(box, 5.4 * u, 5.4 * u);
		}
		right -= 16. * u;
	}
	if (!row.tag.isEmpty()) {
		const auto font = FontOf(k, 8., true);
		const auto w = TextWidth(font, row.tag) + 8. * u;
		const auto pill = QRectF(right - w, center.y() - 6.5 * u, w, 13. * u);
		FillRound(k, pill, 6.5 * u, Alpha(k.c.accent, 0.18));
		DrawText(k, pill, row.tag, font, k.c.accent, kCenter);
		right -= w + 4. * u;
	}
	if (row.pin) {
		Pin(k, QPointF(right - 5. * u, center.y()), k.c.sub);
		right -= 13. * u;
	}

	const auto x = r.x() + 2. * avatarRadius + 6. * u;
	const auto width = std::max(0., right - x);
	const auto nameFont = FontOf(k, 10.5, true);
	const auto nameTop = row.status.isEmpty()
		? (center.y() - 7. * u)
		: (center.y() - 12. * u);
	const auto starSpace = row.star ? (12. * u) : 0.;
	const auto nameW = std::min(
		TextWidth(nameFont, row.name) + 1. * u,
		std::max(0., width - starSpace));
	DrawText(k, QRectF(x, nameTop, nameW, 13. * u), row.name, nameFont, k.c.fg);
	if (row.star) {
		Star(k, QPointF(x + nameW + 6. * u, nameTop + 6.5 * u), 4.6 * u);
	}
	if (!row.status.isEmpty()) {
		DrawText(
			k,
			QRectF(x, center.y() + 0.5 * u, width, 12. * u),
			row.status,
			FontOf(k, 9.5),
			row.statusColor);
	}
	return height;
}

void Face(const Canvas &k, const QPointF &center, double radius) {
	FillCircle(k, center, radius, QColor(0xff, 0xc8, 0x3d));
	const auto eye = radius * 0.13;
	const auto dark = QColor(0x5a, 0x3b, 0x12);
	FillCircle(k, center + QPointF(-radius * 0.35, -radius * 0.2), eye, dark);
	FillCircle(k, center + QPointF(radius * 0.35, -radius * 0.2), eye, dark);
	auto smile = QPainterPath();
	smile.moveTo(center.x() - radius * 0.45, center.y() + radius * 0.18);
	smile.quadTo(
		center.x(),
		center.y() + radius * 0.75,
		center.x() + radius * 0.45,
		center.y() + radius * 0.18);
	k.p.setPen(PenOf(dark, std::max(1., radius * 0.12)));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawPath(smile);
}

void GifTile(const Canvas &k, const QRectF &rect) {
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., k.c.accent);
	gradient.setColorAt(1., QColor(0xff, 0x8a, 0xac));
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(gradient);
	k.p.drawRoundedRect(rect, 6. * k.s, 6. * k.s);
	DrawText(
		k,
		rect,
		U("GIF"),
		FontOf(k, std::max(8., rect.height() / k.s * 0.38), true),
		QColor(0xff, 0xff, 0xff),
		kCenter);
}

void Waveform(
		const Canvas &k,
		const QRectF &rect,
		const QColor &color,
		int bars) {
	const auto u = k.s;
	const auto step = rect.width() / std::max(bars, 1);
	for (auto i = 0; i != bars; ++i) {
		const auto value = 0.25 + 0.75 * std::fabs(std::sin(i * 1.7 + 0.6));
		const auto h = std::max(2. * u, rect.height() * value);
		const auto x = rect.x() + i * step + step / 2.;
		StrokeLine(
			k,
			QPointF(x, rect.center().y() - h / 2.),
			QPointF(x, rect.center().y() + h / 2.),
			color,
			std::max(1., step * 0.55));
	}
}

void PlayButton(
		const Canvas &k,
		const QPointF &center,
		double radius,
		const QColor &bg,
		const QColor &fg) {
	FillCircle(k, center, radius, bg);
	auto triangle = QPolygonF();
	triangle << QPointF(center.x() - radius * 0.3, center.y() - radius * 0.42)
		<< QPointF(center.x() + radius * 0.48, center.y())
		<< QPointF(center.x() - radius * 0.3, center.y() + radius * 0.42);
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(fg);
	k.p.drawPolygon(triangle);
}

void Spoiler(const Canvas &k, const QRectF &rect, const QColor &color) {
	const auto u = k.s;
	const auto step = 2.6 * u;
	auto seed = 7u;
	for (auto y = rect.y() + step / 2.; y < rect.y() + rect.height(); y += step) {
		for (auto x = rect.x() + step / 2.; x < rect.x() + rect.width(); x += step) {
			seed = seed * 1103515245u + 12345u;
			const auto jitterX = (int((seed >> 16) % 100) - 50) / 100. * step;
			const auto jitterY = (int((seed >> 8) % 100) - 50) / 100. * step;
			const auto alpha = 0.35 + ((seed >> 4) % 60) / 100.;
			FillCircle(
				k,
				QPointF(x + jitterX * 0.6, y + jitterY * 0.6),
				0.55 * u,
				Alpha(color, alpha));
		}
	}
}

void MiniWindow(const Canvas &k, const QRectF &r) {
	const auto u = k.s;
	FillRound(k, r, 8. * u, k.c.chat);
	const auto inner = r.adjusted(6. * u, 6. * u, -6. * u, -6. * u);
	Bubble(k, inner, inner.y(), {
		.text = U("Привет!"),
		.time = U("12:30"),
	});
	Bubble(k, inner, inner.y() + 27. * u, {
		.text = U("Как тема?"),
		.time = U("12:31"),
		.out = true,
		.checks = 2,
	});
}

void SceneGhostAll(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Так вас видит собеседник"));
	Row(k, r, r.y() + 15. * u, 10., {
		.letter = U("В"),
		.avatar = k.c.accent,
		.name = U("Вы"),
		.status = on ? U("был(а) недавно") : U("печатает…"),
		.statusColor = on ? k.c.sub : k.c.accent,
		.online = !on,
	});
	const auto spec = BubbleSpec{
		.text = U("Ты тут?"),
		.time = U("12:30"),
		.out = true,
		.checks = on ? 1 : 2,
	};
	Bubble(k, r, r.y() + r.height() - BubbleHeight(k, spec), spec);
}

void SceneGhostRead(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Чат у собеседника"));
	const auto bubble = Bubble(k, r, r.y() + 16. * u, {
		.text = U("Ты тут?"),
		.time = U("12:30"),
		.out = true,
		.checks = on ? 1 : 2,
	});
	const auto font = FontOf(k, 9.5, true);
	const auto label = on ? U("не прочитано") : U("прочитано");
	const auto color = on ? k.c.sub : k.c.accent;
	const auto top = bubble.y() + bubble.height() + 7. * u;
	const auto width = TextWidth(font, label);
	const auto right = r.x() + r.width();
	DrawText(
		k,
		QRectF(right - width - 1. * u, top, width + 1. * u, 13. * u),
		label,
		font,
		color);
	Checks(
		k,
		right - width - 4. * u,
		top + 6.5 * u,
		on ? 1 : 2,
		color);
}

void SceneGhostTyping(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Шапка чата у собеседника"));
	Row(k, r, r.y() + 15. * u, 10., {
		.letter = U("В"),
		.avatar = k.c.accent,
		.name = U("Вы"),
		.status = on ? U("в сети") : U("печатает…"),
		.statusColor = on ? k.c.sub : k.c.accent,
	});
	const auto field = QRectF(
		r.x(),
		r.y() + r.height() - 20. * u,
		r.width(),
		20. * u);
	FillRound(k, field, 10. * u, k.c.inBg);
	const auto font = FontOf(k, 10.);
	const auto text = U("Пишу ответ");
	DrawText(k, field.adjusted(9. * u, 0., -9. * u, 0.), text, font, k.c.inFg);
	const auto caret = field.x() + 9. * u + TextWidth(font, text) + 2. * u;
	StrokeLine(
		k,
		QPointF(caret, field.y() + 5. * u),
		QPointF(caret, field.y() + field.height() - 5. * u),
		k.c.accent,
		1.2 * u);
}

void SceneGhostOnline(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Ваш статус у других"));
	Row(k, r, r.y() + 20. * u, 14., {
		.letter = U("В"),
		.avatar = k.c.accent,
		.name = U("Вы"),
		.status = on ? U("был(а) недавно") : U("в сети"),
		.statusColor = on ? k.c.sub : k.c.accent,
		.online = !on,
	});
}

void SceneGhostStories(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto thumb = QRectF(r.x(), r.y(), 38. * u, r.height());
	auto gradient = QLinearGradient(thumb.topLeft(), thumb.bottomRight());
	gradient.setColorAt(0., k.c.accent);
	gradient.setColorAt(1., QColor(0xff, 0x8a, 0xac));
	k.p.setPen(Qt::NoPen);
	k.p.setBrush(gradient);
	k.p.drawRoundedRect(thumb, 7. * u, 7. * u);
	FillCircle(
		k,
		QPointF(thumb.x() + thumb.width() * 0.68, thumb.y() + 14. * u),
		5. * u,
		QColor(0xff, 0xff, 0xff, 200));

	const auto x = thumb.x() + thumb.width() + 9. * u;
	const auto column = QRectF(x, r.y(), r.x() + r.width() - x, r.height());
	Caption(k, column, r.y(), U("Кто смотрел"));
	const auto eyeCenter = QPointF(x + 6. * u, r.y() + 22. * u);
	Eye(k, eyeCenter, 11. * u, k.c.fg);
	DrawText(
		k,
		QRectF(x + 15. * u, eyeCenter.y() - 7. * u, column.width(), 14. * u),
		on ? U("0") : U("1"),
		FontOf(k, 10.5, true),
		k.c.fg);
	if (on) {
		DrawText(
			k,
			QRectF(x, r.y() + 36. * u, column.width(), 13. * u),
			U("вас нет в списке"),
			FontOf(k, 9.5),
			k.c.sub);
	} else {
		Row(k, column, r.y() + 34. * u, 8., {
			.letter = U("В"),
			.avatar = k.c.accent,
			.name = U("Вы"),
		});
	}
}

void SceneStreamer(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto screen = QRectF(
		r.x() + 12. * u,
		r.y() + 1. * u,
		r.width() - 24. * u,
		r.height() - 12. * u);
	k.p.setPen(PenOf(k.c.sub, 1.3 * u));
	k.p.setBrush(k.c.bg);
	k.p.drawRoundedRect(screen, 4. * u, 4. * u);
	const auto bottom = screen.y() + screen.height();
	const auto middle = screen.center().x();
	StrokeLine(
		k,
		QPointF(middle, bottom),
		QPointF(middle, bottom + 5. * u),
		k.c.sub,
		1.3 * u);
	StrokeLine(
		k,
		QPointF(middle - 10. * u, bottom + 6. * u),
		QPointF(middle + 10. * u, bottom + 6. * u),
		k.c.sub,
		1.3 * u);
	FillCircle(
		k,
		QPointF(screen.x() + 8. * u, screen.y() + 8. * u),
		2.6 * u,
		k.c.attention);
	DrawText(
		k,
		QRectF(screen.x() + 13. * u, screen.y() + 2. * u, 40. * u, 12. * u),
		U("Запись"),
		FontOf(k, 8., true),
		k.c.attention);
	if (on) {
		DrawText(
			k,
			screen.adjusted(4. * u, 12. * u, -4. * u, -2. * u),
			U("окон не видно"),
			FontOf(k, 9.),
			k.c.sub,
			kCenter);
	} else {
		const auto window = screen.adjusted(16. * u, 15. * u, -16. * u, -6. * u);
		FillRound(k, window, 3. * u, k.c.inBg);
		FillRound(
			k,
			QRectF(window.x(), window.y(), window.width(), 6. * u),
			3. * u,
			Alpha(k.c.accent, 0.85));
		for (auto i = 0; i != 3; ++i) {
			const auto y = window.y() + (10. + i * 6.) * u;
			if (y + 3. * u > window.y() + window.height()) {
				break;
			}
			FillRound(
				k,
				QRectF(
					window.x() + 4. * u,
					y,
					(window.width() - 8. * u) * (i == 1 ? 0.6 : 0.85),
					3. * u),
				1.5 * u,
				Alpha(k.c.sub, 0.6));
		}
	}
}

void SceneSaveDeleted(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto first = Bubble(k, r, r.y(), {
		.text = U("Привет!"),
		.time = U("12:29"),
	});
	const auto top = first.y() + first.height() + 5. * u;
	if (on) {
		Bubble(k, r, top, {
			.text = U("Встреча в 8"),
			.time = U("12:30"),
			.deleted = true,
		});
		DrawText(
			k,
			QRectF(r.x(), top + 27. * u, r.width(), 12. * u),
			U("удалено, но осталось"),
			FontOf(k, 9.),
			k.c.sub);
	} else {
		const auto ghost = QRectF(r.x(), top, r.width() * 0.8, 22.5 * u);
		auto pen = QPen(Alpha(k.c.sub, 0.75), 1. * u, Qt::DashLine);
		pen.setDashPattern(QList<qreal>{ 3., 2.5 });
		k.p.setPen(pen);
		k.p.setBrush(Qt::NoBrush);
		k.p.drawRoundedRect(ghost, 8. * u, 8. * u);
		DrawText(
			k,
			ghost.adjusted(7. * u, 0., -7. * u, 0.),
			U("сообщение исчезло"),
			FontOf(k, 9.5),
			k.c.sub);
	}
}

void SceneEditHistory(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto bubble = Bubble(k, r, r.y(), {
		.text = U("Встреча в 9"),
		.time = U("12:31"),
		.edited = true,
	});
	const auto top = bubble.y() + bubble.height() + 6. * u;
	if (on) {
		const auto card = QRectF(
			r.x() + 8. * u,
			top,
			r.width() - 8. * u,
			r.y() + r.height() - top);
		FillRound(k, card, 7. * u, k.c.bg);
		k.p.setPen(PenOf(Alpha(k.c.sub, 0.35), 1. * u));
		k.p.setBrush(Qt::NoBrush);
		k.p.drawRoundedRect(card, 7. * u, 7. * u);
		DrawText(
			k,
			QRectF(card.x() + 7. * u, card.y() + 3. * u, card.width() - 14. * u, 12. * u),
			U("История изменений"),
			FontOf(k, 8.5, true),
			k.c.accent);
		DrawText(
			k,
			QRectF(card.x() + 7. * u, card.y() + 16. * u, card.width() - 14. * u, 14. * u),
			U("было: «в 8»"),
			FontOf(k, 10.),
			k.c.fg);
	} else {
		DrawText(
			k,
			QRectF(r.x(), top + 3. * u, r.width(), 12. * u),
			U("прежний текст не узнать"),
			FontOf(k, 9.5),
			k.c.sub);
	}
}

void SceneForwardNoAuthor(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Получатель увидит"));
	Bubble(k, r, r.y() + 16. * u, {
		.text = U("Смотри, закат!"),
		.time = U("12:30"),
		.header = on ? QString() : U("Переслано от Аня"),
	});
}

void SceneMultiForward(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto names = QStringList{ U("Мама"), U("Работа"), U("Друзья") };
	const auto letters = QStringList{ U("М"), U("Р"), U("Д") };
	const auto colors = std::array<QColor, 3>{
		QColor(0x9a, 0xd1, 0x64),
		QColor(0xfe, 0xbb, 0x5b),
		QColor(0x5c, 0xaf, 0xfa),
	};
	const auto rowHeight = 17. * u;
	for (auto i = 0; i != 3; ++i) {
		const auto top = r.y() + i * rowHeight;
		if (!on && i == 1) {
			FillRound(
				k,
				QRectF(r.x() - 3. * u, top - 1.5 * u, r.width() + 6. * u, 17. * u),
				6. * u,
				Alpha(k.c.accent, 0.16));
		}
		Row(k, r, top, 7., {
			.letter = letters[i],
			.avatar = colors[i],
			.name = names[i],
			.tag = (!on && i == 1) ? U("сразу") : QString(),
			.checkbox = on ? ((i != 1) ? 2 : 1) : 0,
		});
	}
	const auto top = r.y() + 3 * rowHeight + 2. * u;
	const auto button = QRectF(r.x(), top, r.width(), r.y() + r.height() - top);
	if (on) {
		FillRound(k, button, button.height() / 2., k.c.accent);
		DrawText(
			k,
			button,
			U("Отправить (2)"),
			FontOf(k, 9.5, true),
			k.c.accentFg,
			kCenter);
	} else {
		DrawText(
			k,
			button,
			U("клик — и сообщение ушло"),
			FontOf(k, 9.),
			k.c.sub,
			kCenter);
	}
}

void ConfirmDialog(
		const Canvas &k,
		const QRectF &r,
		const QString &question,
		bool gif) {
	const auto u = k.s;
	const auto dialog = r.adjusted(2. * u, 4. * u, -2. * u, -4. * u);
	FillRound(k, dialog, 8. * u, k.c.bg);
	k.p.setPen(PenOf(Alpha(k.c.sub, 0.35), 1. * u));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawRoundedRect(dialog, 8. * u, 8. * u);
	const auto icon = QPointF(dialog.x() + 17. * u, dialog.y() + 18. * u);
	if (gif) {
		GifTile(k, QRectF(icon.x() - 11. * u, icon.y() - 8. * u, 22. * u, 16. * u));
	} else {
		Face(k, icon, 9. * u);
	}
	DrawText(
		k,
		QRectF(dialog.x() + 32. * u, icon.y() - 7. * u, dialog.width() - 38. * u, 14. * u),
		question,
		FontOf(k, 9.5, true),
		k.c.fg);
	const auto buttonsTop = dialog.y() + dialog.height() - 18. * u;
	const auto font = FontOf(k, 9., true);
	const auto send = U("Отправить");
	const auto cancel = U("Отмена");
	const auto sendW = TextWidth(font, send) + 2. * u;
	const auto cancelW = TextWidth(font, cancel) + 2. * u;
	const auto right = dialog.x() + dialog.width() - 8. * u;
	DrawText(
		k,
		QRectF(right - sendW, buttonsTop, sendW, 13. * u),
		send,
		font,
		k.c.accent);
	DrawText(
		k,
		QRectF(right - sendW - 10. * u - cancelW, buttonsTop, cancelW, 13. * u),
		cancel,
		font,
		k.c.sub);
}

void SceneConfirmSticker(const Canvas &k, const QRectF &r, bool on, bool gif) {
	const auto u = k.s;
	if (on) {
		ConfirmDialog(
			k,
			r,
			gif ? U("Отправить GIF?") : U("Отправить стикер?"),
			gif);
		return;
	}
	Caption(k, r, r.y(), U("Случайный клик"));
	const auto right = r.x() + r.width();
	if (gif) {
		GifTile(k, QRectF(right - 54. * u, r.y() + 14. * u, 54. * u, 40. * u));
	} else {
		Face(k, QPointF(right - 22. * u, r.y() + 33. * u), 19. * u);
	}
	const auto font = FontOf(k, 8.5);
	const auto time = U("12:30");
	const auto timeW = TextWidth(font, time);
	const auto top = r.y() + r.height() - 12. * u;
	DrawText(
		k,
		QRectF(right - 12. * u - timeW, top, timeW + 1. * u, 12. * u),
		time,
		font,
		k.c.sub);
	Checks(k, right, top + 6. * u, 2, k.c.sub);
	DrawText(
		k,
		QRectF(r.x(), r.y() + 26. * u, r.width() * 0.5, 13. * u),
		U("уже ушёл"),
		FontOf(k, 9.5, true),
		k.c.attention);
}

void SceneConfirmVoice(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	if (on) {
		Caption(k, r, r.y(), U("Сначала прослушайте"));
		const auto bar = QRectF(r.x(), r.y() + 22. * u, r.width(), 28. * u);
		FillRound(k, bar, 14. * u, k.c.inBg);
		const auto trash = QRectF(bar.x() + 8. * u, bar.center().y() - 5. * u, 8.4 * u, 10. * u);
		PaintTrashIcon(k.p, trash, k.c.attention, 1.2 * u);
		PlayButton(
			k,
			QPointF(bar.x() + 31. * u, bar.center().y()),
			8. * u,
			Alpha(k.c.accent, 0.18),
			k.c.accent);
		const auto sendCenter = QPointF(
			bar.x() + bar.width() - 14. * u,
			bar.center().y());
		FillCircle(k, sendCenter, 10. * u, k.c.accent);
		auto arrow = QPainterPath();
		arrow.moveTo(sendCenter.x(), sendCenter.y() + 4. * u);
		arrow.lineTo(sendCenter.x(), sendCenter.y() - 4. * u);
		arrow.moveTo(sendCenter.x() - 3.5 * u, sendCenter.y() - 0.5 * u);
		arrow.lineTo(sendCenter.x(), sendCenter.y() - 4. * u);
		arrow.lineTo(sendCenter.x() + 3.5 * u, sendCenter.y() - 0.5 * u);
		k.p.setPen(PenOf(k.c.accentFg, 1.5 * u));
		k.p.setBrush(Qt::NoBrush);
		k.p.drawPath(arrow);
		Waveform(
			k,
			QRectF(
				bar.x() + 44. * u,
				bar.center().y() - 6. * u,
				bar.width() - 44. * u - 30. * u,
				12. * u),
			Alpha(k.c.accent, 0.8),
			14);
		return;
	}
	Caption(k, r, r.y(), U("Отпустили кнопку — ушло"));
	const auto width = std::min(r.width(), 128. * u);
	const auto bubble = QRectF(
		r.x() + r.width() - width,
		r.y() + 18. * u,
		width,
		32. * u);
	FillRound(k, bubble, 9. * u, k.c.outBg);
	PlayButton(
		k,
		QPointF(bubble.x() + 15. * u, bubble.center().y()),
		9. * u,
		k.c.outSub,
		k.c.outBg);
	Waveform(
		k,
		QRectF(
			bubble.x() + 29. * u,
			bubble.y() + 7. * u,
			bubble.width() - 36. * u,
			10. * u),
		k.c.outSub,
		16);
	const auto font = FontOf(k, 8.5);
	DrawText(
		k,
		QRectF(bubble.x() + 29. * u, bubble.y() + 18. * u, 30. * u, 11. * u),
		U("0:07"),
		font,
		k.c.outSub);
	Checks(
		k,
		bubble.x() + bubble.width() - 7. * u,
		bubble.y() + 23.5 * u,
		2,
		k.c.outSub);
}

void SceneLocalPins(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto rowHeight = 18. * u;
	Row(k, r, r.y(), 7.5, {
		.letter = U("М"),
		.avatar = QColor(0x9a, 0xd1, 0x64),
		.name = U("Мама"),
		.pin = true,
	});
	Row(k, r, r.y() + rowHeight, 7.5, {
		.letter = U("Р"),
		.avatar = QColor(0xfe, 0xbb, 0x5b),
		.name = U("Работа"),
		.pin = true,
	});
	const auto top = r.y() + 2 * rowHeight + 2. * u;
	if (on) {
		Row(k, r, top, 7.5, {
			.letter = U("Д"),
			.avatar = QColor(0x5c, 0xaf, 0xfa),
			.name = U("Друзья"),
			.pin = true,
			.tag = U("этот ПК"),
		});
	} else {
		const auto toast = QRectF(r.x(), top, r.width(), r.y() + r.height() - top);
		FillRound(k, toast, 7. * u, Alpha(k.c.fg, 0.82));
		DrawText(
			k,
			toast.adjusted(7. * u, 0., -7. * u, 0.),
			U("Лимит закрепов исчерпан"),
			FontOf(k, 9.),
			k.c.bg,
			kCenter);
	}
}

void SceneHideStories(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	auto top = r.y();
	if (!on) {
		const auto colors = std::array<QColor, 4>{
			QColor(0xff, 0x84, 0x5e),
			QColor(0x5c, 0xaf, 0xfa),
			QColor(0xb6, 0x94, 0xf9),
			QColor(0x9a, 0xd1, 0x64),
		};
		for (auto i = 0; i != 4; ++i) {
			const auto center = QPointF(
				r.x() + (10.5 + i * 24.) * u,
				top + 10.5 * u);
			auto gradient = QLinearGradient(
				center - QPointF(10. * u, 10. * u),
				center + QPointF(10. * u, 10. * u));
			gradient.setColorAt(0., QColor(0x4f, 0xd1, 0x7a));
			gradient.setColorAt(1., k.c.accent);
			k.p.setPen(QPen(QBrush(gradient), 1.6 * u));
			k.p.setBrush(Qt::NoBrush);
			k.p.drawEllipse(center, 9.8 * u, 9.8 * u);
			FillCircle(k, center, 7.4 * u, colors[i]);
		}
		top += 26. * u;
	}
	const auto rows = std::array<RowSpec, 3>{ {
		{
			.letter = U("А"),
			.avatar = QColor(0xff, 0x8a, 0xac),
			.name = U("Аня"),
		},
		{
			.letter = U("О"),
			.avatar = QColor(0x5c, 0xaf, 0xfa),
			.name = U("Олег"),
		},
		{
			.letter = U("М"),
			.avatar = QColor(0x9a, 0xd1, 0x64),
			.name = U("Мама"),
		},
	} };
	for (const auto &row : rows) {
		if (top + 16. * u > r.y() + r.height() + 1. * u) {
			break;
		}
		Row(k, r, top, 8., row);
		top += 19. * u;
	}
}

void SceneHideSponsored(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Bubble(k, r, r.y(), {
		.text = U("Новый выпуск уже тут"),
		.time = U("12:30"),
		.header = U("Новости"),
	});
	if (on) {
		return;
	}
	const auto top = r.y() + 39. * u;
	const auto ad = QRectF(r.x(), top, r.width() * 0.92, r.y() + r.height() - top);
	FillRound(k, ad, 8. * u, k.c.inBg);
	FillRound(
		k,
		QRectF(ad.x(), ad.y() + 4. * u, 2.6 * u, ad.height() - 8. * u),
		1.3 * u,
		k.c.accent);
	DrawText(
		k,
		QRectF(ad.x() + 8. * u, ad.y() + 3. * u, ad.width() - 14. * u, 13. * u),
		U("Реклама"),
		FontOf(k, 9.5, true),
		k.c.accent);
	DrawText(
		k,
		QRectF(ad.x() + 8. * u, ad.y() + 16. * u, ad.width() - 14. * u, 14. * u),
		U("Купите слона!"),
		FontOf(k, 10.),
		k.c.inFg);
}

void SceneHideEmojiStatus(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Row(k, r, r.y() + 4. * u, 10., {
		.letter = U("А"),
		.avatar = QColor(0xff, 0x8a, 0xac),
		.name = U("Аня"),
		.status = U("в сети"),
		.statusColor = k.c.accent,
		.star = !on,
	});
	Row(k, r, r.y() + 32. * u, 10., {
		.letter = U("О"),
		.avatar = QColor(0x5c, 0xaf, 0xfa),
		.name = U("Олег"),
		.status = U("был(а) недавно"),
		.statusColor = k.c.sub,
		.star = !on,
	});
}

void SceneHidePhone(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Ваш профиль"));
	const auto top = r.y() + 17. * u;
	Row(k, r, top, 12., {
		.letter = U("В"),
		.avatar = k.c.accent,
		.name = U("Вы"),
		.status = on ? U(" ") : U("+7 999 123-45-67"),
		.statusColor = k.c.sub,
	});
	if (on) {
		const auto x = r.x() + 30. * u;
		Spoiler(
			k,
			QRectF(x, top + 13. * u, std::min(84. * u, r.x() + r.width() - x), 10. * u),
			k.c.fg);
		DrawText(
			k,
			QRectF(r.x(), top + 31. * u, r.width(), 12. * u),
			U("нажмите, чтобы показать"),
			FontOf(k, 9.),
			k.c.sub);
	}
}

void ScenePreciseLastSeen(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Caption(k, r, r.y(), U("Шапка чата"));
	Row(k, r, r.y() + 20. * u, 13., {
		.letter = U("А"),
		.avatar = QColor(0xff, 0x8a, 0xac),
		.name = U("Аня"),
		.status = on ? U("был(а) в 12:31:47") : U("был(а) 5 минут назад"),
		.statusColor = on ? k.c.accent : k.c.sub,
	});
}

void SceneShowSeconds(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Bubble(k, r, r.y(), {
		.text = U("Привет!"),
		.time = on ? U("12:31:47") : U("12:31"),
	});
	Bubble(k, r, r.y() + 28. * u, {
		.text = U("Приветик"),
		.time = on ? U("12:32:05") : U("12:32"),
		.out = true,
		.checks = 2,
	});
}

void SceneRegistrationDate(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Row(k, r, r.y(), 12., {
		.letter = U("А"),
		.avatar = QColor(0xff, 0x8a, 0xac),
		.name = U("Аня"),
		.status = U("@anya"),
		.statusColor = k.c.accent,
	});
	const auto top = r.y() + 32. * u;
	if (on) {
		DrawText(
			k,
			QRectF(r.x(), top, r.width(), 14. * u),
			U("~ март 2016"),
			FontOf(k, 10.5, true),
			k.c.fg);
		DrawText(
			k,
			QRectF(r.x(), top + 14. * u, r.width(), 12. * u),
			U("Аккаунт создан"),
			FontOf(k, 9.),
			k.c.sub);
	} else {
		DrawText(
			k,
			QRectF(r.x(), top + 7. * u, r.width(), 12. * u),
			U("возраст аккаунта неизвестен"),
			FontOf(k, 9.),
			k.c.sub);
	}
}

void SceneSnowfall(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	Bubble(k, r, r.y() + 6. * u, {
		.text = U("С наступающим!"),
		.time = U("23:59"),
	});
	Bubble(k, r, r.y() + 36. * u, {
		.text = U("И тебя!"),
		.time = U("00:00"),
		.out = true,
		.checks = 2,
	});
	if (!on) {
		return;
	}
	const auto color = IsDark(k.c.chat)
		? QColor(0xff, 0xff, 0xff)
		: Mix(k.c.accent, QColor(0xff, 0xff, 0xff), 0.3);
	constexpr auto kFlakes = std::array<std::array<double, 3>, 14>{ {
		{ 0.05, 0.06, 1.4 },
		{ 0.27, 0.02, 1.0 },
		{ 0.62, 0.04, 1.7 },
		{ 0.93, 0.12, 1.2 },
		{ 0.80, 0.30, 1.0 },
		{ 0.08, 0.55, 1.1 },
		{ 0.22, 0.74, 1.6 },
		{ 0.04, 0.96, 1.0 },
		{ 0.38, 0.90, 1.2 },
		{ 0.55, 0.97, 1.5 },
		{ 0.74, 0.93, 1.0 },
		{ 0.96, 0.78, 1.3 },
		{ 0.47, 0.47, 0.9 },
		{ 0.16, 0.33, 0.9 },
	} };
	for (const auto &flake : kFlakes) {
		const auto center = QPointF(
			r.x() + flake[0] * r.width(),
			r.y() + flake[1] * r.height());
		const auto size = flake[2] * u;
		FillCircle(k, center, size, Alpha(color, 0.9));
		if (flake[2] >= 1.5) {
			for (auto i = 0; i != 3; ++i) {
				const auto angle = i * kPi / 3.;
				const auto dx = std::cos(angle) * size * 2.2;
				const auto dy = std::sin(angle) * size * 2.2;
				StrokeLine(
					k,
					QPointF(center.x() - dx, center.y() - dy),
					QPointF(center.x() + dx, center.y() + dy),
					Alpha(color, 0.6),
					0.7 * u);
			}
		}
	}
}

void SceneTheme(const Canvas &k, Scene scene, const QRectF &r, bool on) {
	if (!on) {
		MiniWindow(k, r);
		return;
	}
	const auto themed = Canvas{ k.p, ThemeColors(scene, k.c), k.base, k.s };
	MiniWindow(themed, r);
}

void PaintScene(const Canvas &k, Scene scene, bool on, const QRectF &r) {
	switch (scene) {
	case Scene::GhostAll: SceneGhostAll(k, r, on); return;
	case Scene::GhostRead: SceneGhostRead(k, r, on); return;
	case Scene::GhostTyping: SceneGhostTyping(k, r, on); return;
	case Scene::GhostOnline: SceneGhostOnline(k, r, on); return;
	case Scene::GhostStories: SceneGhostStories(k, r, on); return;
	case Scene::Streamer: SceneStreamer(k, r, on); return;
	case Scene::SaveDeleted: SceneSaveDeleted(k, r, on); return;
	case Scene::EditHistory: SceneEditHistory(k, r, on); return;
	case Scene::ForwardNoAuthor: SceneForwardNoAuthor(k, r, on); return;
	case Scene::MultiForward: SceneMultiForward(k, r, on); return;
	case Scene::ConfirmStickers: SceneConfirmSticker(k, r, on, false); return;
	case Scene::ConfirmGifs: SceneConfirmSticker(k, r, on, true); return;
	case Scene::ConfirmVoice: SceneConfirmVoice(k, r, on); return;
	case Scene::LocalPins: SceneLocalPins(k, r, on); return;
	case Scene::HideStories: SceneHideStories(k, r, on); return;
	case Scene::HideSponsored: SceneHideSponsored(k, r, on); return;
	case Scene::HideEmojiStatus: SceneHideEmojiStatus(k, r, on); return;
	case Scene::HidePhone: SceneHidePhone(k, r, on); return;
	case Scene::PreciseLastSeen: ScenePreciseLastSeen(k, r, on); return;
	case Scene::ShowSeconds: SceneShowSeconds(k, r, on); return;
	case Scene::RegistrationDate: SceneRegistrationDate(k, r, on); return;
	case Scene::Snowfall: SceneSnowfall(k, r, on); return;
	case Scene::ThemeLight:
	case Scene::ThemeDark:
	case Scene::ThemeAmoled: SceneTheme(k, scene, r, on); return;
	}
}

void Panel(
		const Canvas &k,
		const QRectF &rect,
		Scene scene,
		bool on,
		const QString &label,
		double active,
		bool showTag) {
	const auto u = k.s;
	const auto radius = 12. * u;
	FillRound(k, rect, radius, k.c.chat);

	k.p.save();
	auto clip = QPainterPath();
	clip.addRoundedRect(rect, radius, radius);
	k.p.setClipPath(clip, Qt::IntersectClip);
	PaintScene(
		k,
		scene,
		on,
		rect.adjusted(9. * u, 23. * u, -9. * u, -8. * u));
	k.p.restore();

	const auto labelColor = Mix(k.c.sub, k.c.accent, active);
	DrawText(
		k,
		QRectF(rect.x() + 10. * u, rect.y() + 5. * u, rect.width() - 20. * u, 14. * u),
		label,
		FontOf(k, 9.5, true),
		labelColor);
	if (showTag && active > 0.) {
		const auto font = FontOf(k, 8., true);
		const auto text = U("сейчас");
		const auto w = TextWidth(font, text) + 10. * u;
		const auto pill = QRectF(
			rect.x() + rect.width() - 8. * u - w,
			rect.y() + 5.5 * u,
			w,
			13. * u);
		FillRound(k, pill, 6.5 * u, Alpha(k.c.accent, active));
		DrawText(k, pill, text, font, Alpha(k.c.accentFg, active), kCenter);
	}
	if (active > 0.) {
		const auto half = 0.8 * u;
		k.p.setPen(QPen(Alpha(k.c.accent, active), 1.6 * u));
		k.p.setBrush(Qt::NoBrush);
		k.p.drawRoundedRect(
			rect.adjusted(half, half, -half, -half),
			radius - half,
			radius - half);
	}
}

} // namespace

QString SceneTitle(Scene scene) {
	switch (scene) {
	case Scene::GhostAll: return U("Режим призрака целиком");
	case Scene::GhostRead: return U("Не отправлять «прочитано»");
	case Scene::GhostTyping: return U("Не отправлять «печатает…»");
	case Scene::GhostOnline: return U("Всегда «не в сети»");
	case Scene::GhostStories: return U("Смотреть истории анонимно");
	case Scene::Streamer: return U("Скрывать окна от записи экрана");
	case Scene::SaveDeleted: return U("Сохранять удалённые сообщения");
	case Scene::EditHistory: return U("Сохранять историю изменений");
	case Scene::ForwardNoAuthor: return U("Пересылать без автора");
	case Scene::MultiForward: return U("Пересылать в несколько чатов");
	case Scene::ConfirmStickers: return U("Спрашивать перед отправкой стикера");
	case Scene::ConfirmGifs: return U("Спрашивать перед отправкой GIF");
	case Scene::ConfirmVoice: return U("Голосовые — сначала прослушать");
	case Scene::LocalPins: return U("Закреплять чаты сверх лимита");
	case Scene::HideStories: return U("Скрыть истории");
	case Scene::HideSponsored: return U("Скрыть рекламу в каналах");
	case Scene::HideEmojiStatus: return U("Скрыть эмодзи-статусы");
	case Scene::HidePhone: return U("Скрывать мой номер");
	case Scene::PreciseLastSeen: return U("Точное время «был(а) в сети»");
	case Scene::ShowSeconds: return U("Секунды во времени сообщений");
	case Scene::RegistrationDate: return U("Дата создания аккаунта");
	case Scene::Snowfall: return U("Снегопад");
	case Scene::ThemeLight: return U("Светлая тема Telewhite");
	case Scene::ThemeDark: return U("Тёмная тема Telewhite");
	case Scene::ThemeAmoled: return U("Полностью чёрная тема");
	}
	return QString();
}

QString SceneHint(Scene scene) {
	switch (scene) {
	case Scene::GhostAll: return U("Один переключатель включает всё сразу: без «прочитано», «печатает…», «в сети» и просмотров историй.");
	case Scene::GhostRead: return U("Собеседник видит одну галочку, пока вы не ответите или не выберете «Отправить „прочитано“» в меню чата.");
	case Scene::GhostTyping: return U("Вы набираете ответ, а у собеседника в шапке чата не появляется «печатает…».");
	case Scene::GhostOnline: return U("Telewhite не сообщает серверу, что вы в сети: зелёной точки и «в сети» у вас не будет.");
	case Scene::GhostStories: return U("Истории открываются как обычно, но вашего имени нет в списке просмотревших.");
	case Scene::Streamer: return U("На стриме, записи и скриншотах окон Telewhite нет, а на вашем мониторе они видны как обычно.");
	case Scene::SaveDeleted: return U("Удалённое собеседником сообщение остаётся в чате: оно полупрозрачное, а у времени — корзинка.");
	case Scene::EditHistory: return U("Правый клик по изменённому сообщению → «История изменений» покажет прежний текст.");
	case Scene::ForwardNoAuthor: return U("Пересланное сообщение приходит как ваше — без строки «Переслано от…».");
	case Scene::MultiForward: return U("В окне пересылки клик ставит галочку: отметьте несколько чатов и отправьте одной кнопкой.");
	case Scene::ConfirmStickers: return U("Стикер из панели, подсказок и поиска уходит только после вашего подтверждения.");
	case Scene::ConfirmGifs: return U("GIF уходит только после подтверждения — случайный клик ничего не отправит.");
	case Scene::ConfirmVoice: return U("Записанное голосовое или кружок не уходит сразу: прослушайте, затем отправьте или удалите.");
	case Scene::LocalPins: return U("Когда лимит закрепов Telegram исчерпан, чат закрепляется только на этом компьютере.");
	case Scene::HideStories: return U("Полоса историй над списком чатов больше не показывается.");
	case Scene::HideSponsored: return U("Спонсорские сообщения в каналах не показываются.");
	case Scene::HideEmojiStatus: return U("Значки эмодзи-статусов рядом с именами пропадают — интерфейс спокойнее.");
	case Scene::HidePhone: return U("Номер в настройках и в профиле закрыт спойлером: нажмите, чтобы посмотреть.");
	case Scene::PreciseLastSeen: return U("Вместо «5 минут назад» — точное время последнего визита с секундами.");
	case Scene::ShowSeconds: return U("У каждого сообщения время показывается с секундами.");
	case Scene::RegistrationDate: return U("В профиле появляется примерная дата создания аккаунта, группы или канала.");
	case Scene::Snowfall: return U("Поверх окна мягко падает снег. Клики проходят сквозь снежинки.");
	case Scene::ThemeLight: return U("Белый фон и фиолетовые акценты. Нажмите, чтобы применить.");
	case Scene::ThemeDark: return U("Ночная тема с фиолетовым акцентом. Нажмите, чтобы применить.");
	case Scene::ThemeAmoled: return U("Чистый чёрный фон — для OLED-экранов и работы ночью. Нажмите, чтобы применить.");
	}
	return QString();
}

bool SceneIsTheme(Scene scene) {
	return (scene == Scene::ThemeLight)
		|| (scene == Scene::ThemeDark)
		|| (scene == Scene::ThemeAmoled);
}

int CardHeight(double scale) {
	return int(std::ceil(kCardHeight * scale));
}

void PaintCard(
		QPainter &p,
		const QRectF &rect,
		Scene scene,
		double state,
		const Palette &palette) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);

	const auto k = Canvas{
		p,
		ColorsFromPalette(palette),
		palette.font,
		palette.scale,
	};
	const auto u = k.s;
	const auto pad = 14. * u;
	const auto inner = rect.adjusted(pad, 0., -pad, 0.);
	auto y = rect.y() + 9. * u;
	DrawText(
		k,
		QRectF(inner.x(), y, inner.width(), 18. * u),
		SceneTitle(scene),
		FontOf(k, 13., true),
		k.c.fg);
	y += 20. * u;
	const auto hintFont = FontOf(k, 11.);
	for (const auto &line : Wrap(SceneHint(scene), hintFont, inner.width(), 2)) {
		DrawText(
			k,
			QRectF(inner.x(), y, inner.width(), 15. * u),
			line,
			hintFont,
			k.c.sub);
		y += 15. * u;
	}

	const auto theme = SceneIsTheme(scene);
	const auto gap = 8. * u;
	const auto top = rect.y() + kPanelsTop * u;
	const auto width = (inner.width() - gap) / 2.;
	const auto height = kPanelHeight * u;
	const auto clamped = std::clamp(state, 0., 1.);
	Panel(
		k,
		QRectF(inner.x(), top, width, height),
		scene,
		false,
		theme ? U("Сейчас") : U("Без мода"),
		theme ? 0. : (1. - clamped),
		!theme);
	Panel(
		k,
		QRectF(inner.x() + width + gap, top, width, height),
		scene,
		true,
		theme ? U("После") : U("С модом"),
		theme ? 0. : clamped,
		!theme);
	p.restore();
}

int BannerHeight(double scale) {
	return int(std::ceil(64. * scale));
}

int BannerRadius(double scale) {
	return int(std::lround(14. * scale));
}

void PaintGhostIcon(QPainter &p, const QRectF &rect, const QColor &color) {
	const auto x = rect.x();
	const auto y = rect.y();
	const auto w = rect.width();
	const auto h = rect.height();
	const auto bottom = y + h;
	const auto wave = h * 0.2;

	auto path = QPainterPath();
	path.setFillRule(Qt::OddEvenFill);
	path.moveTo(x, y + w / 2.);
	path.arcTo(QRectF(x, y, w, w), 180., -180.);
	path.lineTo(x + w, bottom);
	for (auto i = 0; i != 3; ++i) {
		const auto from = x + w - (w / 3.) * i;
		const auto till = from - w / 3.;
		path.quadTo(
			QPointF((from + till) / 2., bottom - wave * 1.6),
			QPointF(till, bottom));
	}
	path.closeSubpath();
	const auto eye = QSizeF(w * 0.17, w * 0.24);
	for (const auto center : { 0.34, 0.66 }) {
		path.addEllipse(QRectF(
			QPointF(
				x + w * center - eye.width() / 2.,
				y + h * 0.36 - eye.height() / 2.),
			eye));
	}

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
	p.restore();
}

void PaintBanner(QPainter &p, const QRectF &rect, const Banner &banner) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	const auto u = banner.scale;
	const auto k = Canvas{ p, Colors(), banner.font, u };
	const auto over = std::clamp(banner.over, 0., 1.) * 0.1;
	const auto white = QColor(255, 255, 255);
	const auto radius = double(BannerRadius(u));

	auto shape = QPainterPath();
	shape.addRoundedRect(rect, radius, radius);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., Mix(QColor(0xa1, 0x7f, 0xff), white, over));
	gradient.setColorAt(0.55, Mix(QColor(0x7a, 0x5c, 0xf5), white, over));
	gradient.setColorAt(1., Mix(QColor(0x58, 0x3a, 0xe0), white, over));
	p.fillPath(shape, gradient);

	p.setClipPath(shape, Qt::IntersectClip);
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(white, 0.09));
	p.drawEllipse(
		QPointF(rect.right() - 34. * u, rect.top() + 4. * u),
		38. * u,
		38. * u);
	p.setBrush(Alpha(white, 0.07));
	p.drawEllipse(
		QPointF(rect.right() - 96. * u, rect.bottom() + 2. * u),
		20. * u,
		20. * u);
	p.setClipping(false);

	const auto iconSize = 40. * u;
	const auto icon = QRectF(
		rect.x() + 12. * u,
		rect.y() + (rect.height() - iconSize) / 2.,
		iconSize,
		iconSize);
	p.setBrush(Alpha(white, 0.2));
	p.drawEllipse(icon);
	PaintGhostIcon(
		p,
		QRectF(
			icon.x() + 11. * u,
			icon.y() + 9.5 * u,
			iconSize - 22. * u,
			iconSize - 19. * u),
		white);

	const auto arrowX = rect.right() - 18. * u;
	const auto arrowY = rect.center().y();
	auto arrow = QPainterPath();
	arrow.moveTo(arrowX - 3.5 * u, arrowY - 6. * u);
	arrow.lineTo(arrowX + 2.5 * u, arrowY);
	arrow.lineTo(arrowX - 3.5 * u, arrowY + 6. * u);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		Alpha(white, 0.9),
		2. * u,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.drawPath(arrow);

	const auto left = icon.right() + 12. * u;
	const auto width = arrowX - 14. * u - left;
	DrawText(
		k,
		QRectF(left, arrowY - 19. * u, width, 20. * u),
		banner.title,
		FontOf(k, 14.5, true),
		white);
	DrawText(
		k,
		QRectF(left, arrowY + 1. * u, width, 17. * u),
		banner.about,
		FontOf(k, 12.),
		Alpha(white, 0.82));
	p.restore();
}

void PaintTrashIcon(
		QPainter &p,
		const QRectF &rect,
		const QColor &color,
		double lineWidth) {
	const auto x = rect.x();
	const auto y = rect.y();
	const auto w = rect.width();
	const auto h = rect.height();
	const auto lid = y + h * 0.2;

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(color, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);

	p.drawLine(QPointF(x, lid), QPointF(x + w, lid));

	auto handle = QPainterPath();
	handle.moveTo(x + w * 0.34, lid);
	handle.lineTo(x + w * 0.34, y);
	handle.lineTo(x + w * 0.66, y);
	handle.lineTo(x + w * 0.66, lid);
	p.drawPath(handle);

	auto body = QPainterPath();
	body.moveTo(x + w * 0.12, lid + h * 0.12);
	body.lineTo(x + w * 0.2, y + h);
	body.lineTo(x + w * 0.8, y + h);
	body.lineTo(x + w * 0.88, lid + h * 0.12);
	p.drawPath(body);

	p.drawLine(
		QPointF(x + w * 0.5, lid + h * 0.28),
		QPointF(x + w * 0.5, y + h * 0.8));
	p.restore();
}

} // namespace Telewhite::Preview
