/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class History;
class HistoryItem;

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

// Chats excluded from "Save deleted" in the settings.
[[nodiscard]] bool ExcludedChat(not_null<History*> history);

[[nodiscard]] bool KeepDeletedIn(not_null<History*> history);
[[nodiscard]] bool KeepEditsIn(not_null<History*> history);

// Message deleted on the server should stay in the chat.
[[nodiscard]] bool KeepDeleted(not_null<HistoryItem*> item);

// Message with an expired auto-delete timer should stay in the chat.
[[nodiscard]] bool KeepExpired(not_null<HistoryItem*> item);

// Own message deleted "for everyone" should stay in the chat.
[[nodiscard]] bool KeepOwnOnDelete(not_null<HistoryItem*> item, bool revoke);

// Paints the message faded with a trash can and stores it in the archive.
void KeepAsDeleted(not_null<HistoryItem*> item);

[[nodiscard]] double DeletedOpacity();

[[nodiscard]] base::flat_set<FilterId> ExcludedFolders(
	not_null<Main::Session*> session);
void SetExcludedFolders(
	not_null<Main::Session*> session,
	const base::flat_set<FilterId> &ids);

// Separate chats excluded from "Save deleted" and "Edit history".
[[nodiscard]] bool ExcludedPeer(not_null<PeerData*> peer);
void SetExcludedPeer(not_null<PeerData*> peer, bool excluded);
[[nodiscard]] std::vector<not_null<PeerData*>> ExcludedPeers(
	not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<> ExcludedPeersChanges();

// "Don't save deleted here" / "Save deleted here" in the chat menu.
void AddExcludeChatAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

// Message text with the previous versions below it, painted pale.
[[nodiscard]] TextWithEntities WithEditVersions(
	not_null<HistoryItem*> item,
	TextWithEntities text);

// Text colors with the color of the previous versions appended.
[[nodiscard]] std::span<Ui::Text::SpecialColor> WithVersionsColor(
	std::span<Ui::Text::SpecialColor> colors,
	const QColor &fg);

} // namespace Telewhite
