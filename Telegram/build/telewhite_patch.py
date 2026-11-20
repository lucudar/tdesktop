'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# Build time patches of the upstream sources that are too large to keep
# as whole copies in the fork: the history widget, the compose controls,
# the list of topics and replies, the small glass changes of the top bars
# and the menus of lib_ui.
# Telegram/configure.py runs it before CMake, after the checkout.
#
# A piece is an exact fragment of the upstream code and its replacement.
# Runs of whitespace match any whitespace, so tabs, spaces and the line
# endings of the checkout do not matter. A feature is applied only when
# every piece of it is found the expected number of times, otherwise it
# is skipped as a whole and the build goes on without it. An already
# applied piece counts as found, so running the script twice is safe.
#
#   python telewhite_patch.py [--root <repository>] [--check]

import os
import re
import sys

HISTORY_WIDGET = 'Telegram/SourceFiles/history/history_widget.cpp'
HISTORY_INNER = 'Telegram/SourceFiles/history/history_inner_widget.cpp'
COMPOSE_CONTROLS = (
    'Telegram/SourceFiles/history/view/controls/'
    'history_view_compose_controls.cpp')
GROUP_CALL_BAR = 'Telegram/SourceFiles/ui/chat/group_call_bar.cpp'
REQUESTS_BAR = 'Telegram/SourceFiles/ui/chat/requests_bar.cpp'
TRANSLATE_BAR = (
    'Telegram/SourceFiles/history/view/'
    'history_view_translate_bar.cpp')
LIST_WIDGET = 'Telegram/SourceFiles/history/view/history_view_list_widget.cpp'
CHAT_SECTION = 'Telegram/SourceFiles/history/view/history_view_chat_section.cpp'
POPUP_MENU = 'Telegram/lib_ui/ui/widgets/popup_menu.cpp'

