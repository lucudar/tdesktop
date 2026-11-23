'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 41: the fix of batch 40. The design studio was skipped as a whole:
its helper in empty_userpic.cpp drew the circle with the same call that
the next piece replaces, so the call was found 7 times instead of 6.
The helper draws the circle by a QRect now.
'''
import telewhite_patch
import telewhite_patch_batch40

for _feature in telewhite_patch.FEATURES:
    if _feature['id'] != 'design-studio':
        continue
    for _piece in _feature['pieces']:
        if (_piece['file'].endswith('empty_userpic.cpp')
                and 'void TelewhiteUserpicShape' in _piece['replace']):
            _piece['replace'] = _piece['replace'].replace(
                '\t\tp.drawEllipse(x, y, size, size);\n\t}\n}',
                '\t\tp.drawEllipse(QRect(x, y, size, size));\n\t}\n}')
