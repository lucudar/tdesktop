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
