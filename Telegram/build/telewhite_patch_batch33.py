'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# Batch 33, applied at build time with the rules of telewhite_patch.py:
# bubbles with large rounding without pointed edges, previews of the new
# mods, the glass profile (section cards, glass action buttons) and the
# dictation instead of voice messages. Each piece is skipped once its text
# is already in the source. Telegram/configure.py imports this module.
#
#   python telewhite_patch_batch33.py [--root <repository>] [--check]

import os
import sys

import telewhite_patch
import telewhite_patch_more  # noqa: F401, keeps the order of the features.
import telewhite_patch_batch32  # noqa: F401, the settings rows go first.

MESSAGE_BUBBLE = 'Telegram/SourceFiles/ui/chat/message_bubble.cpp'
PREVIEW = 'Telegram/SourceFiles/telewhite/telewhite_mods_preview.cpp'
SECTION_STACK = 'Telegram/SourceFiles/info/profile/info_profile_section_stack.cpp'
CONTENT_WIDGET = 'Telegram/SourceFiles/info/info_content_widget.cpp'
ACTION_BUTTON = 'Telegram/SourceFiles/info/profile/info_profile_top_bar_action_button.cpp'
HISTORY_WIDGET = 'Telegram/SourceFiles/history/history_widget.cpp'
COMPOSE_CONTROLS = 'Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp'
SETTINGS = 'Telegram/SourceFiles/telewhite/telewhite_settings.cpp'

