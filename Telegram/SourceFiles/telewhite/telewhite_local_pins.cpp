/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_local_pins.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "storage/storage_account.h"
#include "telewhite/telewhite_mods.h"
#include "window/window_session_controller.h"

#include <QtCore/QDataStream>

namespace Telewhite {
namespace {

// Version 1 had only the main list, version 2 keeps a list per chat folder.
constexpr auto kVersion = quint32(2);
constexpr auto kMaxLocalPins = 1000;
constexpr auto kMaxLists = 1000;

using Lists = base::flat_map<FilterId, std::vector<PeerId>>;

[[nodiscard]] QByteArray Serialize(UserId userId, const Lists &lists) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kVersion << quint64(userId.bare) << quint32(lists.size());
	for (const auto &[filterId, order] : lists) {
		stream << qint32(filterId) << quint32(order.size());
		for (const auto &peerId : order) {
			stream << SerializePeerId(peerId);
		}
	}
	return result;
}

[[nodiscard]] bool ReadOrder(QDataStream &stream, std::vector<PeerId> &order) {
	auto count = quint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count > quint32(kMaxLocalPins)) {
		return false;
	}
	order.reserve(count);
	for (auto i = quint32(); i != count; ++i) {
		auto peerId = quint64();
		stream >> peerId;
		if (stream.status() != QDataStream::Ok) {
			return false;
		}
		order.push_back(DeserializePeerId(peerId));
	}
	return true;
}

[[nodiscard]] Lists Deserialize(UserId userId, const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {};
	}
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32();
	auto storedUserId = quint64();
	stream >> version >> storedUserId;
	if (stream.status() != QDataStream::Ok || storedUserId != userId.bare) {
		return {};
	}
	auto result = Lists();
	if (version == 1) {
		auto order = std::vector<PeerId>();
		if (!ReadOrder(stream, order)) {
			return {};
		}
		result.emplace(FilterId(), std::move(order));
		return result;
	} else if (version != kVersion) {
		return {};
	}
	auto lists = quint32();
	stream >> lists;
	if (stream.status() != QDataStream::Ok || lists > quint32(kMaxLists)) {
		return {};
	}
	for (auto i = quint32(); i != lists; ++i) {
		auto filterId = qint32();
		stream >> filterId;
		auto order = std::vector<PeerId>();
		if (stream.status() != QDataStream::Ok || !ReadOrder(stream, order)) {
			return {};
		}
		if (!order.empty()) {
			result.emplace(FilterId(filterId), std::move(order));
		}
	}
	return result;
}

// Never dropped with the session: sort keys may be asked during teardown.
[[nodiscard]] Lists &AllLists(not_null<Main::Session*> session) {
	static auto loaded = base::flat_map<uint64, Lists>();
	const auto key = session->uniqueId();
	auto i = loaded.find(key);
	if (i == end(loaded)) {
		const auto serialized = session->local().readTelewhiteData(
			Storage::TelewhiteData::LocalPins);
		i = loaded.emplace(
			key,
			Deserialize(session->userId(), serialized)).first;
	}
	return i->second;
}

void Save(not_null<Main::Session*> session) {
	session->local().writeTelewhiteData(
		Storage::TelewhiteData::LocalPins,
		Serialize(session->userId(), AllLists(session)),
		false);
}

[[nodiscard]] const std::vector<PeerId> *OrderFor(
		not_null<Main::Session*> session,
		FilterId filterId) {
	const auto &lists = AllLists(session);
	const auto i = lists.find(filterId);
	return (i != end(lists)) ? &i->second : nullptr;
}

void Resort(not_null<History*> history, const std::vector<PeerId> &order) {
	const auto owner = &history->owner();
	for (const auto &id : order) {
		if (const auto pinnedHistory = owner->historyLoaded(id)) {
			pinnedHistory->updateChatListSortPosition();
		}
	}
}

[[nodiscard]] auto InFlight() -> base::flat_set<std::pair<uint64, uint64>> & {
	static auto result = base::flat_set<std::pair<uint64, uint64>>();
	return result;
}

// Telegram Desktop checks the pinned limit only on the client, the server
// may still pin the chat: then it is a real pin, seen on all devices.
void ServerPin(
		not_null<History*> history,
		Fn<void()> done,
		Fn<void()> fail) {
	const auto key = std::make_pair(
		history->session().uniqueId(),
		uint64(SerializePeerId(history->peer->id)));
	if (InFlight().contains(key)) {
		return;
	}
	InFlight().emplace(key);
	const auto channel = history->peer->asChannel();
	const auto peer = (channel && channel->isCommunity())
		? MTP_inputDialogPeerCommunity(channel->inputChannel())
		: MTP_inputDialogPeer(history->peer->input());
	history->session().api().request(MTPmessages_ToggleDialogPin(
		MTP_flags(MTPmessages_ToggleDialogPin::Flag::f_pinned),
		peer
	)).done([=] {
		InFlight().remove(key);
		history->owner().setChatPinned(history, FilterId(), true);
		history->owner().notifyPinnedDialogsOrderUpdated();
		if (done) {
			done();
		}
	}).fail([=] {
		InFlight().remove(key);
		if (fail) {
			fail();
		}
	}).send();
}

} // namespace

