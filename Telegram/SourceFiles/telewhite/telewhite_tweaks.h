/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QPainterPath>

class PeerData;
class UserData;
class HistoryItem;

namespace Api {
struct SendAction;
} // namespace Api

namespace Data {
class ChatFilter;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Telewhite {

// "Anti-caps": a message typed in CAPS LOCK is sent in normal case.
void ApplyAntiCaps(TextWithEntities &text);

// Ghost mode: messages go as scheduled a few seconds ahead,
// so the server never shows the sender online.
void ApplyGhostSchedule(Api::SendAction &action);

// "Folder tags" without Premium: shown after a restart, folders
// without their own color get a color picked locally.
[[nodiscard]] bool FolderTagsShown(bool serverEnabled);
[[nodiscard]] std::optional<uint8> FolderTagColor(
	const Data::ChatFilter &filter);

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

// "Compact chats list": lower rows with smaller userpics. Called once
// right after the styles are made, before any chats list is created.
void ApplyStyleTweaks();

// "Confirm calls": true when the call waits for the confirmation box,
// then the start callback calls Calls::Instance::startOutgoingCall again.
[[nodiscard]] bool ConfirmOutgoingCall(
	not_null<UserData*> user,
	bool video,
	Fn<void()> start);

// "Hide blocked users": their messages in groups are not shown.
[[nodiscard]] bool HiddenFromBlocked(not_null<const HistoryItem*> item);

// "Delete 'joined Telegram'": marked when the service message is made.
void MarkContactSignUp(not_null<const HistoryItem*> item);

// The mods that work with the whole account: called once per session.
void SetupSessionMods(not_null<Main::Session*> session);

// Glass chat list: the hovered and the opened chats are rounded capsules.
// False when the row is painted as in Telegram.
bool PaintGlassRow(
	QPainter &p,
	QRect geometry,
	const QBrush &bg,
	const QBrush &listBg,
	bool highlighted,
	bool active);

// The capsule shape for the ripple, empty without the glass chat list.
[[nodiscard]] QPainterPath GlassRowClip(QRect geometry);

} // namespace Telewhite
