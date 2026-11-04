/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Telewhite {

// "Anti-caps": a message typed in CAPS LOCK is sent in normal case.
void ApplyAntiCaps(TextWithEntities &text);

// Green dot on the userpic of an online author in groups.
void PaintAuthorOnline(
	QPainter &p,
	not_null<PeerData*> from,
	int x,
	int y,
	int size);
void SetupAuthorOnline(
	not_null<Ui::RpWidget*> widget,
	not_null<Main::Session*> session,
	Fn<bool(not_null<PeerData*>)> shown);

struct StoryRingColor {
	QString name;
	QColor from;
	QColor till;
	bool theme = false;
};

// First is the default Telegram outline, second - the theme accent.
[[nodiscard]] const std::vector<StoryRingColor> &StoryRingColors();

// Changes when the outline of the stories in the chats list changes.
[[nodiscard]] int StoryRingsKey();

void SetupStoryRings(rpl::lifetime &lifetime);

} // namespace Telewhite
