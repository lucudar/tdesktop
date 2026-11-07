/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_extras.h"

#include "api/api_common.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "telewhite/telewhite_mods.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QFile>

namespace Telewhite {
namespace {

struct PollPeek {
	std::vector<std::pair<QString, int>> answers;
	int total = 0;
};

[[nodiscard]] PollData *PollOf(HistoryItem *item) {
	const auto media = item ? item->media() : nullptr;
	return media ? media->poll() : nullptr;
}

[[nodiscard]] bool CanPeek(not_null<HistoryItem*> item) {
	const auto poll = PollOf(item);
	return poll
		&& item->isRegular()
		&& !poll->quiz()
		&& !poll->publicVotes()
		&& !poll->closed()
		&& !poll->voted()
		&& !poll->answers.empty();
}

// Results without our own vote for the given option.
[[nodiscard]] PollPeek TakePeek(
		not_null<PollData*> poll,
		const QByteArray &option) {
	auto result = PollPeek();
	for (const auto &answer : poll->answers) {
		const auto own = (answer.option == option) ? 1 : 0;
		result.answers.emplace_back(
			answer.text.text,
			std::max(answer.votes - own, 0));
	}
	result.total = std::max(poll->totalVoters - 1, 0);
	return result;
}

void PollResultsBox(not_null<Ui::GenericBox*> box, PollPeek peek) {
	box->setTitle(tr::lng_telewhite_poll_results_title());
	auto lines = QStringList();
	for (const auto &[text, votes] : peek.answers) {
		const auto percent = (peek.total > 0)
			? int(std::round(votes * 100. / peek.total))
			: 0;
		lines.push_back(QString::number(percent)
			+ u"%  "_q
			+ text
			+ u" ("_q
			+ QString::number(votes)
			+ u")"_q);
	}
	lines.push_back(QString());
	lines.push_back(tr::lng_telewhite_poll_results_total(
		tr::now,
		lt_amount,
		QString::number(peek.total)));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(lines.join(QChar('\n'))),
		st::boxLabel));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void PeekResults(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto item = session->data().message(itemId);
	if (!item || !CanPeek(item)) {
		return;
	}
	const auto poll = PollOf(item);
	const auto peer = item->history()->peer;
	const auto msgId = item->id;
	const auto option = poll->answers.front().option;
	const auto api = &session->api();
	const auto failed = [=] {
		if (const auto strong = weak.get()) {
			strong->showToast(tr::lng_telewhite_poll_results_failed(tr::now));
		}
	};
	api->request(MTPmessages_SendVote(
		peer->input(),
		MTP_int(msgId),
		MTP_vector<MTPbytes>(QVector<MTPbytes>{ MTP_bytes(option) })
	)).done([=](const MTPUpdates &result) {
		session->updates().applyUpdates(result);
		const auto now = PollOf(session->data().message(itemId));
		const auto peek = now ? TakePeek(now, option) : PollPeek();
		// Retract the vote at once, the results are already known.
		api->request(MTPmessages_SendVote(
			peer->input(),
			MTP_int(msgId),
			MTP_vector<MTPbytes>(QVector<MTPbytes>())
		)).done([=](const MTPUpdates &retracted) {
			session->updates().applyUpdates(retracted);
			if (const auto strong = weak.get()) {
				strong->show(Box(PollResultsBox, peek));
			}
		}).fail([=] {
			failed();
		}).send();
	}).fail([=] {
		failed();
	}).send();
}

} // namespace

void AddPollResultsAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!Enabled(Mod::PollResults) || !CanPeek(item)) {
		return;
	}
	const auto weak = base::make_weak(controller);
	const auto session = &item->history()->session();
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_telewhite_poll_results(tr::now), [=] {
		PeekResults(weak, session, itemId);
	}, &st::menuIconStats);
}

bool SendStickerWithoutSet(
		not_null<DocumentData*> document,
		const Api::SendAction &action) {
	if (!Enabled(Mod::StickersWithoutSet)) {
		return false;
	}
	const auto sticker = document->sticker();
	if (!sticker
		|| !sticker->isStatic()
		|| sticker->setType == Data::StickersType::Emoji) {
		return false;
	}
	const auto media = document->createMediaView();
	auto bytes = media->bytes();
	if (bytes.isEmpty()) {
		const auto path = document->filepath(true);
		if (!path.isEmpty()) {
			auto file = QFile(path);
			if (file.open(QIODevice::ReadOnly)) {
				bytes = file.readAll();
			}
		}
	}
	if (bytes.isEmpty()) {
		return false;
	}
	document->session().api().sendFile(bytes, SendMediaType::File, action);
	return true;
}

} // namespace Telewhite
