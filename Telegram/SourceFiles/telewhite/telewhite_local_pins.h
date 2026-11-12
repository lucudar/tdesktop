/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"

class History;

namespace Data {
class Session;
class Folder;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace Telewhite {

// Unlimited pinned chats in "All chats" and in the archive, like Ghostgram.
//
// One pinned list per place - Telegram's own, shown and reordered by the chat
// list as is. Its first pins (as many as the server allows this account) are
// synchronized with the server, the rest is a local "tail" that only this
// computer knows and never goes to the server. The tail is remembered per
// account and list, so a server list that doesn't mention those chats can't
// unpin them. Chat folders keep Telegram's behaviour.

// The local ceiling of every list, far beyond any server limit.
[[nodiscard]] int LocalPinsLimit();

// The pinned order goes to the server: pins after the first serverLimit ones
// are remembered as the local tail of this list.
void SavePinnedTail(
	not_null<Main::Session*> session,
	Data::Folder *folder,
	const std::vector<Dialogs::Key> &order,
	int serverLimit);

// A pin was toggled (toggled is empty after drag-n-drop): the first pins
// of the list are pinned on the server one by one (reordering alone does
// not pin new chats), the pins pushed out of them are unpinned there and
// stay on this computer, then the server order is saved.
void SyncPinned(
	not_null<Main::Session*> session,
	Data::Folder *folder,
	Dialogs::Key toggled,
	bool pinned);

// The chat is pinned only on this computer in its current list.
[[nodiscard]] bool IsPinnedTail(Dialogs::Key key);

// Where to put a pin: a server pin goes right before the local tail,
// a pin of the tail - by the remembered order of the tail.
[[nodiscard]] int PinnedInsertPosition(
	Dialogs::Key key,
	const std::vector<Dialogs::Key> &order,
	bool serverPin);

// The server list was applied: puts the local tail back after it.
void RestorePinnedTail(
	not_null<Data::Session*> owner,
	Data::Folder *folder);

// The API of the old separate local pins, kept for its callers. Pins over
// the server limit are regular pins of the list now, so these do nothing.
[[nodiscard]] int LocalPinIndex(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId = FilterId());
[[nodiscard]] bool IsLocallyPinned(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId = FilterId());
[[nodiscard]] bool IsPinnedInList(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId);
void SetLocallyPinned(
	not_null<History*> history,
	bool pinned,
	FilterId filterId = FilterId());
[[nodiscard]] bool PinLocallyOverLimit(
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	FilterId filterId = FilterId());
[[nodiscard]] bool ReorderLocalPins(
	FilterId filterId,
	Dialogs::Key key1,
	Dialogs::Key key2);
void ForgetLocalPin(
	not_null<Dialogs::Entry*> entry,
	FilterId filterId);

} // namespace Telewhite
