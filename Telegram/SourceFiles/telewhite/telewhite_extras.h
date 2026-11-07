/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Api {
struct SendAction;
} // namespace Api

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Telewhite {

// "Show results" for anonymous polls: votes for the first answer, reads
// the results and retracts the vote right away. Quizzes are skipped.
void AddPollResultsAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// A static sticker is sent as a .webp file: Telegram shows it as a sticker
// without the link to its set. Returns false if it should be sent as usual.
[[nodiscard]] bool SendStickerWithoutSet(
	not_null<DocumentData*> document,
	const Api::SendAction &action);

} // namespace Telewhite
