/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "telewhite/telewhite_glass.h"
#include "telewhite/telewhite_glass_scroll.h"
#include "mainwidget.h"
#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/palette.h"

#include <optional>

namespace Telewhite {

// The wallpaper under the glass is downscaled, blurred and stretched back,
// with the messages that go under the glass header painted over it.
inline constexpr auto kGlassDownscale = 4;
inline constexpr auto kGlassBlurRadius = 6;
inline constexpr auto kGlassBlurMargin = 24;

struct GlassSource {
	not_null<Ui::ChatTheme*> theme;
	QSize fill;
	QPoint shift;
	int fromY = 0;
	quint64 key = 0;
	const GlassContent *content = nullptr;
};

// The chats list is a section too, but it has no wallpaper.
[[nodiscard]] bool GlassSectionHasWallpaper(
	not_null<Window::AbstractSectionWidget*> section);

// The chat section that paints the wallpaper under the widget.
[[nodiscard]] inline Window::AbstractSectionWidget *GlassSection(
		not_null<QWidget*> widget) {
	if (widget->isWindow()) {
		return nullptr;
	}
	for (auto parent = widget->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		const auto section = dynamic_cast<Window::AbstractSectionWidget*>(
			parent);
		if (section) {
			return GlassSectionHasWallpaper(section) ? section : nullptr;
		} else if (parent->isWindow()) {
			break;
		}
	}
	return nullptr;
}

[[nodiscard]] inline quint64 GlassKeyMix(quint64 seed, quint64 value) {
	return seed ^ (value + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
}

// Where the wallpaper of the section lies under the widget: the point
// (x, y) of the wallpaper is painted at (x, y) - shift of the widget.
// The fill size must be the same as the section uses itself,
// otherwise the background cache of the theme is rebuilt all the time.
[[nodiscard]] inline std::optional<GlassSource> ResolveGlassSource(
		not_null<QWidget*> widget) {
	const auto section = GlassSection(widget);
	if (!section) {
		return std::nullopt;
	}
	const auto controller = section->controller();
	const auto content = controller->content();
	const auto fill = QSize(section->width(), content->height());
	if (fill.isEmpty()) {
		return std::nullopt;
	}
	const auto theme = controller->currentChatTheme();
	const auto position = widget->mapTo(section, QPoint());
	const auto fromY = content->backgroundFromY();
	const auto shift = QPoint(position.x(), position.y() - fromY);
	const auto &background = theme->background();
	auto key = quint64(reinterpret_cast<quintptr>(theme.get()));
	if (background.colorForFill) {
		key = GlassKeyMix(key, quint64(background.colorForFill->rgba()));
	} else {
		const auto &state = theme->backgroundState(fill);
		key = GlassKeyMix(key, quint64(state.now.pixmap.cacheKey()));
		key = GlassKeyMix(key, quint64(background.prepared.cacheKey()));
		if (state.shown < 1. && !background.gradientForFill.isNull()) {
			key = GlassKeyMix(key, quint64(state.was.pixmap.cacheKey()));
			key = GlassKeyMix(key, quint64(int(state.shown * 32.) + 1));
		}
	}
	// The messages under the glass header, when they are under this widget.
	const auto glass = GlassContentOf(section);
	const auto under = (glass
		&& !glass->image.isNull()
		&& glass->rect.intersects(QRect(position, widget->size())))
		? glass
		: nullptr;
	if (under) {
		key = GlassKeyMix(key, under->revision + 1);
	}
	return GlassSource{
		.theme = theme,
		.fill = fill,
		.shift = shift,
		.fromY = fromY,
		.key = key,
		.content = under,
	};
}

// The wallpaper as is, for the areas that continue the chat background.
inline void PaintGlassWallpaper(
		QPainter &p,
		const GlassSource &source,
		QRect rect) {
	p.save();
	p.setClipRect(rect, Qt::IntersectClip);
	p.translate(-source.shift);
	Window::SectionWidget::PaintBackground(
		p,
		source.theme,
		source.fill,
		rect.translated(source.shift),
		true);
	p.restore();
}

// The frosted wallpaper of the slice (in the wallpaper coordinates).
// A margin around is blurred as well, so neighbour bars match.
[[nodiscard]] inline QImage PrepareGlassBlur(
		const GlassSource &source,
		QRect slice) {
	const auto margin = style::ConvertScale(kGlassBlurMargin);
	const auto area = slice.marginsAdded(
		QMargins(margin, margin, margin, margin)
	).intersected(QRect(QPoint(), source.fill));
	if (area.isEmpty()) {
		return QImage();
	}
	const auto reduced = QSize(
		std::max(area.width() / kGlassDownscale, 1),
		std::max(area.height() / kGlassDownscale, 1));
	auto image = QImage(reduced, QImage::Format_ARGB32_Premultiplied);
	image.fill(st::windowBg->c);
	{
		auto q = QPainter(&image);
		q.setRenderHint(QPainter::SmoothPixmapTransform);
		q.scale(
			reduced.width() / float64(area.width()),
			reduced.height() / float64(area.height()));
		q.translate(-area.topLeft());
		Window::SectionWidget::PaintBackground(
			q,
			source.theme,
			source.fill,
			area,
			true);
		if (const auto under = source.content) {
			q.drawImage(
				QRectF(under->rect.translated(0, -source.fromY)),
				under->image);
		}
	}
	image = Images::BlurLargeImage(
		std::move(image),
		std::max(style::ConvertScale(kGlassBlurRadius), 1));

	const auto fx = reduced.width() / float64(area.width());
	const auto fy = reduced.height() / float64(area.height());
	const auto from = QRectF(
		(slice.x() - area.x()) * fx,
		(slice.y() - area.y()) * fy,
		slice.width() * fx,
		slice.height() * fy);
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		slice.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto q = QPainter(&result);
		q.setRenderHint(QPainter::SmoothPixmapTransform);
		q.drawImage(QRectF(QPointF(), QSizeF(slice.size())), image, from);
	}
	return result;
}

inline bool PaintGlassBlurred(
		QPainter &p,
		not_null<QWidget*> widget,
		QRect rect,
		GlassBackdropCache &cache) {
	const auto source = ResolveGlassSource(widget);
	if (!source) {
		return false;
	}
	const auto &background = source->theme->background();
	if (background.colorForFill && !source->content) {
		p.fillRect(rect, *background.colorForFill);
		return true;
	}
	const auto slice = rect.translated(source->shift);
	if (cache.image.isNull()
		|| (cache.key != source->key)
		|| (cache.slice != slice)
		|| (cache.fill != source->fill)) {
		cache.image = PrepareGlassBlur(*source, slice);
		cache.key = source->key;
		cache.slice = slice;
		cache.fill = source->fill;
	}
	if (cache.image.isNull()) {
		return false;
	}
	p.drawImage(rect.topLeft(), cache.image);
	return true;
}

// The widgets from ui/ reach the backdrop through this hook only.
inline void InstallGlassBackdrop() {
	auto &hook = GlassBackdropHook();
	if (!hook) {
		hook = [](
				QPainter &p,
				not_null<QWidget*> widget,
				QRect rect,
				GlassBackdropCache &cache) {
			return PaintGlassBlurred(p, widget, rect, cache);
		};
	}
}

} // namespace Telewhite
