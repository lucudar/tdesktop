/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "telewhite/telewhite_archive.h"

class HistoryItem;

namespace Ui {
class GenericBox;
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Telewhite {

[[nodiscard]] std::optional<EditVersion> EditSnapshot(
	not_null<HistoryItem*> item);
void RememberEdit(
	not_null<HistoryItem*> item,
	std::optional<EditVersion> was);

// All versions of a message, the last one is its current text.
// A long press on a version copies it with the formatting.
void EditVersionsBox(
	not_null<Ui::GenericBox*> box,
	std::vector<EditVersion> versions);

void AddEditHistoryAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Telewhite
