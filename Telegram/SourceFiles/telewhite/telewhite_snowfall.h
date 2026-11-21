/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
class ElasticScroll;
} // namespace Ui

namespace Telewhite {

// Snow over the whole window.
void SetupSnowfall(not_null<Ui::RpWidget*> parent);

// Snow over the messages only, when the window snowfall is off.
void SetupChatSnowfall(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QRect> geometry);

// The messages of the chat go under the glass header and bars: the list
// is lowered under them, its covered part is their frosted backdrop.
void SetupGlassScroll(
	not_null<Ui::RpWidget*> section,
	not_null<Ui::ElasticScroll*> scroll);

// The floating input capsule for the compose controls of topics,
// replies, scheduled messages and other sections.
void SetupComposeGlass(not_null<Ui::RpWidget*> wrap);

} // namespace Telewhite
