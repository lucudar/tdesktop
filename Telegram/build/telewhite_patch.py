'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# Build time patches of the upstream sources that are too large to keep
# as whole copies in the fork: the history widget, the compose controls,
# and the small glass changes of the top bars.
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