FEATURES = [
    {
        'id': 'history-glass',
        'title': 'messages under the glass header',
        'pieces': [
            {
                'file': HISTORY_WIDGET,
                'find': '#include "telewhite/telewhite_snowfall.h"',
                'replace': '#include "telewhite/telewhite_snowfall.h"\n'
                    '#include "telewhite/telewhite_glass_scroll.h"',
            },
            {
                'file': HISTORY_WIDGET,
                'find': 'Telewhite::SetupChatSnowfall(this, '
                    '_scroll->geometryValue());',
                'replace': 'Telewhite::SetupChatSnowfall(this, '
                    '_scroll->geometryValue());\n'
                    '\tTelewhite::SetupGlassScroll(this, _scroll.data());',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''if (_scroll->y() != scrollAreaTop || _scroll->x() != tabsLeftSkip) {
		_scroll->moveToLeft(tabsLeftSkip, scrollAreaTop);''',
                'replace': '''const auto glassScrollTop = Telewhite::GlassScrollTop(
		_scroll.data(),
		scrollAreaTop,
		!_subsectionTabs);
	if (_scroll->y() != glassScrollTop || _scroll->x() != tabsLeftSkip) {
		_scroll->moveToLeft(tabsLeftSkip, glassScrollTop);''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''if (_businessBotStatus) {
		newScrollHeight -= _businessBotStatus->bar().height();
	}
	if (isChoosingTheme()) {''',
                'replace': '''if (_businessBotStatus) {
		newScrollHeight -= _businessBotStatus->bar().height();
	}
	const auto glassBarsBottom = height() - newScrollHeight;
	if (isChoosingTheme()) {''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''if (newScrollHeight <= 0) {
		return;
	}
	const auto wasScrollTop = _scroll->scrollTop();''',
                'replace': '''newScrollHeight += Telewhite::GlassScrollExtra(
		glassBarsBottom,
		!_subsectionTabs);
	if (newScrollHeight <= 0) {
		return;
	}
	const auto wasScrollTop = _scroll->scrollTop();''',
            },
            {
                # The bars float over the messages, nothing to compensate.
                'file': HISTORY_WIDGET,
                'count': 5,
                'find': '_topDelta = _preserveScrollTop',
                'replace': '_topDelta = (_preserveScrollTop\n'
                    '\t\t\t|| Telewhite::GlassScrollInset(_scroll.data()) > 0)',
            },
            {
                'file': HISTORY_WIDGET,
                'count': 4,
                'find': 'setBoundings(_scroll->geometry())',
                'replace': 'setBoundings('
                    'Telewhite::GlassScrollVisible(_scroll.data()))',
            },
            {
                'file': HISTORY_WIDGET,
                'find': 'return _peer ? mapToGlobal(_scroll->geometry()) '
                    ': mapToGlobal(rect());',
                'replace': '''return _peer
		? mapToGlobal(Telewhite::GlassScrollVisible(_scroll.data()))
		: mapToGlobal(rect());''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''} else if (const auto top = unreadBarTop()) {
		return *top;''',
                'replace': '''} else if (const auto top = unreadBarTop()) {
		return *top - Telewhite::GlassScrollInset(_scroll.data());''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': 'const auto possibleUnreadBarTop = '
                    '_scroll->scrollTopMax()',
                'replace': 'const auto possibleUnreadBarTop = '
                    '_scroll->scrollTopMax()\n'
                    '\t\t\t+ Telewhite::GlassScrollInset(_scroll.data())',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''const auto visibleAreaHeight = _scroll->height();
	const auto viewHeight = view->height();
	const auto heightLeft = (visibleAreaHeight - viewHeight);
	if (heightLeft >= 0) {
		return std::max(itemTop - (heightLeft / 2), 0);
	}''',
                'replace': '''const auto glassInset = Telewhite::GlassScrollInset(_scroll.data());
	const auto visibleAreaHeight = _scroll->height() - glassInset;
	const auto viewHeight = view->height();
	const auto heightLeft = (visibleAreaHeight - viewHeight);
	if (heightLeft >= 0) {
		return std::max(itemTop - (heightLeft / 2) - glassInset, 0);
	}''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''return HistoryView::AdjustScrollForRange(
			itemTop,
			visibleAreaHeight,
			range);''',
                'replace': '''return std::max(
			HistoryView::AdjustScrollForRange(
				itemTop,
				visibleAreaHeight,
				range) - glassInset,
			0);''',
            },
            {
                'file': HISTORY_WIDGET,
                'find': '''std::min(maxAllowed, std::max(bottomResult, byReactionResult)),
			0);
	}
	return itemTop;
}''',
                'replace': '''std::min(maxAllowed, std::max(bottomResult, byReactionResult))
				- glassInset,
			0);
	}
	return std::max(itemTop - glassInset, 0);
}''',
            },
            {
                'file': HISTORY_INNER,
                'find': '#include "telewhite/telewhite_tweaks.h"',
                'replace': '#include "telewhite/telewhite_tweaks.h"\n'
                    '#include "telewhite/telewhite_glass_scroll.h"',
            },
            {
                'file': HISTORY_INNER,
                'find': '''auto oldHistoryMarginTop = std::max(
		visibleHeight - historyHeight() - _historyMarginBottom,
		0);''',
                'replace': '''auto oldHistoryMarginTop = std::max(
		visibleHeight - historyHeight() - _historyMarginBottom,
		Telewhite::GlassScrollInset(_scroll.get()));''',
            },
            {
                'file': HISTORY_INNER,
                'find': '''auto newHistoryMarginTop = std::max(
		visibleHeight - historyHeight() - _historyMarginBottom,
		0);''',
                'replace': '''auto newHistoryMarginTop = std::max(
		visibleHeight - historyHeight() - _historyMarginBottom,
		Telewhite::GlassScrollInset(_scroll.get()));''',
            },
            {
                'file': HISTORY_INNER,
                'find': '''auto newHistoryMarginTop = std::max(
		visibleHeight - itemsHeight - newHistoryMarginBottom,
		0);''',
                'replace': '''auto newHistoryMarginTop = std::max(
		visibleHeight - itemsHeight - newHistoryMarginBottom,
		Telewhite::GlassScrollInset(_scroll.get()));''',
            },
            {
                'file': HISTORY_INNER,
                'count': 4,
                'find': 'std::max(0, (_scroll->height() - _aboutView->height) / 2)',
                'replace': 'Telewhite::GlassCenteredTop('
                    '_scroll.get(), _aboutView->height)',
            },
            {
                'file': HISTORY_INNER,
                'find': '''int dateTop = std::max(itemtop - collapsed, _visibleAreaTop)
				+ st::msgServiceMargin.top();''',
                'replace': '''int dateTop = std::max(
				itemtop - collapsed,
				_visibleAreaTop + Telewhite::GlassScrollInset(_scroll.get()))
				+ st::msgServiceMargin.top();''',
            },
            {
                # Rendering the covered part is not a view of the messages.
                'file': HISTORY_INNER,
                'find': 'const auto metricsStale = '
                    'base::take(_readMetricsStale);',
                'replace': 'const auto metricsStale = '
                    '!Telewhite::GlassCapturing()\n'
                    '\t\t&& base::take(_readMetricsStale);',
            },
            {
                'file': HISTORY_INNER,
                'find': '_visibleAreaTop + skip)',
                'replace': '_visibleAreaTop\n'
                    '\t\t\t\t\t+ skip\n'
                    '\t\t\t\t\t+ Telewhite::GlassScrollInset(_scroll.get()))',
            },
        ],
    },
    {
        'id': 'compose-glass',
        'title': 'input capsule in topics, replies and scheduled',
        'pieces': [
            {
                'file': COMPOSE_CONTROLS,
                'find': '#include "telewhite/telewhite_send_confirm.h"',
                'replace': '#include "telewhite/telewhite_send_confirm.h"\n'
                    '#include "telewhite/telewhite_snowfall.h"',
            },
            {
                'file': COMPOSE_CONTROLS,
                'find': '''_wrap->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_wrap.get());
		paintBackground(p, _wrap->rect(), clip);
	}, _wrap->lifetime());''',
                'replace': '''_wrap->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_wrap.get());
		paintBackground(p, _wrap->rect(), clip);
	}, _wrap->lifetime());
	Telewhite::SetupComposeGlass(_wrap.get());''',
            },
        ],
    },
    {
        'id': 'glass-bars',
        'title': 'glass group call, requests and translate bars',
        'pieces': [
            {
                'file': GROUP_CALL_BAR,
                'find': '''#include "ui/chat/group_call_userpics.h"''',
                'replace': '''#include "ui/chat/group_call_userpics.h"
#include "telewhite/telewhite_glass.h"''',
            },
            {
                'file': GROUP_CALL_BAR,
                'find': '''_inner->resize(0, st::historyReplyHeight);
	_inner->paintRequest(
	) | rpl::on_next([=](QRect rect) {
		auto p = Painter(_inner);
		paint(p);
	}, _inner->lifetime());''',
                'replace': '''_inner->resize(0, st::historyReplyHeight);
	const auto glass = _inner->lifetime().make_state<
		Telewhite::GlassBackdropCache>();
	_inner->paintRequest(
	) | rpl::on_next([=](QRect rect) {
		auto p = Painter(_inner);
		const auto glassy = Telewhite::GlassUiEnabled()
			&& Telewhite::PaintGlassBar(
				p,
				_inner,
				_inner->rect(),
				*glass,
				st::historyComposeAreaBg->c,
				false);
		if (!glassy) {
			p.fillRect(_inner->rect(), st::historyComposeAreaBg);
		}
		paint(p);
	}, _inner->lifetime());''',
            },
            {
                'file': GROUP_CALL_BAR,
                'find': '''void GroupCallBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);''',
                'replace': '''void GroupCallBar::paint(Painter &p) {
	// The background is painted by setupInner(), glass or solid.''',
            },
            {
                'file': REQUESTS_BAR,
                'find': '''#include "ui/chat/group_call_userpics.h"''',
                'replace': '''#include "ui/chat/group_call_userpics.h"
#include "telewhite/telewhite_glass.h"''',
            },
            {
                'file': REQUESTS_BAR,
                'find': '''_inner->resize(0, st::historyRequestsHeight);
	_inner->paintRequest(
	) | rpl::on_next([=](QRect rect) {
		auto p = Painter(_inner);
		paint(p);
	}, _inner->lifetime());''',
                'replace': '''_inner->resize(0, st::historyRequestsHeight);
	const auto glass = _inner->lifetime().make_state<
		Telewhite::GlassBackdropCache>();
	_inner->paintRequest(
	) | rpl::on_next([=](QRect rect) {
		auto p = Painter(_inner);
		const auto glassy = Telewhite::GlassUiEnabled()
			&& Telewhite::PaintGlassBar(
				p,
				_inner,
				_inner->rect(),
				*glass,
				st::historyComposeAreaBg->c,
				false);
		if (!glassy) {
			p.fillRect(_inner->rect(), st::historyComposeAreaBg);
		}
		paint(p);
	}, _inner->lifetime());''',
            },
            {
                'file': REQUESTS_BAR,
                'find': '''void RequestsBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);''',
                'replace': '''void RequestsBar::paint(Painter &p) {
	// The background is painted by setupInner(), glass or solid.''',
            },
            {
                'file': TRANSLATE_BAR,
                'find': '''#include "settings/settings_credits_graphics.h" // CreditsEntryBoxStyleOverrides''',
                'replace': '''#include "settings/settings_credits_graphics.h" // CreditsEntryBoxStyleOverrides
#include "telewhite/telewhite_glass.h"''',
            },
            {
                'file': TRANSLATE_BAR,
                'find': '''button->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(button).fillRect(clip, st::historyComposeButtonBg);
	}, button->lifetime());''',
                'replace': '''const auto glass = button->lifetime().make_state<
		Telewhite::GlassBackdropCache>();
	button->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(button);
		const auto glassy = Telewhite::GlassUiEnabled()
			&& Telewhite::PaintGlassBar(
				p,
				button,
				button->rect(),
				*glass,
				st::historyComposeButtonBg->c,
				false);
		if (!glassy) {
			p.fillRect(clip, st::historyComposeButtonBg);
		}
	}, button->lifetime());''',
            },
        ],
    },
    {
        'id': 'glass-menus',
        'title': 'frosted glass context menus',
        'pieces': [
            {
                'file': POPUP_MENU,
                'find': '''#include <QtWidgets/QApplication>''',
                'replace': '''#include <QtWidgets/QApplication>
#include <QtGui/QGuiApplication>

#include <map>
#include <memory>''',
            },
            {
                'file': POPUP_MENU,
                'find': '''namespace Ui {
namespace {''',
                'replace': '''namespace Ui {
namespace {

// Telewhite: the glass menus, see Telegram/build/telewhite_patch.py.
// The items of a glass menu get a transparent background and the menu
// paints the frosted screen under it, captured right before it is shown.
constexpr auto kTelewhiteGlassMenus = "telewhiteGlassMenus";
constexpr auto kTelewhiteGlassBackdrop = "telewhiteGlassBackdrop";
constexpr auto kTelewhiteGlassTint = 0.8;
constexpr auto kTelewhiteGlassOver = 0.6;
constexpr auto kTelewhiteGlassDownscale = 4;
constexpr auto kTelewhiteGlassRadius = 6;

struct TelewhiteGlassStyle {
	explicit TelewhiteGlassStyle(const style::PopupMenu &st)
	: itemBg(QColor(0, 0, 0, 0))
	, itemBgOver(st.menu.itemBgOver->c)
	, base(st.menu.itemBg)
	, copy(st) {
		copy.menu.itemBg = itemBg.color();
		copy.menu.itemBgOver = itemBgOver.color();
	}

	style::owned_color itemBg;
	style::owned_color itemBgOver;
	style::color base;
	style::PopupMenu copy;
};

[[nodiscard]] auto &TelewhiteGlassStyles() {
	static auto result = std::map<
		const style::PopupMenu*,
		std::unique_ptr<TelewhiteGlassStyle>>();
	return result;
}

[[nodiscard]] const TelewhiteGlassStyle *TelewhiteGlassFor(
		const style::PopupMenu &st) {
	for (const auto &entry : TelewhiteGlassStyles()) {
		if (&entry.second->copy == &st) {
			return entry.second.get();
		}
	}
	return nullptr;
}

[[nodiscard]] const style::PopupMenu &TelewhiteMenuStyle(
		const style::PopupMenu &st) {
	const auto app = QCoreApplication::instance();
	if (!app
		|| !app->property(kTelewhiteGlassMenus).toBool()
		|| TelewhiteGlassFor(st)) {
		return st;
	}
	auto &entry = TelewhiteGlassStyles()[&st];
	if (!entry) {
		entry = std::make_unique<TelewhiteGlassStyle>(st);
	}
	auto over = st.menu.itemBgOver->c;
	over.setAlphaF(float(over.alphaF() * kTelewhiteGlassOver));
	entry->itemBgOver.update(over);
	return entry->copy;
}

void TelewhiteCaptureGlass(
		not_null<QWidget*> menu,
		const style::PopupMenu &st,
		QRect inner) {
	menu->setProperty(kTelewhiteGlassBackdrop, QVariant());
	if (!TelewhiteGlassFor(st) || inner.isEmpty()) {
		return;
	}
	const auto global = QRect(menu->mapToGlobal(inner.topLeft()), inner.size());
	const auto screen = QGuiApplication::screenAt(global.center());
	if (!screen) {
		return;
	}
	const auto origin = screen->geometry().topLeft();
	const auto shot = screen->grabWindow(
		0,
		global.x() - origin.x(),
		global.y() - origin.y(),
		global.width(),
		global.height());
	if (shot.isNull()) {
		return;
	}
	auto reduced = shot.toImage().scaled(
		std::max(shot.width() / kTelewhiteGlassDownscale, 1),
		std::max(shot.height() / kTelewhiteGlassDownscale, 1),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	reduced.setDevicePixelRatio(1.);
	reduced = Images::BlurLargeImage(
		std::move(reduced),
		kTelewhiteGlassRadius);
	menu->setProperty(kTelewhiteGlassBackdrop, QVariant::fromValue(reduced));
}

void TelewhitePaintGlass(
		QPainter &p,
		not_null<QWidget*> menu,
		const style::PopupMenu &st,
		QRect inner) {
	const auto glass = TelewhiteGlassFor(st);
	if (!glass || inner.isEmpty()) {
		return;
	}
	auto tint = glass->base->c;
	const auto backdrop = menu->property(
		kTelewhiteGlassBackdrop).value<QImage>();
	if (backdrop.isNull()) {
		p.fillRect(inner, tint);
		return;
	}
	const auto dark = (tint.lightnessF() < 0.5);
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(QRectF(inner), backdrop);
	tint.setAlphaF(float(tint.alphaF() * kTelewhiteGlassTint));
	p.fillRect(inner, tint);
	auto sheen = QLinearGradient(
		QPointF(inner.left(), inner.top()),
		QPointF(inner.left(), inner.top() + inner.height() * 0.45));
	sheen.setColorAt(0., QColor(255, 255, 255, dark ? 16 : 36));
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(inner, QBrush(sheen));
	p.restore();
}''',
            },
            {
                'file': POPUP_MENU,
                'count': 2,
                'find': ''', _st(st)
, _roundRect(_st.radius, _st.menu.itemBg)''',
                'replace': ''', _st(TelewhiteMenuStyle(st))
, _roundRect(_st.radius, st.menu.itemBg)''',
            },
            {
                'file': POPUP_MENU,
                'find': '''void PopupMenu::paintBg(QPainter &p) {
	if (!_useTransparency) {''',
                'replace': '''void PopupMenu::paintBg(QPainter &p) {
	TelewhitePaintGlass(p, this, _st, _inner);
	if (!_useTransparency) {''',
            },
            {
                'file': POPUP_MENU,
                'find': '''_grabbingForPanelAnimation = true;
		p.fillRect(_inner, _st.menu.itemBg);''',
                'replace': '''_grabbingForPanelAnimation = true;
		p.fillRect(_inner, _st.menu.itemBg);
		TelewhitePaintGlass(p, this, _st, _inner);''',
            },
            {
                'file': POPUP_MENU,
                'find': '''void PopupMenu::showPrepared(TriggeredSource source) {
	startShowAnimation();''',
                'replace': '''void PopupMenu::showPrepared(TriggeredSource source) {
	TelewhiteCaptureGlass(this, _st, _inner);
	startShowAnimation();''',
            },
        ],
    },
    {
        'id': 'chat-glass',
        'title': 'messages under the glass header in topics and replies',
        'pieces': [
            {
                'file': CHAT_SECTION,
                'find': '''#include "telewhite/telewhite_tweaks.h"''',
                'replace': '''#include "telewhite/telewhite_tweaks.h"
#include "telewhite/telewhite_snowfall.h"
#include "telewhite/telewhite_glass_scroll.h"''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''_scroll->setOverscrollBg(QColor(0, 0, 0, 0));''',
                'replace': '''_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	Telewhite::SetupGlassScroll(this, _scroll.get());''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''takeTopDelta() + _scrollTopDelta''',
                'replace': '''Telewhite::GlassScrollShift(
			_scroll.get(),
			takeTopDelta() + _scrollTopDelta)''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''const auto scrollHeight = bottom - top;
	const auto scrollSize = QSize(innerWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {''',
                'replace': '''const auto glassInsetWas = Telewhite::GlassScrollInset(_scroll.get());
	const auto glassTop = Telewhite::GlassScrollTop(
		_scroll.get(),
		top,
		!_subsectionTabs);
	const auto scrollHeight = bottom - glassTop;
	const auto scrollSize = QSize(innerWidth, scrollHeight);
	if (_scroll->size() != scrollSize
		|| glassInsetWas != Telewhite::GlassScrollInset(_scroll.get())) {''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''_scroll->move(tabsLeftSkip, top);''',
                'replace': '''_scroll->move(tabsLeftSkip, glassTop);''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''setAutocompleteBoundingRect(_scroll->geometry())''',
                'replace': '''setAutocompleteBoundingRect(
		Telewhite::GlassScrollVisible(_scroll.get()))''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''setBoundings(_scroll->geometry())''',
                'replace': '''setBoundings(
			Telewhite::GlassScrollVisible(_scroll.get()))''',
            },
            {
                'file': CHAT_SECTION,
                'find': '''return mapToGlobal(_scroll->geometry());''',
                'replace': '''return mapToGlobal(Telewhite::GlassScrollVisible(_scroll.get()));''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''#include "telewhite/telewhite_tweaks.h"''',
                'replace': '''#include "telewhite/telewhite_tweaks.h"
#include "telewhite/telewhite_glass_scroll.h"''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''const auto result = (_minHeight > full) ? (_minHeight - full) : 0;''',
                'replace': '''// The first message starts below the glass bars over the list.
	const auto result = std::max(
		_minHeight - full,
		Telewhite::GlassScrollInset(parentWidget()));''',
            },
            {
                'file': LIST_WIDGET,
                'count': 2,
                'find': '''std::max(0, (_minHeight - about->height) / 2)''',
                'replace': '''Telewhite::GlassCentered(
					parentWidget(),
					_minHeight,
					about->height)''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''auto dateTop = std::max(itemtop - collapsed, _visibleTop)''',
                'replace': '''auto dateTop = std::max(
				itemtop - collapsed,
				_visibleTop + Telewhite::GlassScrollInset(parentWidget()))''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''_visibleTop + skip)''',
                'replace': '''_visibleTop
					+ skip
					+ Telewhite::GlassScrollInset(parentWidget()))''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''const auto top = view->y();
	const auto height = view->height();
	const auto available = _visibleBottom - _visibleTop;
	const auto heightLeft = (available - height);
	if (heightLeft >= 0) {
		return std::max(top - (heightLeft / 2), 0);
	}
	const auto highlight = _highlighter.state(view->data());
	if (const auto range = FindHighlightYRange(view, highlight)) {
		return AdjustScrollForRange(top, available, range);
	}
	return top;''',
                'replace': '''const auto glassInset = Telewhite::GlassScrollInset(parentWidget());
	const auto top = view->y();
	const auto height = view->height();
	const auto available = _visibleBottom - _visibleTop - glassInset;
	const auto heightLeft = (available - height);
	if (heightLeft >= 0) {
		return std::max(top - (heightLeft / 2) - glassInset, 0);
	}
	const auto highlight = _highlighter.state(view->data());
	if (const auto range = FindHighlightYRange(view, highlight)) {
		return std::max(
			AdjustScrollForRange(top, available, range) - glassInset,
			0);
	}
	return std::max(top - glassInset, 0);''',
            },
            {
                'file': LIST_WIDGET,
                'find': '''const auto metricsStale = _readMetricsTracker
		&& base::take(_readMetricsStale);''',
                'replace': '''const auto metricsStale = !Telewhite::GlassCapturing()
		&& _readMetricsTracker
		&& base::take(_readMetricsStale);''',
            },
        ],
    },
]

