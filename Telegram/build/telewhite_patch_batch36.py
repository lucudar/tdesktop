'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 36: the glass page right behind the profile cards, so the own
profile with stories (a Stories section, not a Profile one) gets it too,
and its albums divider becomes a plain gap. See telewhite_patch.py for
the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33
import telewhite_patch_batch34
import telewhite_patch_batch35

STACK = 'Telegram/SourceFiles/info/profile/info_profile_section_stack.cpp'
STORIES = 'Telegram/SourceFiles/info/stories/info_stories_inner_widget.cpp'

FEATURES = [
    {
        'id': 'glass-page-stack',
        'title': 'glass profile: the page behind the cards in any section',
        'pieces': [
            {
                'file': STACK,
                'find': r'''	_finalized = true;

	Ui::AddSkip(_layout, st::infoProfileSkip);''',
                'replace': r'''	_finalized = true;

	if (Telewhite::GlassProfileShown()) {
		// Telewhite: the glass page right behind the cards, so it is there
		// in any section, the own profile with stories is not a Profile.
		const auto host = _layout;
		host->paintRequest(
		) | rpl::on_next([=](QRect clip) {
			QPainter(host.get()).fillRect(
				clip,
				Telewhite::GlassProfilePage());
		}, host->lifetime());
	}

	Ui::AddSkip(_layout, st::infoProfileSkip);''',
            },
        ],
    },
    {
        'id': 'glass-stories-divider',
        'title': 'glass profile: the albums divider of the own profile is a plain gap',
        'pieces': [
            {
                'file': STORIES,
                'find': r'''#include "styles/style_credits.h"''',
                'replace': r'''#include "telewhite/telewhite_tweaks.h"
#include "styles/style_widgets.h"
#include "styles/style_credits.h"''',
            },
            {
                'file': STORIES,
                'find': r'''	_albumsWrap = _top->add(object_ptr<Ui::BoxContentDivider>(_top));''',
                'replace': r'''	// Telewhite: in the glass own profile the divider is a plain gap.
	static const auto telewhiteBar = [] {
		auto result = st::defaultDividerBar;
		result.bg = st::transparent;
		return result;
	}();
	_albumsWrap = (Telewhite::GlassProfileShown() && _peer->isSelf())
		? _top->add(object_ptr<Ui::BoxContentDivider>(
			_top,
			st::boxDividerHeight,
			telewhiteBar,
			RectParts()))
		: _top->add(object_ptr<Ui::BoxContentDivider>(_top));''',
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
