/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_ghost_read.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mark_as_read.h"
#include "telewhite/telewhite_mods.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Telewhite {
namespace {

void SendHistoryReadTill(not_null<History*> history, MsgId tillId) {
	if (!tillId) {
		return;
	}
	const auto peer = history->peer;
	const auto api = &peer->session().api();
	if (const auto channel = peer->asChannel()) {
		api->request(MTPchannels_ReadHistory(
			channel->inputChannel(),
			MTP_int(tillId)
		)).send();
	} else {
		api->request(MTPmessages_ReadHistory(
			peer->input(),
			MTP_int(tillId)
		)).done([=](const MTPmessages_AffectedMessages &result) {
			api->applyAffectedMessages(peer, result);
		}).send();
	}
}

void SendHistoryRead(not_null<History*> history) {
	const auto send = [=] {
		const auto last = history->lastServerMessage();
		SendHistoryReadTill(history, last ? last->id : MsgId());
	};
	if (history->lastServerMessageKnown()) {
		send();
	} else {
		history->owner().histories().requestDialogEntry(history, send);
	}
}

void SendTopicRead(not_null<Data::ForumTopic*> topic) {
	const auto tillId = topic->lastKnownServerMessageId();
	if (!tillId) {
		return;
	}
	const auto peer = topic->peer();
	peer->session().api().request(MTPmessages_ReadDiscussion(
		peer->input(),
		MTP_int(topic->rootId()),
		MTP_int(tillId)
	)).send();
}

void SendSublistRead(not_null<Data::SavedSublist*> sublist) {
	const auto parentChat = sublist->parentChat();
	const auto tillId = sublist->lastKnownServerMessageId();
	if (!parentChat || !tillId) {
		return;
	}
	parentChat->session().api().request(MTPmessages_ReadSavedHistory(
		parentChat->input(),
		sublist->sublistPeer()->input(),
		MTP_int(tillId)
	)).send();
}

} // namespace

void SendReadReceipt(not_null<Data::Thread*> thread) {
	MarkAsReadMenu::MarkAsReadThread(thread);
	if (const auto forum = thread->asForum()) {
		forum->enumerateTopics(SendTopicRead);
	} else if (const auto history = thread->asHistory()) {
		SendHistoryRead(history);
		if (const auto migrated = history->migrateSibling()) {
			SendHistoryRead(migrated);
		}
	} else if (const auto topic = thread->asTopic()) {
		SendTopicRead(topic);
	} else if (const auto sublist = thread->asSublist()) {
		SendSublistRead(sublist);
	}
}

void AddSendReadReceiptAction(
		not_null<Window::SessionController*> controller,
		Data::Thread *thread,
		const Ui::Menu::MenuCallback &addAction) {
	if (!thread
		|| !Enabled(Mod::GhostRead)
		|| thread->peer()->isSelf()) {
		return;
	}
	const auto weak = base::make_weak(thread);
	const auto window = base::make_weak(controller);
	addAction(tr::lng_telewhite_ghost_send_read(tr::now), [=] {
		if (const auto strong = weak.get()) {
			SendReadReceipt(strong);
			if (const auto controller = window.get()) {
				controller->showToast(
					tr::lng_telewhite_ghost_read_sent(tr::now));
			}
		}
	}, &st::menuIconMarkRead);
}

} // namespace Telewhite
