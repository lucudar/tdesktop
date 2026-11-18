/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "telewhite/telewhite_glass.h"

#include <rpl/event_stream.h>

#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <map>

namespace Telewhite {

// "Messages under the glass": the list of a chat starts at the top of the
// section and the header with the bars floats over it. The history widget
// asks these helpers from the build time patches, see
// Telegram/build/telewhite_patch.py, so its own code stays upstream.

inline constexpr auto kGlassScrollInsetProperty = "telewhiteGlassInset";

// The messages under the glass bars, rendered for their frosted backdrop.
struct GlassContent {
	QImage image;
	QRect rect; // In the coordinates of the section.
	quint64 revision = 0;
};

// Set while the covered part of a list is rendered for the backdrop:
// such a paint is not a view of the messages, the list skips its
// read metrics batch then.
[[nodiscard]] inline bool &GlassCapturingFlag() {
	static auto result = false;
	return result;
}

[[nodiscard]] inline bool GlassCapturing() {
	return GlassCapturingFlag();
}

// Fired when the covered part of a list changes, with the list scroll.
[[nodiscard]] inline auto GlassScrollInsetUpdates()
-> rpl::event_stream<not_null<QWidget*>>& {
	static auto result = rpl::event_stream<not_null<QWidget*>>();
	return result;
}

// How much of the top of the list is covered by the glass bars.
[[nodiscard]] inline int GlassScrollInset(const QWidget *scroll) {
	return scroll
		? scroll->property(kGlassScrollInsetProperty).toInt()
		: 0;
}

// The extra height of the list: the bars are over it, not above it.
[[nodiscard]] inline int GlassScrollExtra(int barsBottom, bool allowed) {
	return (allowed && GlassUiEnabled()) ? std::max(barsBottom, 0) : 0;
}

// The top of the list: the bottom of the bars without the glass and the
// top of the section with it. The covered part is remembered.
[[nodiscard]] inline int GlassScrollTop(
		QWidget *scroll,
		int barsBottom,
		bool allowed) {
	const auto inset = GlassScrollExtra(barsBottom, allowed);
	if (scroll && GlassScrollInset(scroll) != inset) {
		scroll->setProperty(kGlassScrollInsetProperty, inset);
		GlassScrollInsetUpdates().fire(not_null<QWidget*>(scroll));
	}
	return barsBottom - inset;
}

// The part of the list that is not covered by the bars.
[[nodiscard]] inline QRect GlassScrollVisible(const QWidget *scroll) {
	return scroll->geometry().marginsRemoved(
		QMargins(0, GlassScrollInset(scroll), 0, 0));
}

// The top of a block centered in the uncovered part of the list.
[[nodiscard]] inline int GlassCenteredTop(
		const QWidget *scroll,
		int blockHeight) {
	const auto inset = GlassScrollInset(scroll);
	return inset + std::max(0, (scroll->height() - inset - blockHeight) / 2);
}

// The sections with the messages under the glass, no events are sent
// to a section on its registration, it is safe in its destruction.
[[nodiscard]] inline auto GlassContents()
-> std::map<const QWidget*, GlassContent*>& {
	static auto result = std::map<const QWidget*, GlassContent*>();
	return result;
}

[[nodiscard]] inline GlassContent *GlassContentOf(const QWidget *section) {
	const auto &contents = GlassContents();
	const auto i = contents.find(section);
	return (i != end(contents)) ? i->second : nullptr;
}

} // namespace Telewhite
