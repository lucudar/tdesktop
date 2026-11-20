'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# More build time patches on top of telewhite_patch.py, with the same
# rules. Importing this module appends its features to the list there,
# so a new batch adds a small module instead of rewriting the big one.
# Telegram/configure.py imports both.
#
#   python telewhite_patch_more.py [--root <repository>] [--check]

import os
import sys

import telewhite_patch

MENU_ACTION = 'Telegram/lib_ui/ui/widgets/menu/menu_action.cpp'
MENU_MULTILINE = (
    'Telegram/lib_ui/ui/widgets/menu/menu_multiline_action.cpp')
CHAT_STYLE_RADIUS = 'Telegram/SourceFiles/ui/chat/chat_style_radius.cpp'
APPLICATION = 'Telegram/SourceFiles/core/application.cpp'
TABBED_SELECTOR = 'Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp'
TABBED_PANEL_H = 'Telegram/SourceFiles/chat_helpers/tabbed_panel.h'
TABBED_PANEL = 'Telegram/SourceFiles/chat_helpers/tabbed_panel.cpp'
GIFS_LIST = 'Telegram/SourceFiles/chat_helpers/gifs_list_widget.cpp'
CALLS_INSTANCE = 'Telegram/SourceFiles/calls/calls_instance.cpp'
VIEW_ELEMENT = 'Telegram/SourceFiles/history/view/history_view_element.cpp'
HISTORY_ITEM = 'Telegram/SourceFiles/history/history_item.cpp'
MAIN_SESSION = 'Telegram/SourceFiles/main/main_session.cpp'
DIALOGS_LAYOUT = 'Telegram/SourceFiles/dialogs/ui/dialogs_layout.cpp'