STATE = 'out/telewhite-patched.txt'


def anchor(text):
    parts = text.split()
    return re.compile(r'\s+'.join(re.escape(part) for part in parts))


class Source:
    def __init__(self, root, path):
        self.path = path
        self.full = os.path.join(root, path)
        with open(self.full, 'rb') as f:
            raw = f.read()
        self.bom = raw.startswith(b'\xef\xbb\xbf')
        if self.bom:
            raw = raw[3:]
        self.text = raw.decode('utf-8')
        self.crlf = '\r\n' in self.text
        self.changed = False

    def save(self):
        data = self.text.encode('utf-8')
        if self.bom:
            data = b'\xef\xbb\xbf' + data
        with open(self.full, 'wb') as f:
            f.write(data)


def apply_piece(source, piece):
    count = piece.get('count', 1)
    replace = piece['replace']
    if source.crlf:
        replace = replace.replace('\r\n', '\n').replace('\n', '\r\n')
    # Checked first: some replacements contain their own fragment.
    if len(anchor(piece['replace']).findall(source.text)) >= count:
        return 'applied before'
    found = list(anchor(piece['find']).finditer(source.text))
    if len(found) != count:
        return f'found {len(found)} of {count}: {piece["find"].split()[0]}...'
    for match in reversed(found):
        source.text = (
            source.text[:match.start()]
            + replace
            + source.text[match.end():])
    source.changed = True
    return None


