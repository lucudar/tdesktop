/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_local_pins.h"

#include "apiwrap.h"
#include "base/flat_set.h"
#include "core/version.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_pinned_list.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

namespace Telewhite {
namespace {

// Version 1 had only the main list, version 2 - a list per chat folder with
// pins placed under the server ones. Version 3 keeps the local tail of the
// main list (key 0) and of the archive (key - the folder id). Version 4
// keeps the chats of the tails too: they are asked from the server right
// after the start, not when the chats list loads down to them.
constexpr auto kVersion = quint32(4);
constexpr auto kLocalPinsLimit = 1000;
constexpr auto kMaxLists = 1000;
constexpr auto kRequestsAtOnce = 50;

using Lists = base::flat_map<int, std::vector<PeerId>>;

struct State {
	Lists lists;

	// The chats of the tails as they were saved, for the not loaded ones.
	base::flat_map<PeerId, QByteArray> peers;
	quint32 peersAppVersion = 0;

	// The chats of the tails asked from the server in this session.
	base::flat_set<PeerId> requested;
	int requesting = 0;
};

[[nodiscard]] QByteArray SerializePeer(not_null<PeerData*> peer) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	Serialize::writePeer(stream, peer);
	return result;
}

[[nodiscard]] QByteArray PeerBytes(
		not_null<Main::Session*> session,
		const State &state,
		PeerId peerId) {
	if (const auto loaded = session->data().peerLoaded(peerId)) {
		if (loaded->isLoaded()) {
			return SerializePeer(loaded);
		}
	}
	const auto i = state.peers.find(peerId);
	return (i != end(state.peers)) ? i->second : QByteArray();
}

[[nodiscard]] QByteArray SerializeState(
		not_null<Main::Session*> session,
		const State &state) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kVersion
		<< quint64(session->userId().bare)
		<< quint32(AppVersion)
		<< quint32(state.lists.size());
	for (const auto &[key, order] : state.lists) {
		stream << qint32(key) << quint32(order.size());
		for (const auto &peerId : order) {
			stream
				<< SerializePeerId(peerId)
				<< PeerBytes(session, state, peerId);
		}
	}
	return result;
}

[[nodiscard]] bool ReadOrder(
		QDataStream &stream,
		std::vector<PeerId> &order,
		base::flat_map<PeerId, QByteArray> *peers) {
	auto count = quint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count > quint32(kLocalPinsLimit)) {
		return false;
	}
	order.reserve(count);
	for (auto i = quint32(); i != count; ++i) {
		auto serialized = quint64();
		auto bytes = QByteArray();
		stream >> serialized;
		if (peers) {
			stream >> bytes;
		}
		if (stream.status() != QDataStream::Ok) {
			return false;
		}
		const auto peerId = DeserializePeerId(serialized);
		if (!peerId || ranges::contains(order, peerId)) {
			continue;
		}
		order.push_back(peerId);
		if (peers && !bytes.isEmpty()) {
			peers->emplace(peerId, std::move(bytes));
		}
	}
	return true;
}

[[nodiscard]] State DeserializeState(
		not_null<Main::Session*> session,
		const QByteArray &serialized) {
	auto result = State();
	if (serialized.isEmpty()) {
		return result;
	}
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32();
	auto storedUserId = quint64();
	stream >> version >> storedUserId;
	if (stream.status() != QDataStream::Ok
		|| storedUserId != session->userId().bare) {
		return result;
	}
	if (version == 1) {
		auto order = std::vector<PeerId>();
		if (ReadOrder(stream, order, nullptr) && !order.empty()) {
			result.lists.emplace(0, std::move(order));
		}
		return result;
	} else if (version < 2 || version > kVersion) {
		return result;
	}
	if (version >= 4) {
		stream >> result.peersAppVersion;
	}
	auto lists = quint32();
	stream >> lists;
	if (stream.status() != QDataStream::Ok || lists > quint32(kMaxLists)) {
		return State();
	}
	const auto peers = (version >= 4) ? &result.peers : nullptr;
	for (auto i = quint32(); i != lists; ++i) {
		auto key = qint32();
		stream >> key;
		auto order = std::vector<PeerId>();
		if (stream.status() != QDataStream::Ok
			|| !ReadOrder(stream, order, peers)) {
			return State();
		}
		// Version 2 kept chat folders by their ids, only the main list
		// became the local tail.
		if (version == 2 && key != 0) {
			continue;
		}
		if (!order.empty()) {
			result.lists.emplace(int(key), std::move(order));
		}
	}
	return result;
}

