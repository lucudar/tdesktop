'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 35: the glass profile cards without the side bands and the colored
header fading into the page, see telewhite_patch.py for the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33
import telewhite_patch_batch34

TWEAKS = 'Telegram/SourceFiles/telewhite/telewhite_tweaks.cpp'
TOP_BAR = 'Telegram/SourceFiles/info/profile/info_profile_top_bar.cpp'

FEATURES = [
    {
        'id': 'glass-profile-cards',
        'title': 'glass profile: cards by the visible part of the blocks, the header fades into the page',
        'pieces': [
            {
                'file': TWEAKS,
                'find': r'''	const auto height = _content->height();
	_overlay->setGeometry(margin, 0, inner, height);
	return height;''',
                'replace': r'''	// The blocks keep their shadow margins in the geometry, the card
	// covers only the visible part of them.
	const auto height = _content->heightNoMargins();
	_overlay->setGeometry(margin, 0, inner, height);
	return height;''',
            },
            {
                'file': TWEAKS,
                'find': r'''	const auto card = QRectF(_content->geometry());''',
                'replace': r'''	const auto card = QRectF(_overlay->geometry());''',
            },
            {
                'file': TOP_BAR,
                'find': r'''	if (_patternEmoji && _patternEmoji->ready()) {
		paintAnimatedPattern(p, clipBounds, geometry);
	}''',
                'replace': r'''	if (_patternEmoji && _patternEmoji->ready()) {
		paintAnimatedPattern(p, clipBounds, geometry);
	}
	if (Telewhite::GlassProfileShown()
		&& (_hasGradientBg || _solidBg.has_value())) {
		// Telewhite: the colored header melts into the glass page below,
		// the buttons and the pills are painted above as child widgets.
		const auto fade = std::min(height() / 3, style::ConvertScale(56));
		if (fade > 0) {
			const auto page = Telewhite::GlassProfilePage();
			auto clear = page;
			clear.setAlpha(0);
			auto gradient = QLinearGradient(
				QPointF(0., height() - fade),
				QPointF(0., height()));
			gradient.setColorAt(0., clear);
			gradient.setColorAt(1., page);
			p.fillRect(
				QRect(0, height() - fade, width(), fade),
				QBrush(gradient));
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
