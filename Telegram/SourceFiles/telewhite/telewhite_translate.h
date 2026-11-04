/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class TranslateProvider;
} // namespace Ui

namespace Telewhite {

// Google is used when it is chosen in the settings or when whole chats
// are translated without Telegram Premium.
[[nodiscard]] bool UseGoogleTranslate(not_null<Main::Session*> session);

[[nodiscard]] std::unique_ptr<Ui::TranslateProvider>
CreateGoogleTranslateProvider();

} // namespace Telewhite
