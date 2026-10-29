/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_send_confirm.h"

#include "chat_helpers/tabbed_selector.h"
#include "data/data_document.h"
#include "data/stickers/data_stickers.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "telewhite/telewhite_mods.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"

namespace Telewhite {
namespace {

auto SendingConfirmed = false;

[[nodiscard]] std::optional<Mod> ConfirmMod(DocumentData *document) {
	if (!document) {
		return std::nullopt;
	} else if (const auto sticker = document->sticker()) {
		if (sticker->setType == Data::StickersType::Emoji) {
			return std::nullopt;
		}
		return Mod::ConfirmStickers;
	} else if (document->isGifv() || document->isAnimation()) {
		return Mod::ConfirmGifs;
	}
	return std::nullopt;
}

template <typename Chosen>
bool Confirm(
		std::shared_ptr<Ui::Show> show,
		const Chosen &chosen,
		DocumentData *document,
		Fn<void(Chosen)> send) {
	const auto mod = ConfirmMod(document);
	if (SendingConfirmed
		|| !mod
		|| !Enabled(*mod)
		|| chosen.options.scheduled) {
		return false;
	}
	show->show(Ui::MakeConfirmBox({
		.text = ((*mod == Mod::ConfirmStickers)
			? tr::lng_telewhite_confirm_sticker()
			: tr::lng_telewhite_confirm_gif()),
		.confirmed = [=](Fn<void()> close) {
			close();
			SendingConfirmed = true;
			send(chosen);
			SendingConfirmed = false;
		},
		.confirmText = tr::lng_send_button(),
	}));
	return true;
}

template <typename Chosen>
rpl::producer<Chosen> Confirmed(
		std::shared_ptr<Ui::Show> show,
		rpl::producer<Chosen> chosen) {
	return [=, chosen = std::move(chosen)](auto consumer) mutable {
		auto lifetime = rpl::lifetime();
		std::move(chosen) | rpl::on_next([=](Chosen data) {
			const auto send = [=](Chosen data) {
				consumer.put_next(std::move(data));
			};
			if (!ConfirmSending(show, data, send)) {
				send(std::move(data));
			}
		}, lifetime);
		return lifetime;
	};
}

} // namespace

bool ConfirmSending(
		std::shared_ptr<Ui::Show> show,
		const ChatHelpers::FileChosen &chosen,
		Fn<void(ChatHelpers::FileChosen)> send) {
	// WHY: a caption is set only by the "send with caption" box, and that
	// box is already an explicit confirmation of the send.
	if (chosen.needsCaption || !chosen.caption.text.isEmpty()) {
		return false;
	}
	return Confirm(
		std::move(show),
		chosen,
		chosen.document.get(),
		std::move(send));
}

bool ConfirmSending(
		std::shared_ptr<Ui::Show> show,
		const InlineBots::ResultSelected &chosen,
		Fn<void(InlineBots::ResultSelected)> send) {
	if (chosen.open) {
		return false;
	}
	return Confirm(
		std::move(show),
		chosen,
		chosen.result->document(),
		std::move(send));
}

rpl::producer<ChatHelpers::FileChosen> ConfirmedSending(
		std::shared_ptr<Ui::Show> show,
		rpl::producer<ChatHelpers::FileChosen> chosen) {
	return Confirmed(std::move(show), std::move(chosen));
}

rpl::producer<InlineBots::ResultSelected> ConfirmedSending(
		std::shared_ptr<Ui::Show> show,
		rpl::producer<InlineBots::ResultSelected> chosen) {
	return Confirmed(std::move(show), std::move(chosen));
}

} // namespace Telewhite