FEATURES = [
    {
        # The items read the frosted backdrop that "glass-menus" puts on
        # the menu window, the files of that feature are not touched here.
        'id': 'glass-menu-items',
        'title': 'glass menu items with their own styles, like the red ones',
        'pieces': [
            {
                'file': MENU_ACTION,
                'find': '''namespace Ui::Menu {
namespace {
''',
                'replace': '''namespace Ui::Menu {
namespace {

// Telewhite: the items in a glass menu, even with their own styles, paint
// no background, see "glass-menu-items" in telewhite_patch_more.py.
[[nodiscard]] bool TelewhiteGlassItem(not_null<const QWidget*> item) {
	const auto window = item->window();
	return window
		&& window->property("telewhiteGlassBackdrop").isValid();
}

[[nodiscard]] QColor TelewhiteGlassOver(QColor over) {
	if (over.alpha() == 255) {
		over.setAlphaF(float(over.alphaF() * 0.6));
	}
	return over;
}''',
            },
            {
                'file': MENU_ACTION,
                'find': '''void Action::paintBackground(QPainter &p, bool selected) {
	if (selected && _st.itemBgOver->c.alpha() < 255) {''',
                'replace': '''void Action::paintBackground(QPainter &p, bool selected) {
	if (TelewhiteGlassItem(this)) {
		if (selected) {
			p.fillRect(
				QRect(0, 0, width(), _height),
				TelewhiteGlassOver(_st.itemBgOver->c));
		}
		return;
	}
	if (selected && _st.itemBgOver->c.alpha() < 255) {''',
            },
            {
                'file': MENU_MULTILINE,
                'find': '''#include "ui/qt_object_factory.h"

namespace Ui::Menu {
''',
                'replace': '''#include "ui/qt_object_factory.h"

namespace Ui::Menu {
namespace {

// Telewhite: the items in a glass menu, even with their own styles, paint
// no background, see "glass-menu-items" in telewhite_patch_more.py.
[[nodiscard]] bool TelewhiteGlassItem(not_null<const QWidget*> item) {
	const auto window = item->window();
	return window
		&& window->property("telewhiteGlassBackdrop").isValid();
}

[[nodiscard]] QColor TelewhiteGlassOver(QColor over) {
	if (over.alpha() == 255) {
		over.setAlphaF(float(over.alphaF() * 0.6));
	}
	return over;
}

} // namespace
''',
            },
            {
                'file': MENU_MULTILINE,
                'find': '''p.fillRect(rect(), selected ? _st.itemBgOver : _st.itemBg);
	RippleButton::paintRipple(p, 0, 0);''',
                'replace': '''if (!TelewhiteGlassItem(this)) {
		p.fillRect(rect(), selected ? _st.itemBgOver : _st.itemBg);
	} else if (selected) {
		p.fillRect(rect(), TelewhiteGlassOver(_st.itemBgOver->c));
	}
	RippleButton::paintRipple(p, 0, 0);''',
            },
        ],
    },
    {
        # The radius is read once, so the option is applied after a restart,
        # see Option::BubbleRadius in telewhite_mods.cpp.
        'id': 'bubble-radius',
        'title': 'bubble rounding from the "Bubble rounding" option',
        'pieces': [
            {
                'file': CHAT_STYLE_RADIUS,
                'find': '''#include "styles/style_chat.h"

namespace Ui {
namespace {''',
                'replace': '''#include "styles/style_chat.h"
#include "ui/style/style_core_scale.h"

#include <QtCore/QCoreApplication>

#include <algorithm>

namespace Ui {
namespace {

// Telewhite: the "Bubble rounding" option, see Telegram/build/
// telewhite_patch_more.py. The radius in pixels at 100% scale is put
// to the application property when the settings are read, -1 if unset.
[[nodiscard]] int TelewhiteBubbleRadius() {
	static const auto result = [] {
		const auto app = QCoreApplication::instance();
		auto ok = false;
		const auto value = app
			? app->property("telewhiteBubbleRadius").toInt(&ok)
			: 0;
		return ok ? style::ConvertScale(std::clamp(value, 2, 24)) : -1;
	}();
	return result;
}''',
            },
            {
                'file': CHAT_STYLE_RADIUS,
                'find': '''int BubbleRadiusSmall() {
	return st::bubbleRadiusSmall;
}''',
                'replace': '''int BubbleRadiusSmall() {
	const auto custom = TelewhiteBubbleRadius();
	return (custom >= 0)
		? std::min(int(st::bubbleRadiusSmall), custom)
		: int(st::bubbleRadiusSmall);
}''',
            },
            {
                'file': CHAT_STYLE_RADIUS,
                'find': '''int BubbleRadiusLarge() {
	static const auto result = [] {
		if (UseSmallMsgBubbleRadius.value()) {''',
                'replace': '''int BubbleRadiusLarge() {
	static const auto result = [] {
		if (const auto custom = TelewhiteBubbleRadius(); custom >= 0) {
			return custom;
		} else if (UseSmallMsgBubbleRadius.value()) {''',
            },
            {
                'file': CHAT_STYLE_RADIUS,
                'find': '''int MsgFileThumbRadiusSmall() {
	return st::msgFileThumbRadiusSmall;
}''',
                'replace': '''int MsgFileThumbRadiusSmall() {
	const auto custom = TelewhiteBubbleRadius();
	return (custom >= 0)
		? std::min(int(st::msgFileThumbRadiusSmall), custom)
		: int(st::msgFileThumbRadiusSmall);
}''',
            },
            {
                'file': CHAT_STYLE_RADIUS,
                'find': '''int MsgFileThumbRadiusLarge() {
	static const auto result = [] {
		if (UseSmallMsgBubbleRadius.value()) {''',
                'replace': '''int MsgFileThumbRadiusLarge() {
	static const auto result = [] {
		if (const auto custom = TelewhiteBubbleRadius(); custom >= 0) {
			return std::min(int(st::msgFileThumbRadiusLarge), custom);
		} else if (UseSmallMsgBubbleRadius.value()) {''',
            },
        ],
    },
    {
        # "Compact chats list": the styles are changed right after they
        # are made, see Telewhite::ApplyStyleTweaks() in telewhite_tweaks.cpp.
        'id': 'style-tweaks',
        'title': 'compact chats list applied on the start',
        'pieces': [
            {
                'file': APPLICATION,
                'find': '''	Telewhite::SetupFakeLocation();
	Media::Player::start(_audio.get());''',
                'replace': '''	Telewhite::SetupFakeLocation();
	Telewhite::ApplyStyleTweaks();
	Media::Player::start(_audio.get());''',
            },
        ],
    },
    {
        # The emoji panel of the window with the "Glass interface" mod:
        # the panel paints the frosted wallpaper of the chat under itself,
        # the selector and its lists get a transparent style. The style is
        # chosen when the window is made, so it is applied after a restart.
        'id': 'glass-emoji-panel',
        'title': 'frosted glass emoji, stickers and GIFs panel',
        'pieces': [
            {
                'file': TABBED_SELECTOR,
                'find': '''#include "chat_helpers/gifs_list_widget.h"''',
                'replace': '''#include "chat_helpers/gifs_list_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "telewhite/telewhite_mods.h"''',
            },
            {
                'file': TABBED_SELECTOR,
                'find': '''#include "styles/style_chat_helpers.h"

namespace ChatHelpers {''',
                'replace': '''#include "styles/style_chat_helpers.h"

namespace ChatHelpers {
namespace {

// Telewhite: the glass emoji panel, see Telegram/build/
// telewhite_patch_more.py. The panel paints the frosted wallpaper
// under itself, the selector and the lists in it stay transparent.
[[nodiscard]] const style::EmojiPan &TelewhiteGlassEmojiPan() {
	static const auto result = [] {
		auto copy = st::defaultEmojiPan;
		copy.bg = st::transparent;
		copy.categoriesBg = st::transparent;
		return copy;
	}();
	return result;
}

} // namespace''',
            },
            {
                'file': TABBED_SELECTOR,
                'find': '''		: (mode == Mode::RecentReactions)
		? st::backgroundEmojiPan
		: st::defaultEmojiPan),''',
                'replace': '''		: (mode == Mode::RecentReactions)
		? st::backgroundEmojiPan
		: (mode == Mode::Full
			&& Telewhite::Enabled(Telewhite::Mod::GlassUi))
		? TelewhiteGlassEmojiPan()
		: st::defaultEmojiPan),''',
            },
            {
                'file': TABBED_SELECTOR,
                'find': '''	setAttribute(Qt::WA_OpaquePaintEvent, false);
	showAll();
	hide();
}''',
                'replace': '''	setAttribute(Qt::WA_OpaquePaintEvent, false);
	if (&_st == &TelewhiteGlassEmojiPan()) {
		setProperty("telewhiteGlass", true);
	}
	showAll();
	hide();
}''',
            },
            {
                'file': TABBED_SELECTOR,
                'find': '''void TabbedSelector::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
''',
                'replace': '''void TabbedSelector::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (property("telewhiteGlass").toBool()
		&& !dynamic_cast<TabbedPanel*>(parentWidget())) {
		// Out of the glass panel, in the third column: the usual color.
		p.fillRect(e->rect(), st::defaultEmojiPan.bg);
	}
''',
            },
            {
                'file': TABBED_PANEL_H,
                'find': '''#include "base/object_ptr.h"''',
                'replace': '''#include "base/object_ptr.h"
#include "telewhite/telewhite_glass.h"''',
            },
            {
                'file': TABBED_PANEL_H,
                'find': '''	QImage grabForAnimation();
	void startShowAnimation();''',
                'replace': '''	QImage grabForAnimation();
	void telewhitePaintGlass(QPainter &p, bool full);
	void startShowAnimation();''',
            },
            {
                'file': TABBED_PANEL_H,
                'find': '''	Ui::BoxShadow _shadow;
	QPixmap _cache;''',
                'replace': '''	Ui::BoxShadow _shadow;
	QPixmap _cache;
	Telewhite::GlassBackdropCache _telewhiteGlass;
	QImage _telewhiteGlassFrame;''',
            },
            {
                'file': TABBED_PANEL,
                'find': '''		if (!_cache.isNull()) _cache = QPixmap();
		_shadow.paint(p, innerRect(), st::emojiPanRadius);''',
                'replace': '''		if (!_cache.isNull()) _cache = QPixmap();
		_shadow.paint(p, innerRect(), st::emojiPanRadius);
		telewhitePaintGlass(p, e->rect().contains(innerRect()));''',
            },
            {
                'file': TABBED_PANEL,
                'find': '''QImage TabbedPanel::grabForAnimation() {''',
                'replace': '''// Telewhite: the glass emoji panel, see Telegram/build/
// telewhite_patch_more.py. The frosted wallpaper under the panel with
// the rounded corners, the transparent selector paints over it.
// The repaints of the lists inside reuse the last frame.
void TabbedPanel::telewhitePaintGlass(QPainter &p, bool full) {
	if (!_selector->property("telewhiteGlass").toBool()) {
		return;
	}
	const auto inner = innerRect();
	if (inner.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	if (full || _telewhiteGlassFrame.size() != inner.size() * ratio) {
		auto frame = QImage(
			inner.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		frame.setDevicePixelRatio(ratio);
		frame.fill(Qt::transparent);
		{
			auto q = QPainter(&frame);
			q.translate(-inner.x(), -inner.y());
			const auto painted = Telewhite::PaintGlassBar(
				q,
				this,
				inner,
				_telewhiteGlass,
				st::emojiPanBg->c,
				true);
			if (!painted) {
				q.fillRect(inner, st::emojiPanBg);
			}
			q.resetTransform();
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			auto hq = PainterHighQualityEnabler(q);
			q.setPen(Qt::NoPen);
			q.setBrush(Qt::white);
			const auto radius = float64(st::emojiPanRadius);
			q.drawRoundedRect(
				QRectF(0., 0., inner.width(), inner.height()),
				radius,
				radius);
		}
		_telewhiteGlassFrame = std::move(frame);
	}
	p.drawImage(inner.topLeft(), _telewhiteGlassFrame);
}

QImage TabbedPanel::grabForAnimation() {''',
            },
            {
                'file': TABBED_PANEL,
                'find': '''		QPainter p(&result);
		Ui::RenderWidget(p, _selector, _selector->pos());''',
                'replace': '''		QPainter p(&result);
		telewhitePaintGlass(p, true);
		Ui::RenderWidget(p, _selector, _selector->pos());''',
            },
            {
                'file': GIFS_LIST,
                'find': '''	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupSearch();''',
                'replace': '''	setMouseTracking(true);
	if (st().bg->c.alpha() > 0) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	setupSearch();''',
            },
        ],
    },
    {
        # "Confirm calls": the box before every new outgoing call, see
        # Telewhite::ConfirmOutgoingCall() in telewhite_tweaks.cpp.
        'id': 'confirm-calls',
        'title': 'confirmation before the outgoing calls',
        'pieces': [
            {
                'file': CALLS_INSTANCE,
                'find': '''#include "ui/boxes/confirm_box.h"''',
                'replace': '''#include "ui/boxes/confirm_box.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': CALLS_INSTANCE,
                'find': '''		StartOutgoingCallArgs args) {
	if (activateCurrentCall()
		|| (!args.isConfirmed && activateUnconfirmedCall(user))) {
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {''',
                'replace': '''		StartOutgoingCallArgs args) {
	if (activateCurrentCall()
		|| (!args.isConfirmed && activateUnconfirmedCall(user))) {
		return;
	}
	if (Telewhite::ConfirmOutgoingCall(
			user,
			args.video,
			crl::guard(this, [=] { startOutgoingCall(user, args); }))) {
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {''',
            },
        ],
    },
    {
        # "Hide blocked users": their messages in groups get no height,
        # like the hidden parts of an album.
        'id': 'hide-blocked',
        'title': 'messages of the blocked users hidden in groups',
        'pieces': [
            {
                'file': VIEW_ELEMENT,
                'find': '''#include "telewhite/telewhite_mods.h"''',
                'replace': '''#include "telewhite/telewhite_mods.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': VIEW_ELEMENT,
                'find': '''bool Element::isHidden() const {
	return isHiddenByGroup();
}''',
                'replace': '''bool Element::isHidden() const {
	return isHiddenByGroup() || Telewhite::HiddenFromBlocked(data());
}''',
            },
        ],
    },
    {
        # "Delete 'joined Telegram'": the service message is marked when it
        # is made and deleted for me when it comes as a new message, see
        # Telewhite::SetupSessionMods() in telewhite_tweaks.cpp.
        'id': 'delete-joined',
        'title': 'delete the "joined Telegram" messages and account mods',
        'pieces': [
            {
                'file': HISTORY_ITEM,
                'find': '''#include "history/history_item_helpers.h"''',
                'replace': '''#include "history/history_item_helpers.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': HISTORY_ITEM,
                'find': '''	}, [&](const MTPDmessageActionContactSignUp &) {
		_flags |= MessageFlag::IsContactSignUp;''',
                'replace': '''	}, [&](const MTPDmessageActionContactSignUp &) {
		_flags |= MessageFlag::IsContactSignUp;
		Telewhite::MarkContactSignUp(this);''',
            },
            {
                'file': MAIN_SESSION,
                'find': '''#include "telewhite/telewhite_now_playing.h"''',
                'replace': '''#include "telewhite/telewhite_now_playing.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': MAIN_SESSION,
                'find': '''	Telewhite::NowPlaying::Start(this);''',
                'replace': '''	Telewhite::NowPlaying::Start(this);
	Telewhite::SetupSessionMods(this);''',
            },
        ],
    },
    {
        # Glass chat list: the hovered and the opened chats are capsules,
        # see Telewhite::PaintGlassRow() in telewhite_tweaks.cpp.
        'id': 'glass-chats',
        'title': 'glass capsules in the chats list',
        'pieces': [
            {
                'file': DIALOGS_LAYOUT,
                'find': '''#include "telewhite/telewhite_local_pins.h"''',
                'replace': '''#include "telewhite/telewhite_local_pins.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': DIALOGS_LAYOUT,
                'find': '''	p.fillRect(geometry, bg);
	if (!(flags & Flag::TopicJumpRipple)) {
		auto ripple = context.active
			? st::dialogsRippleBgActive
			: st::dialogsRippleBg;
		row->paintRipple(p, 0, 0, context.width, &ripple->c);
	}''',
                'replace': '''	if (!Telewhite::PaintGlassRow(
			p,
			geometry,
			bg,
			context.currentBg,
			context.active || context.selected,
			context.active)) {
		p.fillRect(geometry, bg);
	}
	if (!(flags & Flag::TopicJumpRipple)) {
		auto ripple = context.active
			? st::dialogsRippleBgActive
			: st::dialogsRippleBg;
		const auto telewhiteClip = Telewhite::GlassRowClip(geometry);
		if (!telewhiteClip.isEmpty()) {
			p.save();
			p.setClipPath(telewhiteClip, Qt::IntersectClip);
		}
		row->paintRipple(p, 0, 0, context.width, &ripple->c);
		if (!telewhiteClip.isEmpty()) {
			p.restore();
		}
	}''',
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
