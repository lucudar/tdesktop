'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 38: the crash "count > 0" in random.cpp. A loading skeleton 1px
wide (a list not laid out yet, rendered for the glass backdrop with its
pending resize events) asked base::RandomIndex(0) for its last line. And
the crash report tells what was clicked last: the classes of the widget
and its parents. See telewhite_patch.py for the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33
import telewhite_patch_batch34
import telewhite_patch_batch35
import telewhite_patch_batch36
import telewhite_patch_batch37

LOADING = 'Telegram/SourceFiles/ui/effects/loading_element.cpp'
SNOWFALL = 'Telegram/SourceFiles/telewhite/telewhite_snowfall.cpp'

FEATURES = [
    {
        'id': 'skeleton-thin-width',
        'title': 'loading skeleton: no crash when it is 1px wide',
        'pieces': [
            {
                'file': LOADING,
                'find': r'''			state->lastLineWidth = (width / 4) + base::RandomIndex(width / 2);''',
                'replace': r'''			// Telewhite: a skeleton 1px wide (a list not laid out yet, when
			// the glass renders it) has no room for a random width.
			state->lastLineWidth = (width / 4)
				+ base::RandomIndex(std::max(width / 2, 1));''',
            },
        ],
    },
    {
        'id': 'crash-click-trail',
        'title': 'crash report: the classes of the widget clicked last',
        'pieces': [
            {
                'file': SNOWFALL,
                'find': r'''void SetupSnowfall(not_null<Ui::RpWidget*> parent) {''',
                'replace': r'''} // namespace Telewhite

#include "core/crash_reports.h"

#include <QtCore/QCoreApplication>
#include <typeinfo>

namespace Telewhite {
namespace {

// The crash report tells what was clicked last: the classes of the widget
// and of its parents, they are in the binary even without the symbols.
class CrashTrail final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *o, QEvent *e) override {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			_last = nullptr;
		} else if (type == QEvent::MouseButtonRelease
			&& e != _last
			&& o->isWidgetType()) {
			// The same event goes to the parents, only the first counts.
			_last = e;
			auto chain = QStringList();
			auto widget = static_cast<QWidget*>(o);
			for (auto i = 0; widget && i != 6; ++i) {
				chain.push_back(QString::fromLatin1(typeid(*widget).name()));
				widget = widget->parentWidget();
			}
			auto text = chain.join(u" < "_q);
			text.replace(u"class "_q, QString());
			text.replace(u"struct "_q, QString());
			CrashReports::SetAnnotation("Telewhite click", text);
		}
		return false;
	}

private:
	QEvent *_last = nullptr;

};

void InstallCrashTrail() {
	static auto installed = false;
	const auto app = QCoreApplication::instance();
	if (installed || !app) {
		return;
	}
	installed = true;
	app->installEventFilter(new CrashTrail(app));
}

} // namespace

void SetupSnowfall(not_null<Ui::RpWidget*> parent) {
	InstallCrashTrail();''',
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