// Never dropped with the session: pins may be asked during teardown.
[[nodiscard]] State &StateFor(not_null<Main::Session*> session) {
	static auto loaded = base::flat_map<uint64, State>();
	const auto key = session->uniqueId();
	auto i = loaded.find(key);
	if (i == end(loaded)) {
		const auto serialized = session->local().readTelewhiteData(
			Storage::TelewhiteData::LocalPins);
		i = loaded.emplace(key, DeserializeState(session, serialized)).first;
	}
	return i->second;
}

void Save(not_null<Main::Session*> session) {
	session->local().writeTelewhiteData(
		Storage::TelewhiteData::LocalPins,
		SerializeState(session, StateFor(session)),
		false);
}

[[nodiscard]] int ListKey(Data::Folder *folder) {
	return folder ? int(folder->id()) : 0;
}

[[nodiscard]] const std::vector<PeerId> *TailFor(
		not_null<Main::Session*> session,
		int key) {
	const auto &lists = StateFor(session).lists;
	const auto i = lists.find(key);
	return (i != end(lists)) ? &i->second : nullptr;
}

// Index in the remembered tail or the tail size if it's not there.
[[nodiscard]] int IndexInTail(
		const std::vector<PeerId> &tail,
		Dialogs::Key key) {
	const auto history = key.history();
	if (!history) {
		return int(tail.size());
	}
	return int(ranges::find(tail, history->peer->id) - begin(tail));
}

[[nodiscard]] bool InOrder(
		const std::vector<Dialogs::Key> &order,
		PeerId peerId) {
	return ranges::any_of(order, [&](Dialogs::Key key) {
		const auto history = key.history();
		return history && (history->peer->id == peerId);
	});
}

// Not loaded yet: the chats list doesn't know where this chat is.
[[nodiscard]] bool Unresolved(
		not_null<Data::Session*> owner,
		PeerId peerId) {
	const auto history = owner->historyLoaded(peerId);
	return !history || !history->folderKnown();
}

// The chat of the tail is in the chats list of this place without a pin.
[[nodiscard]] bool LostPin(
		not_null<History*> history,
		Data::Folder *folder) {
	const Dialogs::Entry *entry = history.get();
	return history->folderKnown()
		&& (history->folder() == folder)
		&& !history->isPinnedDialog(FilterId())
		&& entry->shouldBeInChatList();
}

void PinAt(not_null<Data::Session*> owner, not_null<History*> history) {
	const auto key = Dialogs::Key(history);
	const auto list = owner->chatsList(history->folder())->pinned();
	list->addPinnedAt(key, PinnedInsertPosition(key, list->order(), false));
}

// The chats of the tail that came to the list without a pin from the
// server get it back, except the one just unpinned.
void RepairTail(
		not_null<Data::Session*> owner,
		Data::Folder *folder,
		PeerId except) {
	const auto tail = TailFor(&owner->session(), ListKey(folder));
	if (!tail) {
		return;
	}
	const auto copy = *tail;
	for (const auto &peerId : copy) {
		if (peerId == except) {
			continue;
		} else if (const auto history = owner->historyLoaded(peerId)) {
			if (LostPin(history, folder)) {
				PinAt(owner, history);
			}
		}
	}
}

// A chat of a tail came from the server: it's pinned back in its place.
void PinBack(not_null<Data::Session*> owner, PeerId peerId) {
	const auto history = owner->historyLoaded(peerId);
	if (!history || !history->folderKnown()) {
		return;
	}
	const auto folder = history->folder();
	const auto tail = TailFor(&owner->session(), ListKey(folder));
	if (!tail
		|| !ranges::contains(*tail, peerId)
		|| !LostPin(history, folder)) {
		return;
	}
	PinAt(owner, history);
	owner->notifyPinnedDialogsOrderUpdated();
}

void SaveTail(
		not_null<Main::Session*> session,
		Data::Folder *folder,
		const std::vector<Dialogs::Key> &order,
		int serverLimit,
		PeerId unpinned) {
	const auto owner = &session->data();
	auto tail = std::vector<PeerId>();
	for (auto i = std::max(serverLimit, 0); i < int(order.size()); ++i) {
		if (const auto history = order[i].history()) {
			tail.push_back(history->peer->id);
		}
	}
	auto &lists = StateFor(session).lists;
	const auto key = ListKey(folder);
	const auto i = lists.find(key);
	if (i != end(lists)) {
		// The not loaded chats of the tail keep their places: they are
		// not in the pinned list until the server tells about them.
		auto position = 0;
		for (const auto &peerId : i->second) {
			const auto j = ranges::find(tail, peerId);
			if (j != end(tail)) {
				position = int(j - begin(tail)) + 1;
			} else if (peerId != unpinned
				&& !InOrder(order, peerId)
				&& Unresolved(owner, peerId)) {
				tail.insert(begin(tail) + position, peerId);
				++position;
			}
		}
	}
	if (int(tail.size()) > kLocalPinsLimit) {
		tail.erase(begin(tail) + kLocalPinsLimit, end(tail));
	}
	if (tail.empty()) {
		if (i == end(lists)) {
			return;
		}
		lists.remove(key);
	} else if (i != end(lists) && i->second == tail) {
		return;
	} else {
		lists[key] = std::move(tail);
	}
	Save(session);
}

