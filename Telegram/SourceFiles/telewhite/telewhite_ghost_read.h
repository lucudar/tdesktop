/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Thread;
} // namespace Data

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Telewhite {

// Reports the thread as read to the server even while ghost read is on.
void SendReadReceipt(not_null<Data::Thread*> thread);

void AddSendReadReceiptAction(
	not_null<Window::SessionController*> controller,
	Data::Thread *thread,
	const Ui::Menu::MenuCallback &addAction);

} // namespace Telewhite
