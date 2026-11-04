/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Telewhite {

// Snow over the whole window.
void SetupSnowfall(not_null<Ui::RpWidget*> parent);

// Snow over the messages only, when the window snowfall is off.
void SetupChatSnowfall(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QRect> geometry);

} // namespace Telewhite