FEATURES = [
    {
        'id': 'bubble-clamp',
        'title': 'bubbles with large rounding painted without pointed edges',
        'pieces': [
            {
                'file': MESSAGE_BUBBLE,
                'find': r'''#include "ui/chat/torn_edge.h"''',
                'replace': r'''#include "ui/chat/torn_edge.h"
#include "ui/painter.h"

#include <QtGui/QPainterPath>''',
            },
            {
                'file': MESSAGE_BUBBLE,
                'find': r'''void PaintBubble(QPainter &p, const SimpleBubble &args) {
	if (UsePatternBubble(args)) {''',
                'replace': r'''namespace {

// Telewhite: the "Bubble rounding" option can make the corners larger than
// a half of a short bubble, the cached corner images overlap then and give
// a pointed edge. Such bubbles are painted by a path with clamped corners,
// see "bubble-clamp" in telewhite_patch_batch33.py.
[[nodiscard]] int TelewhiteCornerSize(Corner corner) {
	return (corner == Corner::Large)
		? BubbleRadiusLarge()
		: (corner == Corner::Small)
		? BubbleRadiusSmall()
		: 0;
}

[[nodiscard]] bool TelewhiteCornersOverlap(const SimpleBubble &args) {
	const auto &rounding = args.rounding;
	const auto rect = args.geometry;
	const auto topLeft = TelewhiteCornerSize(rounding.topLeft);
	const auto topRight = TelewhiteCornerSize(rounding.topRight);
	const auto bottomLeft = TelewhiteCornerSize(rounding.bottomLeft);
	const auto bottomRight = TelewhiteCornerSize(rounding.bottomRight);
	return (std::max(topLeft, topRight) + std::max(bottomLeft, bottomRight)
			> rect.height())
		|| (std::max(topLeft, bottomLeft) + std::max(topRight, bottomRight)
			> rect.width());
}

[[nodiscard]] QPainterPath TelewhiteBubblePath(const SimpleBubble &args) {
	const auto rect = QRectF(args.geometry);
	const auto limit = std::min(rect.width(), rect.height()) / 2.;
	const auto size = [&](Corner corner) {
		return std::min(float64(TelewhiteCornerSize(corner)), limit);
	};
	const auto topLeft = size(args.rounding.topLeft);
	const auto topRight = size(args.rounding.topRight);
	const auto bottomRight = size(args.rounding.bottomRight);
	const auto bottomLeft = size(args.rounding.bottomLeft);
	auto result = QPainterPath();
	result.moveTo(rect.left() + topLeft, rect.top());
	result.lineTo(rect.right() - topRight, rect.top());
	if (topRight > 0.) {
		result.arcTo(
			QRectF(
				rect.right() - 2. * topRight,
				rect.top(),
				2. * topRight,
				2. * topRight),
			90.,
			-90.);
	}
	result.lineTo(rect.right(), rect.bottom() - bottomRight);
	if (bottomRight > 0.) {
		result.arcTo(
			QRectF(
				rect.right() - 2. * bottomRight,
				rect.bottom() - 2. * bottomRight,
				2. * bottomRight,
				2. * bottomRight),
			0.,
			-90.);
	}
	result.lineTo(rect.left() + bottomLeft, rect.bottom());
	if (bottomLeft > 0.) {
		result.arcTo(
			QRectF(
				rect.left(),
				rect.bottom() - 2. * bottomLeft,
				2. * bottomLeft,
				2. * bottomLeft),
			270.,
			-90.);
	}
	result.lineTo(rect.left(), rect.top() + topLeft);
	if (topLeft > 0.) {
		result.arcTo(
			QRectF(rect.left(), rect.top(), 2. * topLeft, 2. * topLeft),
			180.,
			-90.);
	}
	result.closeSubpath();
	return result;
}

void TelewhitePaintClampedBubble(QPainter &p, const SimpleBubble &args) {
	const auto rect = args.geometry;
	const auto path = TelewhiteBubblePath(args);
	const auto shadow = args.shadowed
		? path.translated(0., float64(st::msgShadow)).subtracted(path)
		: QPainterPath();
	const auto leftTail = (args.rounding.bottomLeft == Corner::Tail);
	const auto rightTail = (args.rounding.bottomRight == Corner::Tail);
	const auto bottomPosition = rightTail
		? QPoint(rect.x() + rect.width(), rect.y() + rect.height())
		: QPoint(rect.x(), rect.y() + rect.height());
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	if (UsePatternBubble(args)) {
		const auto wasOpacity = p.opacity();
		const auto opacity = PatternBubbleOpacity(args, wasOpacity);
		const auto pattern = args.pattern;
		const auto fill = [&](const QPainterPath &shape) {
			const auto target = shape.boundingRect().toAlignedRect().intersected(
				args.patternViewport);
			if (target.isEmpty()) {
				return;
			}
			p.save();
			p.setClipPath(shape, Qt::IntersectClip);
			PaintPatternBubblePart(
				p,
				args.patternViewport,
				pattern->pixmap,
				target);
			p.restore();
		};
		if (!shadow.isEmpty()) {
			p.setOpacity(opacity * args.st->msgOutShadow()->c.alphaF());
			fill(shadow);
		}
		p.setOpacity(opacity);
		fill(path);
		if (leftTail || rightTail) {
			const auto &tail = rightTail
				? pattern->tailRight
				: pattern->tailLeft;
			const auto ratio = int(tail.devicePixelRatio());
			const auto tailShift = (rightTail
				? QPoint(0, tail.height())
				: QPoint(tail.width(), tail.height())) / ratio;
			PaintPatternBubblePart(
				p,
				args.patternViewport,
				pattern->pixmap,
				QRect(bottomPosition - tailShift, tail.size() / ratio),
				tail,
				pattern->tailCache);
		}
		p.setOpacity(wasOpacity);
		return;
	}
	const auto &colors = args.st->messageStyle(args.outbg, args.selected);
	if (!shadow.isEmpty()) {
		p.setBrush(colors.msgShadow);
		p.drawPath(shadow);
	}
	p.setBrush(colors.msgBg);
	p.drawPath(path);
	if (leftTail || rightTail) {
		const auto &tail = rightTail ? colors.tailRight : colors.tailLeft;
		const auto tailShift = rightTail
			? QPoint(0, tail.height())
			: QPoint(tail.width(), tail.height());
		tail.paint(p, bottomPosition - tailShift, args.outerWidth);
		if (args.shadowed) {
			const auto tailLeft = rightTail
				? (rect.x() + rect.width())
				: (rect.x() - tail.width());
			p.fillRect(
				QRect(
					tailLeft,
					rect.y() + rect.height(),
					tail.width(),
					st::msgShadow),
				colors.msgShadow);
		}
	}
}

} // namespace

void PaintBubble(QPainter &p, const SimpleBubble &args) {
	if (TelewhiteCornersOverlap(args)) {
		TelewhitePaintClampedBubble(p, args);
		return;
	}
	if (UsePatternBubble(args)) {''',
            },
        ],
    },
    {
        'id': 'preview-scenes-33',
        'title': 'previews of the new mods in the "?" boxes',
        'pieces': [
            {
                'file': PREVIEW,
                'find': r'''void PaintScene(const Canvas &k, Scene scene, bool on, const QRectF &r) {''',
                'replace': r'''void Handset(
		const Canvas &k,
		const QPointF &center,
		double size,
		const QColor &color) {
	const auto radius = size * 0.42;
	auto path = QPainterPath();
	const auto box = QRectF(
		center.x() - radius,
		center.y() - radius,
		2. * radius,
		2. * radius);
	path.arcMoveTo(box, 120.);
	path.arcTo(box, 120., 170.);
	auto pen = PenOf(color, size * 0.26);
	pen.setCapStyle(Qt::RoundCap);
	k.p.setPen(pen);
	k.p.setBrush(Qt::NoBrush);
	k.p.drawPath(path);
}

void Mic(
		const Canvas &k,
		const QPointF &center,
		double size,
		const QColor &color) {
	const auto w = size * 0.34;
	const auto h = size * 0.56;
	const auto body = QRectF(
		center.x() - w / 2.,
		center.y() - h * 0.72,
		w,
		h);
	FillRound(k, body, w / 2., color);
	const auto line = std::max(size * 0.09, 1.);
	auto path = QPainterPath();
	const auto radius = size * 0.32;
	const auto box = QRectF(
		center.x() - radius,
		body.y() + h - radius * 1.25,
		2. * radius,
		2. * radius);
	path.arcMoveTo(box, 200.);
	path.arcTo(box, 200., 140.);
	k.p.setPen(PenOf(color, line));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawPath(path);
	const auto stemTop = box.y() + box.height();
	StrokeLine(
		k,
		QPointF(center.x(), stemTop),
		QPointF(center.x(), stemTop + size * 0.14),
		color,
		line);
}

void SceneMentionReply(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto first = Bubble(k, r, r.y(), {
		.text = U("Встречаемся в 7?"),
		.time = U("12:30"),
		.header = U("Аня"),
		.deleted = true,
	});
	Bubble(k, r, first.y() + first.height() + 5. * u, {
		.text = on ? U("@anya да, буду!") : U("Да, буду!"),
		.time = U("12:31"),
		.out = true,
		.checks = 1,
	});
}

void SceneConfirmCalls(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	if (!on) {
		Caption(k, r, r.y(), U("Случайный тык — уже звоним"));
		const auto center = QPointF(r.center().x(), r.y() + 33. * u);
		Avatar(k, center, 12. * u, QColor(0xff, 0x8a, 0xac), U("А"), false);
		DrawText(
			k,
			QRectF(r.x(), center.y() + 14. * u, r.width(), 12. * u),
			U("Звоним Ане…"),
			FontOf(k, 9.5, true),
			k.c.fg,
			kCenter);
		const auto hang = QPointF(r.center().x(), r.y() + r.height() - 9. * u);
		FillCircle(k, hang, 8.5 * u, k.c.attention);
		FillRound(
			k,
			QRectF(hang.x() - 5. * u, hang.y() - 1.6 * u, 10. * u, 3.2 * u),
			1.6 * u,
			QColor(0xff, 0xff, 0xff));
		return;
	}
	const auto dialog = r.adjusted(2. * u, 4. * u, -2. * u, -4. * u);
	FillRound(k, dialog, 8. * u, k.c.bg);
	k.p.setPen(PenOf(Alpha(k.c.sub, 0.35), 1. * u));
	k.p.setBrush(Qt::NoBrush);
	k.p.drawRoundedRect(dialog, 8. * u, 8. * u);
	const auto icon = QPointF(dialog.x() + 16. * u, dialog.y() + 18. * u);
	FillCircle(k, icon, 9. * u, Alpha(k.c.accent, 0.16));
	Handset(k, icon, 11. * u, k.c.accent);
	DrawText(
		k,
		QRectF(
			dialog.x() + 30. * u,
			icon.y() - 7. * u,
			dialog.width() - 36. * u,
			14. * u),
		U("Позвонить Ане?"),
		FontOf(k, 9.5, true),
		k.c.fg);
	const auto buttonsTop = dialog.y() + dialog.height() - 18. * u;
	const auto font = FontOf(k, 9., true);
	const auto call = U("Позвонить");
	const auto cancel = U("Отмена");
	const auto callW = TextWidth(font, call) + 2. * u;
	const auto cancelW = TextWidth(font, cancel) + 2. * u;
	const auto right = dialog.x() + dialog.width() - 8. * u;
	DrawText(
		k,
		QRectF(right - callW, buttonsTop, callW, 13. * u),
		call,
		font,
		k.c.accent);
	DrawText(
		k,
		QRectF(right - callW - 10. * u - cancelW, buttonsTop, cancelW, 13. * u),
		cancel,
		font,
		k.c.sub);
}

void SceneUnlimitedSearch(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto field = QRectF(r.x(), r.y(), r.width(), 16. * u);
	FillRound(k, field, 8. * u, Alpha(k.c.sub, 0.16));
	DrawText(
		k,
		field.adjusted(8. * u, 0., -8. * u, 0.),
		U("Поиск"),
		FontOf(k, 9.),
		k.c.sub);
	const auto colors = std::array{
		QColor(0xff, 0x8a, 0xac),
		QColor(0x5c, 0xaf, 0xfa),
		QColor(0x8e, 0xd0, 0x6a),
		QColor(0xff, 0xb3, 0x47),
		QColor(0xb1, 0x8c, 0xff),
		QColor(0x5e, 0xd5, 0xc8),
	};
	const auto letters = std::array{
		U("А"), U("О"), U("М"), U("К"), U("Д"), U("С"),
	};
	const auto radius = 7. * u;
	const auto step = 2. * radius + 4. * u;
	const auto rows = on ? 2 : 1;
	const auto perRow = std::max(1, int((r.width() + 4. * u) / step));
	for (auto row = 0; row != rows; ++row) {
		const auto y = field.y() + field.height() + 7. * u + radius
			+ row * (2. * radius + 4. * u);
		const auto count = (row == 0) ? std::min(perRow, on ? perRow : 4) : perRow;
		for (auto i = 0; i != count; ++i) {
			const auto index = (row * perRow + i) % int(colors.size());
			Avatar(
				k,
				QPointF(r.x() + radius + i * step, y),
				radius,
				colors[index],
				letters[index],
				false);
		}
	}
	DrawText(
		k,
		QRectF(r.x(), r.y() + r.height() - 12. * u, r.width(), 12. * u),
		on ? U("Помнит до 1000 чатов") : U("Не больше 48"),
		FontOf(k, 9., on),
		on ? k.c.accent : k.c.sub);
}

void SceneHideBlocked(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto radius = 7. * u;
	const auto area = r.adjusted(2. * radius + 4. * u, 0., 0., 0.);
	auto top = r.y();
	const auto message = [&](
			const QString &name,
			const QString &text,
			const QColor &color,
			bool blocked) {
		const auto bubble = Bubble(k, area, top, {
			.text = text,
			.time = U("12:30"),
			.header = name,
			.muted = blocked,
		});
		const auto center = QPointF(
			r.x() + radius,
			bubble.y() + bubble.height() - radius);
		Avatar(k, center, radius, color, name.left(1), false);
		if (blocked) {
			const auto badge = QPointF(
				center.x() + radius * 0.7,
				center.y() + radius * 0.7);
			FillCircle(k, badge, 3.6 * u, k.c.chat);
			k.p.setPen(PenOf(k.c.attention, 1.1 * u));
			k.p.setBrush(Qt::NoBrush);
			k.p.drawEllipse(badge, 2.6 * u, 2.6 * u);
			StrokeLine(
				k,
				QPointF(badge.x() - 1.8 * u, badge.y() + 1.8 * u),
				QPointF(badge.x() + 1.8 * u, badge.y() - 1.8 * u),
				k.c.attention,
				1.1 * u);
		}
		top = bubble.y() + bubble.height() + 4. * u;
	};
	message(U("Аня"), U("Всем привет!"), QColor(0xff, 0x8a, 0xac), false);
	if (!on) {
		message(U("Спамер"), U("Купи курс!!!"), QColor(0x9a, 0x9a, 0x9a), true);
	}
	message(U("Олег"), U("Привет :)"), QColor(0x5c, 0xaf, 0xfa), false);
}

void SceneDeleteJoined(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	auto top = r.y();
	if (!on) {
		top += Row(k, r, top, 10., {
			.letter = U("В"),
			.avatar = QColor(0x8e, 0xd0, 0x6a),
			.name = U("Вася"),
			.status = U("Теперь в Telegram"),
			.statusColor = k.c.accent,
		}) + 6. * u;
	}
	top += Row(k, r, top, 10., {
		.letter = U("А"),
		.avatar = QColor(0xff, 0x8a, 0xac),
		.name = U("Аня"),
		.status = U("Встречаемся в 7?"),
		.statusColor = k.c.sub,
	}) + 6. * u;
	Row(k, r, top, 10., {
		.letter = U("О"),
		.avatar = QColor(0x5c, 0xaf, 0xfa),
		.name = U("Олег"),
		.status = U("Скинь фото"),
		.statusColor = k.c.sub,
	});
}

void SceneGlassUi(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto header = QRectF(r.x(), r.y() - 4. * u, r.width(), 18. * u);
	Bubble(k, r, r.y() + 2. * u, {
		.text = U("Под шапкой видно"),
		.time = U("12:30"),
	});
	Bubble(k, r, r.y() + 28. * u, {
		.text = U("Красиво!"),
		.time = U("12:31"),
		.out = true,
		.checks = 2,
	});
	if (on) {
		FillRound(k, header, 9. * u, Alpha(k.c.bg, 0.62));
		FillRound(
			k,
			QRectF(header.x() + 6. * u, header.y() + 1. * u, header.width() - 12. * u, 1. * u),
			0.5 * u,
			QColor(255, 255, 255, 70));
	} else {
		FillRound(k, header, 3. * u, k.c.bg);
	}
	DrawText(
		k,
		header.adjusted(8. * u, 0., -8. * u, 0.),
		U("Аня"),
		FontOf(k, 9.5, true),
		k.c.fg);
	const auto inputHeight = 15. * u;
	const auto input = on
		? QRectF(r.x() + 4. * u, r.y() + r.height() - inputHeight, r.width() - 8. * u, inputHeight)
		: QRectF(r.x() - 9. * u, r.y() + r.height() - inputHeight + 2. * u, r.width() + 18. * u, inputHeight + 6. * u);
	FillRound(
		k,
		input,
		on ? (inputHeight / 2.) : 0.,
		on ? Alpha(k.c.bg, 0.62) : k.c.bg);
	DrawText(
		k,
		QRectF(input.x() + (on ? 8. : 17.) * u, input.y(), input.width() - 20. * u, inputHeight),
		U("Сообщение"),
		FontOf(k, 9.),
		k.c.sub);
}

void SceneGlassChats(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto rowHeight = 22. * u;
	const auto rows = std::array{
		std::make_pair(U("Аня"), QColor(0xff, 0x8a, 0xac)),
		std::make_pair(U("Олег"), QColor(0x5c, 0xaf, 0xfa)),
		std::make_pair(U("Мама"), QColor(0x8e, 0xd0, 0x6a)),
	};
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto top = r.y() + i * (rowHeight + 2. * u);
		if (i == 1) {
			if (on) {
				const auto pill = QRectF(
					r.x() - 3. * u,
					top - 1. * u,
					r.width() + 6. * u,
					rowHeight + 2. * u);
				FillRound(k, pill, 8. * u, Alpha(k.c.accent, 0.22));
				FillRound(
					k,
					QRectF(pill.x() + 6. * u, pill.y() + 1. * u, pill.width() - 12. * u, 1. * u),
					0.5 * u,
					QColor(255, 255, 255, 80));
			} else {
				k.p.fillRect(
					QRectF(r.x() - 9. * u, top - 1. * u, r.width() + 18. * u, rowHeight + 2. * u),
					Alpha(k.c.accent, 0.22));
			}
		}
		Row(k, r, top + 1. * u, 10., {
			.letter = rows[i].first.left(1),
			.avatar = rows[i].second,
			.name = rows[i].first,
			.status = (i == 1) ? U("Открыт сейчас") : U("Привет!"),
			.statusColor = k.c.sub,
		});
	}
}

void SceneCompactChats(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto radius = (on ? 7. : 9.5) * u;
	const auto gap = (on ? 3. : 6.) * u;
	const auto rows = std::array{
		std::make_pair(U("Аня"), QColor(0xff, 0x8a, 0xac)),
		std::make_pair(U("Олег"), QColor(0x5c, 0xaf, 0xfa)),
		std::make_pair(U("Мама"), QColor(0x8e, 0xd0, 0x6a)),
		std::make_pair(U("Работа"), QColor(0xff, 0xb3, 0x47)),
	};
	auto top = r.y();
	for (const auto &[name, color] : rows) {
		const auto height = std::max(2. * radius, 14. * u);
		if (top + height > r.y() + r.height() + 1. * u) {
			break;
		}
		top += Row(k, r, top, radius / u, {
			.letter = name.left(1),
			.avatar = color,
			.name = name,
			.status = U("Привет!"),
			.statusColor = k.c.sub,
		}) + gap;
	}
}

void SceneDictation(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto barHeight = 22. * u;
	const auto bar = QRectF(
		r.x(),
		r.y() + r.height() - barHeight,
		r.width(),
		barHeight);
	if (on) {
		Caption(k, r, r.y(), U("Говорите — текст печатается"));
		FillRound(k, bar, barHeight / 2., k.c.inBg);
		DrawText(
			k,
			QRectF(bar.x() + 9. * u, bar.y(), bar.width() - 36. * u, barHeight),
			U("Буду в семь, жди"),
			FontOf(k, 9.5),
			k.c.inFg);
		const auto mic = QPointF(bar.x() + bar.width() - 11. * u, bar.center().y());
		FillCircle(k, mic, 12. * u, Alpha(k.c.accent, 0.25));
		FillCircle(k, mic, 9. * u, k.c.accent);
		Mic(k, mic, 12. * u, k.c.accentFg);
		return;
	}
	Caption(k, r, r.y(), U("Уходит голосовое"));
	const auto width = std::min(r.width(), 118. * u);
	const auto bubble = QRectF(
		r.x() + r.width() - width,
		r.y() + 17. * u,
		width,
		26. * u);
	FillRound(k, bubble, 9. * u, k.c.outBg);
	PlayButton(
		k,
		QPointF(bubble.x() + 13. * u, bubble.center().y()),
		8. * u,
		k.c.outSub,
		k.c.outBg);
	Waveform(
		k,
		QRectF(bubble.x() + 26. * u, bubble.y() + 8. * u, bubble.width() - 34. * u, 10. * u),
		k.c.outSub,
		14);
	FillRound(k, bar, barHeight / 2., k.c.inBg);
	const auto mic = QPointF(bar.x() + bar.width() - 11. * u, bar.center().y());
	Mic(k, mic, 12. * u, k.c.sub);
}

void SceneGlassProfile(const Canvas &k, const QRectF &r, bool on) {
	const auto u = k.s;
	const auto full = r.adjusted(-9. * u, -23. * u, 9. * u, 8. * u);
	if (on) {
		auto gradient = QLinearGradient(full.topLeft(), full.bottomLeft());
		gradient.setColorAt(0., Mix(k.c.accent, k.c.chat, 0.25));
		gradient.setColorAt(0.55, Mix(k.c.accent, k.c.chat, 0.7));
		gradient.setColorAt(1., k.c.chat);
		k.p.fillRect(full, QBrush(gradient));
	}
	const auto center = QPointF(r.center().x(), r.y() + 6. * u);
	Avatar(k, center, 10. * u, QColor(0xff, 0x8a, 0xac), U("А"), false);
	DrawText(
		k,
		QRectF(r.x(), center.y() + 11. * u, r.width(), 12. * u),
		U("Аня"),
		FontOf(k, 9.5, true),
		on ? QColor(0xff, 0xff, 0xff) : k.c.fg,
		kCenter);
	const auto buttonsTop = center.y() + 25. * u;
	const auto buttonWidth = (r.width() - 8. * u) / 3.;
	for (auto i = 0; i != 3; ++i) {
		const auto button = QRectF(
			r.x() + i * (buttonWidth + 4. * u),
			buttonsTop,
			buttonWidth,
			13. * u);
		if (on) {
			FillRound(k, button, 6.5 * u, QColor(255, 255, 255, 64));
			FillRound(
				k,
				QRectF(button.x() + 4. * u, button.y() + 0.8 * u, button.width() - 8. * u, 1. * u),
				0.5 * u,
				QColor(255, 255, 255, 110));
		} else {
			FillRound(k, button, 3. * u, Alpha(k.c.sub, 0.18));
		}
	}
	const auto cardTop = buttonsTop + 18. * u;
	const auto card = QRectF(
		on ? r.x() : (r.x() - 9. * u),
		cardTop,
		on ? r.width() : (r.width() + 18. * u),
		r.y() + r.height() - cardTop + (on ? 0. : 8. * u));
	FillRound(k, card, on ? 7. * u : 0., on ? Alpha(k.c.bg, 0.8) : k.c.bg);
	const auto lineLeft = r.x() + 6. * u;
	for (auto i = 0; i != 2; ++i) {
		const auto y = card.y() + 5. * u + i * 9. * u;
		if (y + 4. * u > card.y() + card.height()) {
			break;
		}
		FillRound(
			k,
			QRectF(lineLeft, y, (i ? 0.45 : 0.7) * (r.width() - 12. * u), 4. * u),
			2. * u,
			Alpha(k.c.sub, 0.45));
	}
}

void PaintScene(const Canvas &k, Scene scene, bool on, const QRectF &r) {''',
            },
            {
                'file': PREVIEW,
                'find': r'''	case Scene::StickersWithoutSet: SceneStickersWithoutSet(k, r, on); return;''',
                'replace': r'''	case Scene::StickersWithoutSet: SceneStickersWithoutSet(k, r, on); return;
	case Scene::MentionReply: SceneMentionReply(k, r, on); return;
	case Scene::ConfirmCalls: SceneConfirmCalls(k, r, on); return;
	case Scene::UnlimitedSearch: SceneUnlimitedSearch(k, r, on); return;
	case Scene::HideBlocked: SceneHideBlocked(k, r, on); return;
	case Scene::DeleteJoined: SceneDeleteJoined(k, r, on); return;
	case Scene::GlassUi: SceneGlassUi(k, r, on); return;
	case Scene::GlassChats: SceneGlassChats(k, r, on); return;
	case Scene::CompactChats: SceneCompactChats(k, r, on); return;
	case Scene::Dictation: SceneDictation(k, r, on); return;
	case Scene::GlassProfile: SceneGlassProfile(k, r, on); return;''',
            },
            {
                'file': PREVIEW,
                'find': r'''	case Scene::StickersWithoutSet: return U("Стикеры без ссылки на набор");''',
                'replace': r'''	case Scene::StickersWithoutSet: return U("Стикеры без ссылки на набор");
	case Scene::MentionReply: return U("Тег автора в ответе на удалённое");
	case Scene::ConfirmCalls: return U("Подтверждать звонки");
	case Scene::UnlimitedSearch: return U("Безлимитная история поиска");
	case Scene::HideBlocked: return U("Скрывать заблокированных в группах");
	case Scene::DeleteJoined: return U("Удалять «присоединился к Telegram»");
	case Scene::GlassUi: return U("Стеклянный интерфейс");
	case Scene::GlassChats: return U("Капсулы в списке чатов");
	case Scene::CompactChats: return U("Компактный список чатов");
	case Scene::Dictation: return U("Диктовка вместо голосовых");
	case Scene::GlassProfile: return U("Стеклянный профиль");''',
            },
            {
                'file': PREVIEW,
                'find': r'''	case Scene::StickersWithoutSet: return U("Статичный стикер уходит файлом: собеседник видит тот же стикер, но не может открыть ваш набор. Анимированные отправляются как обычно.");''',
                'replace': r'''	case Scene::StickersWithoutSet: return U("Статичный стикер уходит файлом: собеседник видит тот же стикер, но не может открыть ваш набор. Анимированные отправляются как обычно.");
	case Scene::MentionReply: return U("Ответ на удалённое уходит обычным сообщением, а в начале стоит @username автора.");
	case Scene::ConfirmCalls: return U("Перед звонком появляется вопрос «Позвонить?», случайное нажатие никого не потревожит.");
	case Scene::UnlimitedSearch: return U("Список недавних в поиске помнит до 1000 чатов вместо 48.");
	case Scene::HideBlocked: return U("Сообщения заблокированных не видны в группах и комментариях.");
	case Scene::DeleteJoined: return U("Сообщение «присоединился к Telegram» удаляется у вас, пустой чат не всплывает наверх.");
	case Scene::GlassUi: return U("Шапка, поле ввода, плашки и меню становятся матовым стеклом поверх сообщений.");
	case Scene::GlassChats: return U("Открытый и наведённый чат выделяются скруглённой стеклянной капсулой.");
	case Scene::CompactChats: return U("Строки в списке чатов ниже, аватарки меньше: на экран помещается больше чатов.");
	case Scene::Dictation: return U("Нажатие на микрофон открывает голосовой ввод Windows, текст печатается в поле.");
	case Scene::GlassProfile: return U("Разделы профиля как скруглённые карточки, кнопки профиля со стеклянным бликом.");''',
            },
        ],
    },
    {
        'id': 'glass-profile',
        'title': 'glass profile: section cards and action buttons',
        'pieces': [
            {
                'file': SECTION_STACK,
                'find': r'''#include "info/profile/info_profile_section_stack.h"''',
                'replace': r'''#include "info/profile/info_profile_section_stack.h"

#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': SECTION_STACK,
                'find': r'''	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	inner->add(object_ptr<Ui::BoxContentDivider>(
		inner.data(),
		st::boxDividerHeight,
		st::defaultDividerBar));
	Ui::AddSkip(inner.data(), st::infoProfileSkip);''',
                'replace': r'''	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	if (Telewhite::GlassProfileShown()) {
		Ui::AddSkip(inner.data(), st::boxDividerHeight);
	} else {
		inner->add(object_ptr<Ui::BoxContentDivider>(
			inner.data(),
			st::boxDividerHeight,
			st::defaultDividerBar));
	}
	Ui::AddSkip(inner.data(), st::infoProfileSkip);''',
            },
            {
                'file': SECTION_STACK,
                'find': r'''		case RowType::Block:
			_layout->add(std::move(row.widget));''',
                'replace': r'''		case RowType::Block:
			if (Telewhite::GlassProfileShown()) {
				_layout->add(Telewhite::WrapProfileCard(
					_layout,
					std::move(row.widget)));
			} else {
				_layout->add(std::move(row.widget));
			}''',
            },
            {
                'file': CONTENT_WIDGET,
                'find': r'''#include "styles/style_info.h"''',
                'replace': r'''#include "telewhite/telewhite_tweaks.h"
#include "styles/style_info.h"''',
            },
            {
                'file': CONTENT_WIDGET,
                'find': r'''		_bg = (value == Wrap::Layer)
			? st::boxBg
			: st::profileBg;''',
                'replace': r'''		_bg = (Telewhite::GlassProfileShown()
				&& _controller->section().type() == Section::Type::Profile)
			? st::boxDividerBg
			: (value == Wrap::Layer)
			? st::boxBg
			: st::profileBg;''',
            },
            {
                'file': ACTION_BUTTON,
                'find': r'''#include "info/profile/info_profile_top_bar_action_button.h"''',
                'replace': r'''#include "info/profile/info_profile_top_bar_action_button.h"

#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': ACTION_BUTTON,
                'find': r'''		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(rect(), st::boxRadius, st::boxRadius);
''',
                'replace': r'''		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(rect(), st::boxRadius, st::boxRadius);
		Telewhite::PaintGlassButtonShine(p, rect(), st::boxRadius);
''',
            },
        ],
    },
    {
        'id': 'dictation',
        'title': 'dictation instead of voice messages (Windows voice typing)',
        'pieces': [
            {
                'file': HISTORY_WIDGET,
                'find': r'''	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> Data::SendError {
			if (_peer) {''',
                'replace': r'''	_voiceRecordBar->setStartRecordingFilter([=] {
		if (Telewhite::StartDictation(
				_field.data(),
				_send->type() == Ui::SendButton::Type::Record)) {
			return true;
		}
		const auto error = [&]() -> Data::SendError {
			if (_peer) {''',
            },
            {
                'file': COMPOSE_CONTROLS,
                'find': r'''#include "telewhite/telewhite_send_confirm.h"''',
                'replace': r'''#include "telewhite/telewhite_send_confirm.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': COMPOSE_CONTROLS,
                'find': r'''	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> Data::SendError {
			const auto peer = _history ? _history->peer.get() : nullptr;''',
                'replace': r'''	_voiceRecordBar->setStartRecordingFilter([=] {
		if (Telewhite::StartDictation(
				_field,
				_send->type() == Ui::SendButton::Type::Record)) {
			return true;
		}
		const auto error = [&]() -> Data::SendError {
			const auto peer = _history ? _history->peer.get() : nullptr;''',
            },
        ],
    },
    {
        'id': 'settings-batch-33',
        'title': 'settings rows and previews of batch 33',
        'pieces': [
            {
                'file': SETTINGS,
                'find': r'''			Mod::MentionDeletedReply,
			Scene::SaveDeleted,
			{ u"ответ"_q, u"упоминание"_q, u"тег"_q, u"username"_q }));
		result.back().sub = true;
		result.back().preview = false;''',
                'replace': r'''			Mod::MentionDeletedReply,
			Scene::MentionReply,
			{ u"ответ"_q, u"упоминание"_q, u"тег"_q, u"username"_q }));
		result.back().sub = true;''',
            },
            {
                'file': SETTINGS,
                'find': r'''			Mod::UnlimitedSearch,
			Scene::TranslateChats,
			{ u"поиск"_q, u"история"_q, u"недавние"_q, u"search"_q }));
		result.back().preview = false;''',
                'replace': r'''			Mod::UnlimitedSearch,
			Scene::UnlimitedSearch,
			{ u"поиск"_q, u"история"_q, u"недавние"_q, u"search"_q }));''',
            },
            {
                'file': SETTINGS,
                'find': r'''		Mod::GlassUi,
		Scene::ThemedIcon,
		{ u"стекло"_q, u"стеклянный"_q, u"плашки"_q, u"glass"_q });
	glass.preview = false;''',
                'replace': r'''		Mod::GlassUi,
		Scene::GlassUi,
		{ u"стекло"_q, u"стеклянный"_q, u"плашки"_q, u"glass"_q });''',
            },
            {
                'file': SETTINGS,
                'find': r'''		Mod::CompactChats,
		Scene::ThemedIcon,''',
                'replace': r'''		Mod::CompactChats,
		Scene::CompactChats,''',
            },
            {
                'file': SETTINGS,
                'find': r'''		});
	compact.preview = false;
	compact.about''',
                'replace': r'''		});
	compact.about''',
            },
        ],
    },
]

_known = {feature['id'] for feature in telewhite_patch.FEATURES}
telewhite_patch.FEATURES.extend(
    feature for feature in FEATURES if feature['id'] not in _known)


if __name__ == '__main__':
    arguments = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    if '--root' in arguments:
        root = arguments[arguments.index('--root') + 1]
    ok = telewhite_patch.run(root, check=('--check' in arguments))
    sys.exit(0 if ok else 1)