// Up to kRequestsAtOnce chats of the tails not asked in this session yet.
[[nodiscard]] std::vector<PeerId> CollectUnresolved(
		not_null<Data::Session*> owner) {
	auto &state = StateFor(&owner->session());
	auto result = std::vector<PeerId>();
	for (const auto &entry : state.lists) {
		for (const auto &peerId : entry.second) {
			if (int(result.size()) >= kRequestsAtOnce) {
				return result;
			} else if (!state.requested.contains(peerId)
				&& Unresolved(owner, peerId)) {
				state.requested.emplace(peerId);
				result.push_back(peerId);
			}
		}
	}
	return result;
}

// The chat as it was saved with the tail, if it's not loaded yet.
[[nodiscard]] PeerData *ResolvePeer(
		not_null<Main::Session*> session,
		PeerId peerId) {
	if (const auto loaded = session->data().peerLoaded(peerId)) {
		if (loaded->isLoaded()) {
			return loaded;
		}
	}
	auto bytes = QByteArray();
	auto appVersion = 0;
	{
		const auto &state = StateFor(session);
		const auto i = state.peers.find(peerId);
		if (i == end(state.peers)) {
			return nullptr;
		}
		bytes = i->second;
		appVersion = int(state.peersAppVersion);
	}
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	const auto result = Serialize::readPeer(session, appVersion, stream);
	return (result && result->id == peerId) ? result : nullptr;
}

// Asks the server about the not loaded chats of the tails, a few at
// a time, each one is pinned back as soon as it comes.
void RequestUnresolved(not_null<Data::Session*> owner) {
	const auto session = &owner->session();
	if (StateFor(session).requesting > 0) {
		return;
	}
	auto histories = std::vector<not_null<History*>>();
	while (histories.empty()) {
		const auto ids = CollectUnresolved(owner);
		if (ids.empty()) {
			return;
		}
		for (const auto &peerId : ids) {
			if (const auto peer = ResolvePeer(session, peerId)) {
				histories.push_back(owner->history(peer));
			}
		}
	}
	StateFor(session).requesting = int(histories.size());
	for (const auto &history : histories) {
		const auto peerId = history->peer->id;
		owner->histories().requestDialogEntry(history, [=] {
			PinBack(owner, peerId);
			auto &state = StateFor(&owner->session());
			if (state.requesting > 0 && !--state.requesting) {
				RequestUnresolved(owner);
			}
		});
	}
}

[[nodiscard]] MTPInputDialogPeer InputDialogPeer(Dialogs::Key key) {
	if (const auto history = key.history()) {
		if (const auto channel = history->peer->asChannel()
			; channel && channel->isCommunity()) {
			return MTP_inputDialogPeerCommunity(channel->inputChannel());
		}
		return MTP_inputDialogPeer(history->peer->input());
	}
	const auto folder = key.folder();
	return MTP_inputDialogPeerFolder(MTP_int(folder ? folder->id() : 0));
}

} // namespace

int LocalPinsLimit() {
	return kLocalPinsLimit;
}

void SavePinnedTail(
		not_null<Main::Session*> session,
		Data::Folder *folder,
		const std::vector<Dialogs::Key> &order,
		int serverLimit) {
	SaveTail(session, folder, order, serverLimit, PeerId());
}

bool IsPinnedTail(Dialogs::Key key) {
	const auto history = key.history();
	if (!history || !history->folderKnown()) {
		return false;
	}
	const auto tail = TailFor(
		&history->session(),
		ListKey(history->folder()));
	return tail && (IndexInTail(*tail, key) < int(tail->size()));
}

