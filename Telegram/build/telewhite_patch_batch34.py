'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 34: the glass profile polish, see telewhite_patch.py for the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33

CONTENT = 'Telegram/SourceFiles/info/info_content_widget.cpp'
TOP_BAR = 'Telegram/SourceFiles/info/profile/info_profile_top_bar.cpp'
PILLS = 'Telegram/SourceFiles/info/profile/info_profile_music_button.cpp'

FEATURES = [
    {
        'id': 'glass-profile-page',
        'title': 'glass profile: the page darker than the cards, glass pills',
        'pieces': [
            {
                'file': CONTENT,
                'find': r'''void ContentWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_paintPadding.isNull()) {
		p.fillRect(e->rect(), _bg);
	} else {''',
                'replace': r'''void ContentWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto telewhiteGlass = Telewhite::GlassProfileShown()
		&& (_controller->section().type() == Section::Type::Profile);
	const auto bg = telewhiteGlass
		? QBrush(Telewhite::GlassProfilePage())
		: QBrush(_bg->b);
	if (_paintPadding.isNull()) {
		p.fillRect(e->rect(), bg);
	} else {''',
            },
            {
                'file': CONTENT,
                'find': r'''			std::min(0, (r.bottom() - _paintPadding.bottom())));
		p.fillRect(r + padding, _bg);''',
                'replace': r'''			std::min(0, (r.bottom() - _paintPadding.bottom())));
		p.fillRect(r + padding, bg);''',
            },
            {
                'file': TOP_BAR,
                'find': r'''#include "ui/top_background_gradient.h"''',
                'replace': r'''#include "ui/top_background_gradient.h"
#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': TOP_BAR,
                'find': r'''void TopBar::paintEdges(QPainter &p, const QRect &clip) const {
	if (!_solidBg) {
		paintEdges(p, clip, st::boxDividerBg);''',
                'replace': r'''void TopBar::paintEdges(QPainter &p, const QRect &clip) const {
	if (!_solidBg) {
		paintEdges(
			p,
			clip,
			(Telewhite::GlassProfileShown()
				? QBrush(Telewhite::GlassProfilePage())
				: QBrush(st::boxDividerBg->b)));''',
            },
            {
                'file': PILLS,
                'find': r'''#include "info/profile/info_profile_music_button.h"''',
                'replace': r'''#include "info/profile/info_profile_music_button.h"

#include "telewhite/telewhite_tweaks.h"''',
            },
            {
                'file': PILLS,
                'find': r'''	p.setBrush(colors.fill);
	p.drawRoundedRect(rect, radius, radius);
}''',
                'replace': r'''	p.setBrush(colors.fill);
	p.drawRoundedRect(rect, radius, radius);
	Telewhite::PaintGlassButtonShine(p, rect, int(radius));
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
