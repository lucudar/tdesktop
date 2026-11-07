/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Telewhite {

struct CustomText {
	ushort index = 0;
	QByteArray key;
	QString title;
	QString hint;
};

// Own texts instead of "online", "typing" and so on, on this computer only.
void SetupCustomTexts(rpl::lifetime &lifetime);

[[nodiscard]] const std::vector<CustomText> &CustomTexts();
[[nodiscard]] QString CustomTextValue(const QByteArray &key);
void SetCustomText(const QByteArray &key, const QString &value);
[[nodiscard]] int CustomTextsCount();
[[nodiscard]] rpl::producer<> CustomTextsChanges();

} // namespace Telewhite