int PinnedInsertPosition(
		Dialogs::Key key,
		const std::vector<Dialogs::Key> &order,
		bool serverPin) {
	const auto size = int(order.size());
	const auto entry = key.entry();
	if (!entry->folderKnown()) {
		return size;
	}
	const auto tail = TailFor(&entry->session(), ListKey(entry->folder()));
	if (!tail || tail->empty()) {
		return size;
	}
	const auto tailSize = int(tail->size());
	const auto mine = serverPin ? -1 : IndexInTail(*tail, key);
	for (auto i = 0; i != size; ++i) {
		const auto index = IndexInTail(*tail, order[i]);
		if (index < tailSize && index > mine) {
			return i;
		}
	}
	return size;
}

void RestorePinnedTail(
		not_null<Data::Session*> owner,
		Data::Folder *folder) {
	if (const auto tail = TailFor(&owner->session(), ListKey(folder))) {
		const auto copy = *tail;
		const auto list = owner->chatsList(folder)->pinned();
		for (const auto &peerId : copy) {
			const auto history = owner->historyLoaded(peerId);
			if (!history
				|| !history->folderKnown()
				|| history->folder() != folder) {
				continue;
			}
			const auto key = Dialogs::Key(history);
			list->addPinnedAt(
				key,
				PinnedInsertPosition(key, list->order(), false));
		}
	}
	// The rest of the tails is asked from the server.
	RequestUnresolved(owner);
}

void SyncPinned(
		not_null<Main::Session*> session,
		Data::Folder *folder,
		Dialogs::Key toggled,
		bool pinned) {
	using ToggleFlag = MTPmessages_ToggleDialogPin::Flag;
	using ReorderFlag = MTPmessages_ReorderPinnedDialogs::Flag;

	const auto owner = &session->data();
	const auto toggledHistory = toggled ? toggled.history() : nullptr;
	const auto unpinned = (toggledHistory && !pinned)
		? toggledHistory->peer->id
		: PeerId();

	// The chats of the tail that lost the pin get it back first.
	RepairTail(owner, folder, unpinned);

	const auto order = owner->pinnedChatsOrder(folder);
	const auto size = int(order.size());
	const auto serverLimit = std::max(owner->pinnedChatsLimit(folder), 0);
	const auto server = std::min(size, serverLimit);
	const auto stored = TailFor(session, ListKey(folder));
	const auto wasTail = stored ? *stored : std::vector<PeerId>();
	const auto inWasTail = [&](Dialogs::Key key) {
		return IndexInTail(wasTail, key) < int(wasTail.size());
	};
	const auto isNewPin = [&](Dialogs::Key key) {
		return pinned && toggled && (key == toggled);
	};

	// Remember the tail before any request: the server updates about
	// the chats unpinned there must not unpin them here.
	SaveTail(session, folder, order, serverLimit, unpinned);

	auto &api = session->api();
	auto last = mtpRequestId(0);
	const auto toggle = [&](Dialogs::Key key, bool pin) {
		const auto flags = pin ? ToggleFlag::f_pinned : ToggleFlag(0);
		last = api.request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			InputDialogPeer(key)
		)).afterRequest(last).send();
	};

	// First free the server slots.
	if (toggled && !pinned && !inWasTail(toggled)) {
		toggle(toggled, false);
	}
	for (auto i = server; i < size; ++i) {
		const auto key = order[i];
		if (key.history() && !inWasTail(key) && !isNewPin(key)) {
			toggle(key, false);
		}
	}
	// Then pin there the chats that came into the server part.
	for (auto i = 0; i < server; ++i) {
		const auto key = order[i];
		if (key.history() && (inWasTail(key) || isNewPin(key))) {
			toggle(key, true);
		}
	}
	// And save their order.
	auto peers = QVector<MTPInputDialogPeer>();
	peers.reserve(server);
	for (auto i = 0; i < server; ++i) {
		peers.push_back(InputDialogPeer(order[i]));
	}
	api.request(MTPmessages_ReorderPinnedDialogs(
		MTP_flags(ReorderFlag::f_force),
		MTP_int(folder ? folder->id() : 0),
		MTP_vector(peers)
	)).afterRequest(last).send();
}

int LocalPinIndex(not_null<const Dialogs::Entry*>, FilterId) {
	return 0;
}

bool IsLocallyPinned(not_null<const Dialogs::Entry*>, FilterId) {
	return false;
}

bool IsPinnedInList(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	return entry->isPinnedDialog(filterId);
}

void SetLocallyPinned(not_null<History*>, bool, FilterId) {
}

bool PinLocallyOverLimit(
		not_null<Window::SessionController*>,
		not_null<History*>,
		FilterId) {
	return false;
}

bool ReorderLocalPins(FilterId, Dialogs::Key, Dialogs::Key) {
	return false;
}

void ForgetLocalPin(not_null<Dialogs::Entry*>, FilterId) {
}

} // namespace Telewhite
