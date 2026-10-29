/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
struct FileChosen;
} // namespace ChatHelpers

namespace InlineBots {
struct ResultSelected;
} // namespace InlineBots

namespace Ui {
class Show;
} // namespace Ui

namespace Telewhite {

// True when a confirmation box was shown; send() runs once it is accepted.
[[nodiscard]] bool ConfirmSending(
	std::shared_ptr<Ui::Show> show,
	const ChatHelpers::FileChosen &chosen,
	Fn<void(ChatHelpers::FileChosen)> send);
[[nodiscard]] bool ConfirmSending(
	std::shared_ptr<Ui::Show> show,
	const InlineBots::ResultSelected &chosen,
	Fn<void(InlineBots::ResultSelected)> send);

[[nodiscard]] rpl::producer<ChatHelpers::FileChosen> ConfirmedSending(
	std::shared_ptr<Ui::Show> show,
	rpl::producer<ChatHelpers::FileChosen> chosen);
[[nodiscard]] rpl::producer<InlineBots::ResultSelected> ConfirmedSending(
	std::shared_ptr<Ui::Show> show,
	rpl::producer<InlineBots::ResultSelected> chosen);

} // namespace Telewhite
