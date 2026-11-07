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

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class InputField;
} // namespace Ui

namespace Telewhite {

// Google is used when it is chosen in the settings or when whole chats
// are translated without Telegram Premium.
[[nodiscard]] bool UseGoogleTranslate(not_null<Main::Session*> session);

[[nodiscard]] std::unique_ptr<Ui::TranslateProvider>
CreateGoogleTranslateProvider();

// Target languages of "Translate before sending", names for the settings.
[[nodiscard]] std::vector<QString> SendLanguageNames();

// "Translate to ..." in the context menu of the message field.
void SetupTranslateBeforeSend(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::InputField*> field);

} // namespace Telewhite
