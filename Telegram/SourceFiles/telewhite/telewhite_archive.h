/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Telewhite {

struct EditVersion {
	TextWithEntities text;
	TimeId date = 0;
};

struct DeletedFile {
	QString key;
	QString name;
	int64 size = 0;
	bool photo = false;
};

struct DeletedMessage {
	MsgId msgId = 0;
	TimeId date = 0;
	TimeId deletedAt = 0;
	PeerId fromId = 0;
	QString fromName;
	TextWithEntities text;
	QString media;
	DeletedFile file;
};

void RememberDeleted(not_null<HistoryItem*> item);
[[nodiscard]] bool HasDeleted(not_null<PeerData*> peer);
[[nodiscard]] std::vector<DeletedMessage> Deleted(not_null<PeerData*> peer);
void ClearDeleted(not_null<PeerData*> peer);
void ForgetDeleted(not_null<HistoryItem*> item);

// Applies the "auto-clear" period to the loaded archives.
void PruneArchives();

void RememberEditVersion(not_null<HistoryItem*> item, EditVersion version);
[[nodiscard]] std::vector<EditVersion> EditVersions(
	not_null<HistoryItem*> item);

// How many deleted / edited messages the archive of the account keeps.
[[nodiscard]] rpl::producer<int> DeletedCountValue(
	not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> EditedCountValue(
	not_null<Main::Session*> session);

// The journals of the deleted / edited messages: of all chats of the
// account with the chat type tabs or of one chat. With the search,
// "open in chat" and removing of the entries, a long press on a text
// copies it with the formatting.
void ShowDeletedJournal(
	not_null<Window::SessionController*> controller,
	PeerData *peer = nullptr);
void ShowEditedJournal(
	not_null<Window::SessionController*> controller,
	PeerData *peer = nullptr);

// "Deleted messages (N)" and "Edited messages (N)" in the chat menu.
void AddDeletedMessagesAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace Telewhite
