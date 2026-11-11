/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_local_pins.h"

#include "data/data_folder.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_pinned_list.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

namespace Telewhite {
namespace {

// Version 1 had only the main list, version 2 - a list per chat folder with
// pins placed under the server ones. Version 3 keeps the local tail of the
// main list (key 0) and of the archive (key - the folder id).
constexpr auto kVersion = quint32(3);
constexpr auto kLocalPinsLimit = 1000;
constexpr auto kMaxLists = 1000;

using Lists = base::flat_map<int, std::vector<PeerId>>;

[[nodiscard]] QByteArray Serialize(UserId userId, const Lists &lists) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kVersion << quint64(userId.bare) << quint32(lists.size());
	for (const auto &[key, order] : lists) {
		stream << qint32(key) << quint32(order.size());
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
		|| count > quint32(kLocalPinsLimit)) {
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
		if (!order.empty()) {
			result.emplace(0, std::move(order));
		}
		return result;
	} else if (version != 2 && version != kVersion) {
		return {};
	}
	auto lists = quint32();
	stream >> lists;
	if (stream.status() != QDataStream::Ok || lists > quint32(kMaxLists)) {
		return {};
	}
	for (auto i = quint32(); i != lists; ++i) {
		auto key = qint32();
		stream >> key;
		auto order = std::vector<PeerId>();
		if (stream.status() != QDataStream::Ok || !ReadOrder(stream, order)) {
			return {};
		}
		// Version 2 kept chat folders by their ids, only the main list
		// becomes the local tail now.
		if (version == 2 && key != 0) {
			continue;
		}
		if (!order.empty()) {
			result.emplace(int(key), std::move(order));
		}
	}
	return result;
}

// Never dropped with the session: pins may be asked during teardown.
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

[[nodiscard]] int ListKey(Data::Folder *folder) {
	return folder ? int(folder->id()) : 0;
}

[[nodiscard]] const std::vector<PeerId> *TailFor(
		not_null<Main::Session*> session,
		int key) {
	const auto &lists = AllLists(session);
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

} // namespace

int LocalPinsLimit() {
	return kLocalPinsLimit;
}

void SavePinnedTail(
		not_null<Main::Session*> session,
		Data::Folder *folder,
		const std::vector<Dialogs::Key> &order,
		int serverLimit) {
	auto tail = std::vector<PeerId>();
	for (auto i = std::max(serverLimit, 0); i < int(order.size()); ++i) {
		if (const auto history = order[i].history()) {
			tail.push_back(history->peer->id);
		}
	}
	auto &lists = AllLists(session);
	const auto key = ListKey(folder);
	const auto i = lists.find(key);
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
	const auto tail = TailFor(&owner->session(), ListKey(folder));
	if (!tail) {
		return;
	}
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
