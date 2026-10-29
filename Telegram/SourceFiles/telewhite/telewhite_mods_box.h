/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Telewhite {

void ModsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

} // namespace Telewhite
