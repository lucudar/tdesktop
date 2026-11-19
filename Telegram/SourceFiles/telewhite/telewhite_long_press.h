/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/event_filter.h"
#include "base/timer.h"
#include "ui/layers/show.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/text_entity.h"

#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>

namespace Telewhite {

// Holding the left mouse button on the widget for a moment without moving
// it calls the callback, like a long press on a phone. Text selection and
// links of the widget keep working as before.
inline void OnLongPress(
		not_null<Ui::RpWidget*> widget,
		Fn<void()> callback) {
	constexpr auto kDuration = crl::time(450);
	constexpr auto kMoveLimit = 8;

	struct State {
		base::Timer timer;
		QPoint start;
	};
	// Owned by the filter, so it is alive while the widget gets events.
	const auto state = std::make_shared<State>();
	state->timer.setCallback(std::move(callback));
	base::install_event_filter(widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				state->start = QCursor::pos();
				state->timer.callOnce(kDuration);
			}
		} else if (type == QEvent::MouseMove) {
			const auto moved = QCursor::pos() - state->start;
			if (state->timer.isActive()
				&& (moved.manhattanLength()
					> style::ConvertScale(kMoveLimit))) {
				state->timer.cancel();
			}
		} else if (type == QEvent::MouseButtonRelease
			|| type == QEvent::MouseButtonDblClick
			|| type == QEvent::Leave) {
			state->timer.cancel();
		}
		return base::EventFilterResult::Continue;
	});
}

// Copies the text with its formatting: pasted into the message field
// it keeps the bold, the links and so on.
inline void CopyWithFormatting(
		std::shared_ptr<Ui::Show> show,
		TextWithEntities text) {
	TextUtilities::SetClipboardText(TextForMimeData::Rich(std::move(text)));
	if (show && show->valid()) {
		show->showToast(u"Скопировано с форматированием"_q);
	}
}

} // namespace Telewhite