def apply_feature(root, feature, sources):
    working = {}
    for piece in feature['pieces']:
        path = piece['file']
        if path not in working:
            original = sources.get(path)
            if original is None:
                try:
                    original = Source(root, path)
                except OSError as error:
                    return f'{path}: {error}'
                sources[path] = original
            copy = Source.__new__(Source)
            copy.__dict__.update(original.__dict__)
            working[path] = copy
        problem = apply_piece(working[path], piece)
        if problem and problem != 'applied before':
            return f'{os.path.basename(path)}: {problem}'
    for path, copy in working.items():
        sources[path].text = copy.text
        sources[path].changed = sources[path].changed or copy.changed
    return None


def touch(root, paths):
    for path in paths:
        full = os.path.join(root, path)
        if os.path.isfile(full):
            os.utime(full, None)


def run(root, check=False):
    sources = {}
    lines = []
    applied = 0
    for feature in FEATURES:
        problem = apply_feature(root, feature, sources)
        if problem:
            lines.append(f'- {feature["id"]}: skipped, {problem}')
            print(
                f'::warning title=Telewhite patch skipped::'
                f'{feature["id"]} ({feature["title"]}): {problem}')
        else:
            applied += 1
            lines.append(f'+ {feature["id"]}: {feature["title"]}')
    patched = sorted({
        piece['file']
        for feature in FEATURES
        for piece in feature['pieces']
    })
    if not check:
        for source in sources.values():
            if source.changed:
                source.save()
        # Objects of files patched by a previous build must be rebuilt
        # even if a patch of them is skipped or removed now.
        state = os.path.join(root, STATE)
        previous = []
        if os.path.isfile(state):
            with open(state, encoding='utf-8') as f:
                previous = [line.strip() for line in f if line.strip()]
        touch(root, sorted(set(previous) | set(patched)))
        try:
            os.makedirs(os.path.dirname(state), exist_ok=True)
            with open(state, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(patched) + '\n')
        except OSError:
            pass
    summary = f'Telewhite patches: {applied} of {len(FEATURES)} applied.'
    print(summary)
    for line in lines:
        print('  ' + line)
    print('::notice title=Telewhite patches::' + '%0A'.join(
        [summary] + lines))
    return applied == len(FEATURES)


def apply_all(root):
    try:
        run(root)
    except Exception as error:
        print(f'::warning title=Telewhite patches failed::{error}')


if __name__ == '__main__':
    arguments = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    if '--root' in arguments:
        root = arguments[arguments.index('--root') + 1]
    ok = run(root, check=('--check' in arguments))
    sys.exit(0 if ok else 1)