int LocalPinIndex(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = entry->asHistory();
	if (!history) {
		return 0;
	}
	const auto order = OrderFor(&history->session(), filterId);
	if (!order) {
		return 0;
	}
	const auto i = ranges::find(*order, history->peer->id);
	return (i != end(*order)) ? int(i - begin(*order)) + 1 : 0;
}

bool IsLocallyPinned(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	return LocalPinIndex(entry, filterId) != 0;
}

bool IsPinnedInList(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	return entry->isPinnedDialog(filterId)
		|| IsLocallyPinned(entry, filterId);
}

void SetLocallyPinned(
		not_null<History*> history,
		bool pinned,
		FilterId filterId) {
	const auto session = &history->session();
	auto &lists = AllLists(session);
	auto &order = lists[filterId];
	const auto peerId = history->peer->id;
	const auto i = ranges::find(order, peerId);
	if (pinned == (i != end(order))) {
		if (order.empty()) {
			lists.remove(filterId);
		}
		return;
	} else if (pinned) {
		order.insert(begin(order), peerId);
	} else {
		order.erase(i);
	}
	const auto resort = order;
	if (order.empty()) {
		lists.remove(filterId);
	}
	Save(session);
	Resort(history, resort);
	if (!pinned) {
		history->updateChatListSortPosition();
	}
}

bool PinLocallyOverLimit(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		FilterId filterId) {
	if (!Enabled(Mod::LocalPins)) {
		return false;
	}
	const auto order = OrderFor(&history->session(), filterId);
	if (order && int(order->size()) >= kMaxLocalPins) {
		return false;
	}
	if (filterId) {
		SetLocallyPinned(history, true, filterId);
		controller->content()->dialogsToUp();
		controller->showToast(tr::lng_telewhite_local_pin_toast(tr::now));
		return true;
	}
	const auto weak = base::make_weak(controller);
	ServerPin(history, [=] {
		if (const auto strong = weak.get()) {
			strong->content()->dialogsToUp();
			strong->showToast(tr::lng_telewhite_server_pin_toast(tr::now));
		}
	}, [=] {
		SetLocallyPinned(history, true);
		if (const auto strong = weak.get()) {
			strong->content()->dialogsToUp();
			strong->showToast(tr::lng_telewhite_local_pin_toast(tr::now));
		}
	});
	return true;
}

bool ReorderLocalPins(
		FilterId filterId,
		Dialogs::Key key1,
		Dialogs::Key key2) {
	const auto first = key1.entry();
	const auto second = key2.entry();
	const auto local1 = !first->isPinnedDialog(filterId)
		&& IsLocallyPinned(first, filterId);
	const auto local2 = !second->isPinnedDialog(filterId)
		&& IsLocallyPinned(second, filterId);
	if (!local1 && !local2) {
		return false;
	} else if (!local1 || !local2) {
		return true;
	}
	const auto history1 = first->asHistory();
	const auto history2 = second->asHistory();
	if (!history1 || !history2) {
		return true;
	}
	const auto session = &history1->session();
	auto &order = AllLists(session)[filterId];
	const auto a = ranges::find(order, history1->peer->id);
	const auto b = ranges::find(order, history2->peer->id);
	if (a == end(order) || b == end(order)) {
		return true;
	}
	std::iter_swap(a, b);
	Save(session);
	Resort(history1, order);
	return true;
}

namespace {

void PromoteLocalPins(not_null<Main::Session*> session, bool force) {
	if (!Enabled(Mod::LocalPins)) {
		return;
	}
	const auto &lists = AllLists(session);
	const auto i = lists.find(FilterId());
	if (i == end(lists)) {
		return;
	}
	const auto owner = &session->data();
	const auto order = i->second; // SetLocallyPinned() changes the list.
	for (const auto &peerId : order) {
		const auto history = owner->historyLoaded(peerId);
		if (!history || !history->folderKnown()) {
			continue;
		} else if (history->isPinnedDialog(FilterId())) {
			SetLocallyPinned(history, false);
			continue;
		} else if (!force && !owner->pinnedCanPin(history)) {
			continue;
		}
		ServerPin(history, [=] {
			SetLocallyPinned(history, false);
		}, nullptr);
	}
}

} // namespace

void SetupLocalPins(not_null<Main::Session*> session) {
	const auto owner = &session->data();
	// Once the chats are loaded every local pin is tried on the server,
	// later only while Telegram itself has room for more pinned chats.
	rpl::merge(
		owner->pinnedDialogsOrderUpdated() | rpl::map([] {
			return false;
		}),
		owner->chatsListLoadedEvents(
		) | rpl::filter([](Data::Folder *folder) {
			return !folder;
		}) | rpl::map([] {
			return true;
		})
	) | rpl::on_next([=](bool force) {
		PromoteLocalPins(session, force);
	}, session->lifetime());
}

} // namespace Telewhite
