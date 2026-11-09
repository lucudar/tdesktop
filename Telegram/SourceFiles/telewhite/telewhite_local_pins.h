/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"

class History;

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

namespace Telewhite {

// Chats pinned only on this computer: over the server limit, separately for
// the main list (filter 0, also used by the archive) and each chat folder.

// 1-based position among chats pinned only on this computer, or 0.
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

// Drag-n-drop of pinned chats: local pins are reordered among themselves,
// a local pin never swaps with a server one. False for two server pins.
[[nodiscard]] bool ReorderLocalPins(
	FilterId filterId,
	Dialogs::Key key1,
	Dialogs::Key key2);


// While Telegram has room for more pinned chats, local pins of the main list
// and the archive become real pins: then they are seen on all devices.
void SetupLocalPins(not_null<Main::Session*> session);

} // namespace Telewhite
