/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_archive.h"

#include "base/base_file_utilities.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "platform/platform_file_utilities.h"
#include "settings.h"
#include "storage/storage_account.h"
#include "telewhite/telewhite_edit_history.h"
#include "telewhite/telewhite_long_press.h"
#include "telewhite/telewhite_mods.h"
#include "ui/abstract_button.h"
#include "ui/boxes/confirm_box.h"
#include "ui/click_handler.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtGui/QPainter>

namespace Telewhite {
namespace {

constexpr auto kArchiveVersion = quint32(1);
constexpr auto kMaxDeleted = 5000;
constexpr auto kMaxEdited = 3000;
constexpr auto kMaxVersions = 50;
constexpr auto kMaxPreviews = 50;
constexpr auto kJournalPage = 50;
constexpr auto kSearchDelay = crl::time(250);
constexpr auto kSaveDelay = crl::time(3000);
constexpr auto kMaxFileSize = int64(20 * 1024 * 1024);
constexpr auto kMaxFilesSize = int64(1024 * 1024 * 1024);
constexpr auto kFileCheckDelay = crl::time(1000);
constexpr auto kFileLoadTimeout = crl::time(10 * 60 * 1000);

struct PendingFile {
	PeerId peerId = 0;
	MsgId msgId = 0;
	Data::FileOrigin origin;
	QString name;
	PhotoData *photo = nullptr;
	std::shared_ptr<Data::PhotoMedia> photoView;
	DocumentData *document = nullptr;
	QString tempPath;
	crl::time started = 0;
	bool saving = false;
};

struct DeletedEntry {
	PeerId peerId = 0;
	DeletedMessage message;
};

// Previous versions of an edited message (the oldest first), who edited
// it, when, and its text after the last edit.
struct EditedEntry {
	std::vector<EditVersion> versions;
	PeerId fromId = 0;
	QString fromName;
	TimeId editedAt = 0;
	TextWithEntities current;
};

[[nodiscard]] TimeId EditedDate(const EditedEntry &entry) {
	if (entry.editedAt) {
		return entry.editedAt;
	}
	return entry.versions.empty() ? TimeId(0) : entry.versions.back().date;
}

void WriteText(QDataStream &stream, const TextWithEntities &text) {
	stream
		<< text.text
		<< TextUtilities::SerializeTags(
			TextUtilities::ConvertEntitiesToTextTags(text.entities));
}

[[nodiscard]] TextWithEntities ReadText(QDataStream &stream) {
	auto text = QString();
	auto tags = QByteArray();
	stream >> text >> tags;
	auto entities = TextUtilities::ConvertTextTagsToEntities(
		TextUtilities::DeserializeTags(tags, text.size()));
	return { std::move(text), std::move(entities) };
}

[[nodiscard]] QString RandomKey() {
	return QString::number(base::RandomValue<uint64>(), 16);
}

// The temp directory is shared by all accounts of the app.
[[nodiscard]] QString TempFolder(not_null<Main::Session*> session) {
	return session->local().tempDirectory()
		+ u"telewhite_"_q
		+ QString::number(session->userId().bare)
		+ '/';
}

[[nodiscard]] QByteArray ReadWholeFile(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] bool WriteWholeFile(
		const QString &path,
		const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return false;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size());
}

[[nodiscard]] QByteArray EncodeJpeg(const QImage &image) {
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	if (!buffer.open(QIODevice::WriteOnly)
		|| !image.save(&buffer, "JPG", 95)) {
		return QByteArray();
	}
	return result;
}

[[nodiscard]] bool Archivable(not_null<DocumentData*> document) {
	return !document->sticker()
		&& !document->forbidsFileSave()
		&& (document->size > 0)
		&& (document->size <= kMaxFileSize)
		&& (document->isVideoMessage()
			|| document->isVoiceMessage()
			|| document->isVideoFile()
			|| document->isAnimation()
			|| document->isImage());
}

[[nodiscard]] QString FileName(
		not_null<DocumentData*> document,
		TimeId date) {
	if (const auto name = document->filename(); !name.isEmpty()) {
		return base::FileNameFromUserString(name);
	}
	const auto suffix = QMimeDatabase().mimeTypeForName(
		document->mimeString()).preferredSuffix();
	const auto video = document->isVideoFile() || document->isGifv();
	const auto extension = (document->isVideoMessage() || video)
		? u".mp4"_q
		: document->isVoiceMessage()
		? u".ogg"_q
		: suffix.isEmpty()
		? QString()
		: (u"."_q + suffix);
	const auto prefix = document->isVideoMessage()
		? u"round"_q
		: document->isVoiceMessage()
		? u"audio"_q
		: (video || document->isAnimation())
		? u"video"_q
		: u"image"_q;
	return filedialogDefaultName(prefix, extension, QString(), true, date);
}

class Archive final : public base::has_weak_ptr {
public:
	explicit Archive(not_null<Main::Session*> session);

	void rememberDeleted(PeerId peerId, DeletedMessage message);
	void rememberFile(not_null<HistoryItem*> item);
	[[nodiscard]] bool hasDeleted(PeerId peerId);
	[[nodiscard]] std::vector<DeletedMessage> deleted(PeerId peerId);
	[[nodiscard]] const std::deque<DeletedEntry> &deletedList();

	// An empty peerId means all chats in these.
	[[nodiscard]] int countDeleted(PeerId peerId);
	void clearDeleted(PeerId peerId);

	void forgetDeleted(PeerId peerId, MsgId msgId);

	void rememberEditVersion(
		FullMsgId id,
		EditVersion version,
		EditedEntry info);
	[[nodiscard]] std::vector<EditVersion> editVersions(FullMsgId id);
	[[nodiscard]] auto editedList()
		-> const base::flat_map<FullMsgId, EditedEntry> &;

	// An empty peerId means all chats in these.
	[[nodiscard]] int countEdited(PeerId peerId);
	void clearEdited(PeerId peerId);

	void forgetEdited(FullMsgId id);

	[[nodiscard]] rpl::producer<> changes() const;

	// Removes everything older than the "auto-clear" period.
	void prune();

	void flush();

private:
	void load();
	void cleanFiles();
	void changed();
	void save(bool sync);
	void refreshEdited(FullMsgId id);
	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] bool deserialize(const QByteArray &serialized);

	void checkPending();
	[[nodiscard]] bool checkPending(PendingFile &pending, crl::time now);
	void storeFile(
		const PendingFile &pending,
		bool photo,
		Fn<QByteArray()> produce);
	void fileStored(PeerId peerId, MsgId msgId, DeletedFile file);
	void limitFiles();
	void removeFiles(std::vector<QString> keys);

	const not_null<Main::Session*> _session;
	base::Timer _saveTimer;
	base::Timer _pendingTimer;
	std::deque<DeletedEntry> _deleted;
	base::flat_map<FullMsgId, EditedEntry> _edits;
	std::vector<PendingFile> _pending;
	rpl::event_stream<> _changes;
	bool _loaded = false;
	bool _dirty = false;

};

Archive::Archive(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(false); })
, _pendingTimer([=] { checkPending(); }) {
}

void Archive::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto serialized = _session->local().readTelewhiteData(
		Storage::TelewhiteData::Archive);
	if (!serialized.isEmpty() && !deserialize(serialized)) {
		_deleted.clear();
		_edits.clear();
	}
	prune();
	cleanFiles();
}

void Archive::prune() {
	if (!_loaded) {
		return;
	}
	const auto period = AutoClearSeconds(
		AutoClearPeriod(OptionNow(Option::AutoClear)));
	if (!period) {
		return;
	}
	const auto border = base::unixtime::now() - period;
	auto removed = std::vector<QString>();
	const auto wasDeleted = _deleted.size();
	for (const auto &entry : _deleted) {
		const auto &message = entry.message;
		if (message.deletedAt < border && !message.file.key.isEmpty()) {
			removed.push_back(message.file.key);
		}
	}
	_deleted.erase(ranges::remove_if(_deleted, [&](const DeletedEntry &e) {
		return (e.message.deletedAt < border);
	}), end(_deleted));
	const auto wasEdits = _edits.size();
	for (auto i = begin(_edits); i != end(_edits);) {
		if (i->second.versions.empty() || EditedDate(i->second) < border) {
			i = _edits.erase(i);
		} else {
			++i;
		}
	}
	removeFiles(std::move(removed));
	if (_deleted.size() != wasDeleted || _edits.size() != wasEdits) {
		changed();
	}
}

void Archive::cleanFiles() {
	auto keep = base::flat_set<QString>();
	for (const auto &entry : _deleted) {
		if (!entry.message.file.key.isEmpty()) {
			keep.emplace(entry.message.file.key);
		}
	}
	const auto folder = _session->local().telewhiteMediaStore().folder;
	for (const auto &name : QDir(folder).entryList(QDir::Files)) {
		if (!keep.contains(name)) {
			QFile::remove(folder + name);
		}
	}
	QDir(TempFolder(_session)).removeRecursively();
}

void Archive::changed() {
	_dirty = true;
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(kSaveDelay);
	}
	_changes.fire({});
}

rpl::producer<> Archive::changes() const {
	return _changes.events();
}

void Archive::save(bool sync) {
	if (!_dirty) {
		return;
	}
	prune();
	_dirty = false;
	_saveTimer.cancel();
	_session->local().writeTelewhiteData(
		Storage::TelewhiteData::Archive,
		serialize(),
		sync);
}

void Archive::flush() {
	save(true);
}

void Archive::rememberDeleted(PeerId peerId, DeletedMessage message) {
	load();
	_deleted.push_back({ .peerId = peerId, .message = std::move(message) });
	auto removed = std::vector<QString>();
	while (int(_deleted.size()) > kMaxDeleted) {
		const auto &key = _deleted.front().message.file.key;
		if (!key.isEmpty()) {
			removed.push_back(key);
		}
		_deleted.pop_front();
	}
	removeFiles(std::move(removed));
	changed();
}

void Archive::rememberFile(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media || !item->allowsMediaDownloadControls()) {
		return;
	}
	auto pending = PendingFile{
		.peerId = item->history()->peer->id,
		.msgId = item->id,
		.origin = item->fullId(),
		.started = crl::now(),
	};
	if (dynamic_cast<Data::MediaPhoto*>(media)) {
		const auto photo = media->photo();
		if (!photo || photo->isNull()) {
			return;
		}
		pending.name = filedialogDefaultName(
			u"photo"_q,
			u".jpg"_q,
			QString(),
			true,
			item->date());
		pending.photo = photo;
		pending.photoView = photo->createMediaView();
		pending.photoView->wanted(Data::PhotoSize::Large, pending.origin);
	} else if (dynamic_cast<Data::MediaFile*>(media)) {
		const auto document = media->document();
		if (!document || !Archivable(document)) {
			return;
		}
		pending.name = FileName(document, item->date());
		const auto view = document->activeMediaView();
		const auto path = document->filepath(true);
		if (view && !view->bytes().isEmpty()) {
			storeFile(pending, false, [bytes = view->bytes()] {
				return bytes;
			});
			return;
		} else if (!path.isEmpty()) {
			storeFile(pending, false, [=] { return ReadWholeFile(path); });
			return;
		}
		const auto folder = TempFolder(_session);
		QDir().mkpath(folder);
		pending.document = document;
		pending.tempPath = folder + RandomKey() + u".part"_q;
	} else {
		return;
	}
	_pending.push_back(std::move(pending));
	checkPending();
	if (!_pending.empty() && !_pendingTimer.isActive()) {
		_pendingTimer.callEach(kFileCheckDelay);
	}
}

void Archive::checkPending() {
	const auto now = crl::now();
	for (auto i = begin(_pending); i != end(_pending);) {
		if (checkPending(*i, now)) {
			i = _pending.erase(i);
		} else {
			++i;
		}
	}
	if (_pending.empty()) {
		_pendingTimer.cancel();
	}
}

bool Archive::checkPending(PendingFile &pending, crl::time now) {
	if (const auto view = pending.photoView.get()) {
		if (const auto image = view->image(Data::PhotoSize::Large)) {
			auto bytes = view->imageBytes(Data::PhotoSize::Large);
			if (bytes.isEmpty()) {
				storeFile(pending, true, [original = image->original()] {
					return EncodeJpeg(original);
				});
			} else {
				storeFile(pending, true, [bytes = std::move(bytes)] {
					return bytes;
				});
			}
			return true;
		} else if (pending.photo->failed(Data::PhotoSize::Large)) {
			return true;
		}
	} else if (const auto document = pending.document) {
		if (!pending.saving) {
			// WHY: DocumentData::save() cancels a running download that
			// writes to another path, so a download in progress is awaited.
			if (!document->loading()) {
				pending.saving = true;
				document->save(pending.origin, pending.tempPath);
			}
		} else if (!document->loading()) {
			const auto path = pending.tempPath;
			if (QFileInfo(path).size() == document->size) {
				storeFile(pending, false, [=] {
					auto result = ReadWholeFile(path);
					QFile::remove(path);
					return result;
				});
			} else {
				QFile::remove(path);
			}
			return true;
		}
	}
	if (now - pending.started < kFileLoadTimeout) {
		return false;
	} else if (pending.saving
		&& (pending.document->loadingFilePath() == pending.tempPath)) {
		pending.document->cancel();
	}
	if (!pending.tempPath.isEmpty()) {
		QFile::remove(pending.tempPath);
	}
	return true;
}

void Archive::storeFile(
		const PendingFile &pending,
		bool photo,
		Fn<QByteArray()> produce) {
	const auto store = _session->local().telewhiteMediaStore();
	const auto weak = base::make_weak(this);
	const auto peerId = pending.peerId;
	const auto msgId = pending.msgId;
	const auto name = pending.name;
	const auto key = RandomKey();
	crl::async([=] {
		const auto bytes = produce();
		const auto size = int64(bytes.size());
		const auto stored = (size > 0)
			&& (size <= kMaxFileSize)
			&& store.write(key, bytes);
		crl::on_main([=] {
			if (!stored) {
				return;
			} else if (const auto strong = weak.get()) {
				strong->fileStored(peerId, msgId, {
					.key = key,
					.name = name,
					.size = size,
					.photo = photo,
				});
			} else {
				QFile::remove(store.folder + key);
			}
		});
	});
}

void Archive::fileStored(PeerId peerId, MsgId msgId, DeletedFile file) {
	for (auto i = rbegin(_deleted); i != rend(_deleted); ++i) {
		if (i->peerId != peerId || i->message.msgId != msgId) {
			continue;
		} else if (i->message.file.key.isEmpty()) {
			i->message.file = std::move(file);
			limitFiles();
			changed();
			return;
		}
		break;
	}
	removeFiles({ file.key });
}

void Archive::limitFiles() {
	auto total = int64();
	for (const auto &entry : _deleted) {
		total += entry.message.file.size;
	}
	auto removed = std::vector<QString>();
	for (auto &entry : _deleted) {
		if (total <= kMaxFilesSize) {
			break;
		}
		auto &file = entry.message.file;
		if (!file.key.isEmpty()) {
			total -= file.size;
			removed.push_back(base::take(file).key);
		}
	}
	removeFiles(std::move(removed));
}

void Archive::removeFiles(std::vector<QString> keys) {
	if (keys.empty()) {
		return;
	}
	const auto folder = _session->local().telewhiteMediaStore().folder;
	crl::async([=, keys = std::move(keys)] {
		for (const auto &key : keys) {
			QFile::remove(folder + key);
		}
	});
}

bool Archive::hasDeleted(PeerId peerId) {
	load();
	return ranges::contains(_deleted, peerId, &DeletedEntry::peerId);
}

std::vector<DeletedMessage> Archive::deleted(PeerId peerId) {
	load();
	auto result = std::vector<DeletedMessage>();
	for (const auto &entry : _deleted) {
		if (entry.peerId == peerId) {
			result.push_back(entry.message);
		}
	}
	return result;
}

const std::deque<DeletedEntry> &Archive::deletedList() {
	load();
	return _deleted;
}

int Archive::countDeleted(PeerId peerId) {
	load();
	if (peerId == PeerId()) {
		return int(_deleted.size());
	}
	auto result = 0;
	for (const auto &entry : _deleted) {
		if (entry.peerId == peerId) {
			++result;
		}
	}
	return result;
}

void Archive::clearDeleted(PeerId peerId) {
	load();
	const auto all = (peerId == PeerId());
	auto removed = std::vector<QString>();
	for (const auto &entry : _deleted) {
		if ((all || entry.peerId == peerId)
			&& !entry.message.file.key.isEmpty()) {
			removed.push_back(entry.message.file.key);
		}
	}
	const auto was = _deleted.size();
	_deleted.erase(ranges::remove_if(_deleted, [&](const DeletedEntry &e) {
		return all || (e.peerId == peerId);
	}), end(_deleted));
	removeFiles(std::move(removed));
	if (_deleted.size() != was) {
		changed();
	}
}

void Archive::forgetDeleted(PeerId peerId, MsgId msgId) {
	load();
	auto removed = std::vector<QString>();
	const auto was = _deleted.size();
	_deleted.erase(ranges::remove_if(_deleted, [&](const DeletedEntry &e) {
		if (e.peerId != peerId || e.message.msgId != msgId) {
			return false;
		} else if (!e.message.file.key.isEmpty()) {
			removed.push_back(e.message.file.key);
		}
		return true;
	}), end(_deleted));
	removeFiles(std::move(removed));
	if (_deleted.size() != was) {
		changed();
	}
}

void Archive::rememberEditVersion(
		FullMsgId id,
		EditVersion version,
		EditedEntry info) {
	load();
	auto &entry = _edits[id];
	entry.versions.push_back(std::move(version));
	if (int(entry.versions.size()) > kMaxVersions) {
		entry.versions.erase(begin(entry.versions));
	}
	entry.fromId = info.fromId;
	entry.fromName = std::move(info.fromName);
	entry.editedAt = info.editedAt;
	entry.current = std::move(info.current);
	if (int(_edits.size()) > kMaxEdited) {
		auto oldest = end(_edits);
		for (auto i = begin(_edits); i != end(_edits); ++i) {
			if (i->first != id
				&& (oldest == end(_edits)
					|| EditedDate(i->second) < EditedDate(oldest->second))) {
				oldest = i;
			}
		}
		if (oldest != end(_edits)) {
			_edits.erase(oldest);
		}
	}
	changed();
}

std::vector<EditVersion> Archive::editVersions(FullMsgId id) {
	load();
	const auto i = _edits.find(id);
	return (i != end(_edits))
		? i->second.versions
		: std::vector<EditVersion>();
}

auto Archive::editedList()
-> const base::flat_map<FullMsgId, EditedEntry> & {
	load();
	return _edits;
}

int Archive::countEdited(PeerId peerId) {
	load();
	if (peerId == PeerId()) {
		return int(_edits.size());
	}
	auto result = 0;
	for (const auto &pair : _edits) {
		if (pair.first.peer == peerId) {
			++result;
		}
	}
	return result;
}

void Archive::refreshEdited(FullMsgId id) {
	// The previous versions may be shown right under the message text.
	if (const auto item = _session->data().message(id)) {
		_session->data().requestItemTextRefresh(item);
	}
}

void Archive::clearEdited(PeerId peerId) {
	load();
	const auto all = (peerId == PeerId());
	auto removed = std::vector<FullMsgId>();
	for (auto i = begin(_edits); i != end(_edits);) {
		if (all || i->first.peer == peerId) {
			removed.push_back(i->first);
			i = _edits.erase(i);
		} else {
			++i;
		}
	}
	if (removed.empty()) {
		return;
	}
	for (const auto &id : removed) {
		refreshEdited(id);
	}
	changed();
}

void Archive::forgetEdited(FullMsgId id) {
	load();
	const auto i = _edits.find(id);
	if (i == end(_edits)) {
		return;
	}
	_edits.erase(i);
	refreshEdited(id);
	changed();
}

QByteArray Archive::serialize() const {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kArchiveVersion
		<< quint64(_session->userId().bare)
		<< quint32(_deleted.size());
	for (const auto &[peerId, message] : _deleted) {
		stream
			<< SerializePeerId(peerId)
			<< qint64(message.msgId.bare)
			<< qint32(message.date)
			<< qint32(message.deletedAt)
			<< SerializePeerId(message.fromId)
			<< message.fromName;
		WriteText(stream, message.text);
		stream << message.media;
	}
	stream << quint32(_edits.size());
	for (const auto &[id, entry] : _edits) {
		stream
			<< SerializePeerId(id.peer)
			<< qint64(id.msg.bare)
			<< quint32(entry.versions.size());
		for (const auto &version : entry.versions) {
			stream << qint32(version.date);
			WriteText(stream, version.text);
		}
	}
	const auto files = ranges::count_if(_deleted, [](const auto &entry) {
		return !entry.message.file.key.isEmpty();
	});
	stream << quint32(files);
	for (auto i = 0; i != int(_deleted.size()); ++i) {
		const auto &file = _deleted[i].message.file;
		if (!file.key.isEmpty()) {
			stream
				<< quint32(i)
				<< file.key
				<< file.name
				<< qint64(file.size)
				<< qint32(file.photo ? 1 : 0);
		}
	}

	// Who edited and when, with the text after the edit. It goes after
	// everything else: older Telewhite builds read the archive as before.
	const auto described = ranges::count_if(_edits, [](const auto &pair) {
		return (pair.second.editedAt != 0);
	});
	stream << quint32(described);
	for (const auto &[id, entry] : _edits) {
		if (!entry.editedAt) {
			continue;
		}
		stream
			<< SerializePeerId(id.peer)
			<< qint64(id.msg.bare)
			<< SerializePeerId(entry.fromId)
			<< entry.fromName
			<< qint32(entry.editedAt);
		WriteText(stream, entry.current);
	}
	return result;
}

bool Archive::deserialize(const QByteArray &serialized) {
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32();
	auto userId = quint64();
	auto deletedCount = quint32();
	stream >> version >> userId >> deletedCount;
	if (stream.status() != QDataStream::Ok
		|| version != kArchiveVersion
		|| userId != _session->userId().bare
		|| deletedCount > quint32(kMaxDeleted)) {
		return false;
	}
	for (auto i = quint32(); i != deletedCount; ++i) {
		auto peerId = quint64();
		auto msgId = qint64();
		auto date = qint32();
		auto deletedAt = qint32();
		auto fromId = quint64();
		auto entry = DeletedEntry();
		stream
			>> peerId
			>> msgId
			>> date
			>> deletedAt
			>> fromId
			>> entry.message.fromName;
		entry.message.text = ReadText(stream);
		stream >> entry.message.media;
		if (stream.status() != QDataStream::Ok) {
			return false;
		}
		entry.peerId = DeserializePeerId(peerId);
		entry.message.msgId = MsgId(msgId);
		entry.message.date = TimeId(date);
		entry.message.deletedAt = TimeId(deletedAt);
		entry.message.fromId = DeserializePeerId(fromId);
		_deleted.push_back(std::move(entry));
	}
	auto editsCount = quint32();
	stream >> editsCount;
	if (stream.status() != QDataStream::Ok
		|| editsCount > quint32(kMaxEdited)) {
		return false;
	}
	for (auto i = quint32(); i != editsCount; ++i) {
		auto peerId = quint64();
		auto msgId = qint64();
		auto count = quint32();
		stream >> peerId >> msgId >> count;
		if (stream.status() != QDataStream::Ok
			|| !count
			|| count > quint32(kMaxVersions)) {
			return false;
		}
		auto versions = std::vector<EditVersion>();
		versions.reserve(count);
		for (auto j = quint32(); j != count; ++j) {
			auto date = qint32();
			stream >> date;
			auto text = ReadText(stream);
			if (stream.status() != QDataStream::Ok) {
				return false;
			}
			versions.push_back({
				.text = std::move(text),
				.date = TimeId(date),
			});
		}
		_edits.emplace(
			FullMsgId(DeserializePeerId(peerId), MsgId(msgId)),
			EditedEntry{ .versions = std::move(versions) });
	}
	if (stream.atEnd()) {
		return true;
	}
	auto filesCount = quint32();
	stream >> filesCount;
	if (stream.status() != QDataStream::Ok || filesCount > deletedCount) {
		return false;
	}
	for (auto i = quint32(); i != filesCount; ++i) {
		auto index = quint32();
		auto file = DeletedFile();
		auto size = qint64();
		auto photo = qint32();
		stream >> index >> file.key >> file.name >> size >> photo;
		if (stream.status() != QDataStream::Ok || index >= deletedCount) {
			return false;
		}
		file.size = size;
		file.photo = (photo == 1);
		_deleted[index].message.file = std::move(file);
	}
	if (stream.atEnd()) {
		return true;
	}

	// Who edited and when. A broken tail doesn't drop what was read above.
	auto describedCount = quint32();
	stream >> describedCount;
	if (stream.status() != QDataStream::Ok
		|| describedCount > quint32(kMaxEdited)) {
		return true;
	}
	for (auto i = quint32(); i != describedCount; ++i) {
		auto peerId = quint64();
		auto msgId = qint64();
		auto fromId = quint64();
		auto fromName = QString();
		auto editedAt = qint32();
		stream >> peerId >> msgId >> fromId >> fromName >> editedAt;
		auto current = ReadText(stream);
		if (stream.status() != QDataStream::Ok) {
			break;
		}
		const auto j = _edits.find(
			FullMsgId(DeserializePeerId(peerId), MsgId(msgId)));
		if (j != end(_edits)) {
			j->second.fromId = DeserializePeerId(fromId);
			j->second.fromName = std::move(fromName);
			j->second.editedAt = TimeId(editedAt);
			j->second.current = std::move(current);
		}
	}
	return true;
}

using Archives = base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<Archive>>;

[[nodiscard]] Archives &AllArchives() {
	static auto result = Archives();
	return result;
}

[[nodiscard]] not_null<Archive*> ArchiveFor(
		not_null<Main::Session*> session) {
	auto &archives = AllArchives();
	if (const auto i = archives.find(session); i != end(archives)) {
		return i->second.get();
	}
	const auto result = archives.emplace(
		session,
		std::make_unique<Archive>(session)).first->second.get();
	session->lifetime().add([=] {
		auto &archives = AllArchives();
		const auto i = archives.find(session);
		if (i != end(archives)) {
			const auto archive = std::move(i->second);
			archives.erase(i);
			archive->flush();
		}
	});
	return result;
}

[[nodiscard]] not_null<Archive*> ArchiveFor(not_null<HistoryItem*> item) {
	return ArchiveFor(&item->history()->session());
}

void AddPhotoPreview(
		not_null<Ui::VerticalLayout*> container,
		const Storage::TelewhiteMediaStore &store,
		const QString &key,
		Fn<void()> open) {
	const auto button = container->add(
		object_ptr<Ui::AbstractButton>(container),
		st::boxRowPadding);
	const auto image = button->lifetime().make_state<QImage>();
	button->paintRequest() | rpl::on_next([=] {
		if (!image->isNull()) {
			QPainter(button).drawImage(0, 0, *image);
		}
	}, button->lifetime());
	button->setClickedCallback(std::move(open));

	const auto ratio = style::DevicePixelRatio();
	const auto padding = st::boxRowPadding;
	const auto limit = QSize(
		st::boxWideWidth - padding.left() - padding.right(),
		st::sendMediaPreviewSize) * ratio;
	const auto weak = QPointer<Ui::AbstractButton>(button);
	crl::async([=] {
		auto result = QImage::fromData(store.read(key));
		if (!result.isNull()) {
			if (result.width() > limit.width()
				|| result.height() > limit.height()) {
				result = result.scaled(
					limit,
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
			}
			result.setDevicePixelRatio(ratio);
		}
		crl::on_main([=, result = std::move(result)]() mutable {
			if (const auto strong = weak.data()) {
				*image = std::move(result);
				strong->resize(strong->width(), image->height() / ratio);
				strong->update();
			}
		});
	});
}

void AddFileActions(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const DeletedFile &file,
		bool preview) {
	const auto store = session->local().telewhiteMediaStore();
	const auto weak = base::make_weak(session);
	const auto key = file.key;
	const auto name = file.name;
	const auto size = file.size;
	const auto photo = file.photo;
	const auto opened = TempFolder(session) + key + u"_"_q + name;
	const auto toast = [=](QString text) {
		if (weak && show->valid()) {
			show->showToast(text);
		}
	};
	const auto open = [=] {
		crl::async([=] {
			const auto ready = (QFileInfo(opened).size() == size)
				|| WriteWholeFile(opened, store.read(key));
			crl::on_main([=] {
				if (!weak) {
					return;
				} else if (ready) {
					File::Launch(opened);
				} else {
					toast(tr::lng_telewhite_deleted_file_missing(tr::now));
				}
			});
		});
	};
	const auto save = [=] {
		if (cDialogLastPath().isEmpty()) {
			Platform::FileDialog::InitLastPath();
		}
		const auto extension = QFileInfo(name).suffix();
		const auto filter = (extension.isEmpty()
			? QString()
			: (u"*."_q + extension + u";;"_q))
			+ FileDialog::AllFilesFilter();
		const auto caption = photo
			? tr::lng_save_photo(tr::now)
			: tr::lng_save_file(tr::now);
		FileDialog::GetWritePath(
			Core::App().getFileDialogParent(),
			caption,
			filter,
			filedialogNextFilename(name, QString()),
			crl::guard(session.get(), [=](const QString &path) {
				if (path.isEmpty()) {
					return;
				}
				crl::async([=] {
					const auto saved = WriteWholeFile(path, store.read(key));
					crl::on_main([=] {
						toast(saved
							? tr::lng_telewhite_deleted_file_saved(tr::now)
							: tr::lng_telewhite_deleted_file_missing(tr::now));
					});
				});
			}));
	};
	if (preview && photo) {
		AddPhotoPreview(container, store, key, open);
	}
	auto text = tr::link(tr::lng_telewhite_deleted_open(tr::now), 1);
	text.append(u" · "_q);
	text.append(tr::link(tr::lng_telewhite_deleted_save(tr::now), 2));
	text.append(u" · "_q + Ui::FormatSizeText(size));
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(std::move(text)),
			st::boxLabel),
		st::boxRowPadding);
	label->setLink(1, std::make_shared<LambdaClickHandler>(open));
	label->setLink(2, std::make_shared<LambdaClickHandler>(save));
}

enum class JournalKind {
	Deleted,
	Edited,
};

enum class JournalType {
	All,
	Personal,
	Groups,
	Channels,
	Bots,
};

struct JournalState {
	JournalType type = JournalType::All;
	QString query;
	int limit = kJournalPage;
	base::Timer searchTimer;
};

struct JournalFill {
	int shown = 0;
	int total = 0;
};

struct EntryAction {
	QString text;
	Fn<void()> callback;
};

[[nodiscard]] std::vector<QString> JournalTypeNames() {
	return {
		u"Все"_q,
		u"Личные"_q,
		u"Группы"_q,
		u"Каналы"_q,
		u"Боты"_q,
	};
}

[[nodiscard]] JournalType JournalTypeOf(
		not_null<Main::Session*> session,
		PeerId peerId) {
	if (const auto peer = session->data().peerLoaded(peerId)) {
		if (const auto user = peer->asUser()) {
			return user->isBot()
				? JournalType::Bots
				: JournalType::Personal;
		} else if (peer->isChannel() && !peer->isMegagroup()) {
			return JournalType::Channels;
		}
		return JournalType::Groups;
	} else if (peerIsUser(peerId)) {
		return JournalType::Personal;
	} else if (peerIsChannel(peerId)) {
		return JournalType::Channels;
	}
	return JournalType::Groups;
}

[[nodiscard]] QString ChatName(
		not_null<Main::Session*> session,
		PeerId peerId) {
	if (const auto peer = session->data().peerLoaded(peerId)) {
		if (peer->isSelf()) {
			return tr::lng_saved_messages(tr::now);
		} else if (const auto name = peer->name(); !name.isEmpty()) {
			return name;
		}
	}
	return u"Чат"_q;
}

// "Chat · Author", the name is not repeated in a personal chat.
[[nodiscard]] QString EntryTitle(
		not_null<Main::Session*> session,
		PeerId peerId,
		const QString &author,
		bool withChat) {
	auto result = withChat ? ChatName(session, peerId) : QString();
	if (!author.isEmpty() && author != result) {
		if (!result.isEmpty()) {
			result += u" · "_q;
		}
		result += author;
	}
	return result.isEmpty() ? ChatName(session, peerId) : result;
}

[[nodiscard]] QString ShortDateTime(TimeId date) {
	return base::unixtime::parse(date).toString(u"dd.MM.yy HH:mm"_q);
}

[[nodiscard]] bool MatchesQuery(const QString &text, const QString &query) {
	return text.contains(query, Qt::CaseInsensitive);
}

[[nodiscard]] TextWithEntities DeletedText(const DeletedMessage &message) {
	auto result = TextWithEntities();
	if (!message.media.isEmpty()) {
		result.append(u"["_q + message.media + u"]"_q);
		if (!message.text.text.isEmpty()) {
			result.append(u"\n"_q);
		}
	}
	result.append(message.text);
	return result;
}

[[nodiscard]] bool DeletedMatches(
		not_null<Main::Session*> session,
		const DeletedEntry &entry,
		const QString &query,
		bool withChat) {
	const auto &message = entry.message;
	return MatchesQuery(message.text.text, query)
		|| MatchesQuery(message.media, query)
		|| MatchesQuery(message.fromName, query)
		|| (withChat
			&& MatchesQuery(ChatName(session, entry.peerId), query));
}

[[nodiscard]] bool EditedMatches(
		not_null<Main::Session*> session,
		FullMsgId id,
		const EditedEntry &entry,
		const QString &query,
		bool withChat) {
	if (MatchesQuery(entry.current.text, query)
		|| MatchesQuery(entry.fromName, query)
		|| (withChat && MatchesQuery(ChatName(session, id.peer), query))) {
		return true;
	} else if (const auto item = session->data().message(id)) {
		if (MatchesQuery(item->originalText().text, query)) {
			return true;
		}
	}
	return ranges::any_of(entry.versions, [&](const EditVersion &version) {
		return MatchesQuery(version.text.text, query);
	});
}

void AddEntryText(
		not_null<Ui::VerticalLayout*> list,
		TextWithEntities text,
		TextWithEntities copy,
		std::shared_ptr<Ui::Show> show,
		bool pale) {
	const auto label = list->add(
		object_ptr<Ui::FlatLabel>(
			list,
			rpl::single(std::move(text)),
			st::boxLabel),
		st::boxRowPadding);
	label->setSelectable(true);
	if (pale) {
		label->setTextColorOverride(
			std::make_optional(st::windowSubTextFg->c));
	}
	if (!copy.text.isEmpty()) {
		OnLongPress(label, [=] {
			CopyWithFormatting(show, copy);
		});
	}
}

void AddEntryActions(
		not_null<Ui::VerticalLayout*> list,
		std::vector<EntryAction> actions,
		const QString &about) {
	auto text = TextWithEntities();
	for (auto i = 0; i != int(actions.size()); ++i) {
		if (i) {
			text.append(u" · "_q);
		}
		text.append(tr::link(actions[i].text, i + 1));
	}
	if (!about.isEmpty()) {
		text.append(u" · "_q + about);
	}
	const auto label = list->add(
		object_ptr<Ui::FlatLabel>(
			list,
			rpl::single(std::move(text)),
			st::boxLabel),
		st::boxRowPadding);
	for (auto i = 0; i != int(actions.size()); ++i) {
		label->setLink(
			uint16(i + 1),
			std::make_shared<LambdaClickHandler>(actions[i].callback));
	}
}

void OpenInChat(
		not_null<Window::SessionController*> controller,
		not_null<Ui::GenericBox*> box,
		PeerId peerId,
		MsgId msgId) {
	const auto weak = base::make_weak(box);
	controller->showPeerHistory(
		peerId,
		Window::SectionShow::Way::ClearStack,
		msgId);
	if (const auto strong = weak.get()) {
		strong->closeBox();
	}
}

void AddDeletedEntry(
		not_null<Ui::VerticalLayout*> list,
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const DeletedEntry &entry,
		bool withChat,
		bool preview,
		Fn<void()> refresh) {
	const auto session = &controller->session();
	const auto show = box->uiShow();
	const auto &message = entry.message;
	const auto peerId = entry.peerId;
	const auto msgId = message.msgId;
	Ui::AddSubsectionTitle(
		list,
		rpl::single(
			EntryTitle(session, peerId, message.fromName, withChat)
			+ u", "_q
			+ ShortDateTime(message.date)));
	auto text = DeletedText(message);
	if (text.text.isEmpty()) {
		text.append(tr::lng_telewhite_edit_history_empty(tr::now));
	}
	AddEntryText(list, std::move(text), message.text, show, false);
	if (!message.file.key.isEmpty()) {
		AddFileActions(list, show, session, message.file, preview);
	}
	AddEntryActions(list, {
		{
			.text = u"Открыть в чате"_q,
			.callback = [=] {
				OpenInChat(controller, box, peerId, msgId);
			},
		},
		{
			.text = u"Убрать"_q,
			.callback = [=] {
				ArchiveFor(session)->forgetDeleted(peerId, msgId);
				refresh();
			},
		},
	}, u"удалено "_q + ShortDateTime(message.deletedAt));
	Ui::AddSkip(list);
}

void AddEditedEntry(
		not_null<Ui::VerticalLayout*> list,
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		FullMsgId id,
		const EditedEntry &entry,
		bool withChat,
		Fn<void()> refresh) {
	const auto session = &controller->session();
	const auto show = box->uiShow();
	const auto item = session->data().message(id);
	const auto date = EditedDate(entry);
	const auto author = (entry.fromName.isEmpty() && item)
		? item->from()->name()
		: entry.fromName;
	Ui::AddSubsectionTitle(
		list,
		rpl::single(EntryTitle(session, id.peer, author, withChat)));

	// Archives of older builds don't know the text after the edit.
	const auto known = (item != nullptr) || (entry.editedAt != 0);
	const auto current = item ? item->originalText() : entry.current;
	if (known) {
		AddEntryText(
			list,
			(current.text.isEmpty()
				? TextWithEntities{
					tr::lng_telewhite_edit_history_empty(tr::now),
				}
				: current),
			current,
			show,
			false);
	}
	if (!entry.versions.empty()) {
		const auto &was = entry.versions.back().text;
		auto text = TextWithEntities{ u"Было: "_q };
		text.append(was.text.isEmpty()
			? TextWithEntities{
				tr::lng_telewhite_edit_history_empty(tr::now),
			}
			: was);
		AddEntryText(list, std::move(text), was, show, true);
	}

	auto versions = entry.versions;
	versions.push_back({
		.text = (known
			? current
			: TextWithEntities{
				u"Сообщение не загружено, его текст сейчас неизвестен."_q,
			}),
		.date = date,
	});
	const auto count = int(versions.size());
	AddEntryActions(list, {
		{
			.text = (u"Все версии ("_q
				+ QString::number(count)
				+ u")"_q),
			.callback = [=] {
				controller->show(Box(EditVersionsBox, versions));
			},
		},
		{
			.text = u"Открыть в чате"_q,
			.callback = [=] {
				OpenInChat(controller, box, id.peer, id.msg);
			},
		},
		{
			.text = u"Убрать"_q,
			.callback = [=] {
				ArchiveFor(session)->forgetEdited(id);
				refresh();
			},
		},
	}, u"изменено "_q + ShortDateTime(date));
	Ui::AddSkip(list);
}

JournalFill FillDeleted(
		not_null<Ui::VerticalLayout*> list,
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		not_null<const JournalState*> state,
		Fn<void()> refresh) {
	const auto session = &controller->session();
	const auto &all = ArchiveFor(session)->deletedList();
	auto result = JournalFill();
	auto previews = 0;
	for (auto i = int(all.size()); i != 0;) {
		const auto &entry = all[--i];
		if (peer) {
			if (entry.peerId != peer->id) {
				continue;
			}
		} else if (state->type != JournalType::All
			&& JournalTypeOf(session, entry.peerId) != state->type) {
			continue;
		}
		if (!state->query.isEmpty()
			&& !DeletedMatches(session, entry, state->query, !peer)) {
			continue;
		}
		++result.total;
		if (result.shown >= state->limit) {
			continue;
		}
		++result.shown;
		const auto preview = entry.message.file.photo
			&& !entry.message.file.key.isEmpty()
			&& (previews < kMaxPreviews);
		if (preview) {
			++previews;
		}
		AddDeletedEntry(
			list,
			box,
			controller,
			entry,
			!peer,
			preview,
			refresh);
	}
	return result;
}

JournalFill FillEdited(
		not_null<Ui::VerticalLayout*> list,
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		not_null<const JournalState*> state,
		Fn<void()> refresh) {
	const auto session = &controller->session();
	const auto &all = ArchiveFor(session)->editedList();
	auto matched = std::vector<std::pair<TimeId, FullMsgId>>();
	for (const auto &[id, entry] : all) {
		if (peer) {
			if (id.peer != peer->id) {
				continue;
			}
		} else if (state->type != JournalType::All
			&& JournalTypeOf(session, id.peer) != state->type) {
			continue;
		}
		if (!state->query.isEmpty()
			&& !EditedMatches(session, id, entry, state->query, !peer)) {
			continue;
		}
		matched.emplace_back(EditedDate(entry), id);
	}
	ranges::sort(matched, [](const auto &a, const auto &b) {
		return (a.first > b.first);
	});
	auto result = JournalFill{ .total = int(matched.size()) };
	for (const auto &pair : matched) {
		if (result.shown >= state->limit) {
			break;
		}
		const auto i = all.find(pair.second);
		if (i == end(all)) {
			continue;
		}
		++result.shown;
		AddEditedEntry(
			list,
			box,
			controller,
			pair.second,
			i->second,
			!peer,
			refresh);
	}
	return result;
}

void JournalBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		JournalKind kind,
		PeerData *peer) {
	const auto deleted = (kind == JournalKind::Deleted);
	const auto session = &controller->session();
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(peer
		? (deleted
			? tr::lng_telewhite_deleted_title(tr::now)
			: u"Изменённые сообщения"_q)
		: (deleted
			? u"История удалений"_q
			: u"История изменений"_q)));

	const auto state = box->lifetime().make_state<JournalState>();
	const auto content = box->verticalLayout();
	const auto list = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto rebuild = box->lifetime().make_state<Fn<void()>>();

	// Links call it from their own click handlers, so it rebuilds later.
	const auto refresh = [=] {
		crl::on_main(box.get(), [=] {
			(*rebuild)();
		});
	};
	*rebuild = [=] {
		list->clear();
		Ui::AddSkip(list);
		const auto counter = list->add(
			object_ptr<Ui::FlatLabel>(list, st::boxLabel),
			st::boxRowPadding);
		counter->setTextColorOverride(
			std::make_optional(st::windowSubTextFg->c));
		Ui::AddSkip(list);
		const auto result = deleted
			? FillDeleted(list, box, controller, peer, state, refresh)
			: FillEdited(list, box, controller, peer, state, refresh);
		if (!result.total) {
			counter->setText(!state->query.isEmpty()
				? u"Ничего не найдено."_q
				: deleted
				? u"Пока пусто. Здесь появятся сообщения, которые удалили у вас в чатах, пока Telewhite был открыт."_q
				: u"Пока пусто. Здесь появятся сообщения, которые отредактировали у вас в чатах, пока Telewhite был открыт."_q);
		} else if (result.shown < result.total) {
			counter->setText(u"Найдено: "_q
				+ QString::number(result.total)
				+ u", показаны первые "_q
				+ QString::number(result.shown));
		} else {
			counter->setText(
				u"Найдено: "_q + QString::number(result.total));
		}
		if (result.shown < result.total) {
			const auto more = list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					rpl::single(tr::link(u"Показать ещё"_q, 1)),
					st::boxLabel),
				st::boxRowPadding);
			more->setLink(1, std::make_shared<LambdaClickHandler>([=] {
				state->limit += kJournalPage;
				refresh();
			}));
			Ui::AddSkip(list);
		}
		if (result.shown > 0) {
			const auto hint = list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					rpl::single(u"Зажмите текст, чтобы скопировать его с форматированием."_q),
					st::boxLabel),
				st::boxRowPadding);
			hint->setTextColorOverride(
				std::make_optional(st::windowSubTextFg->c));
			Ui::AddSkip(list);
		}
		list->resizeToWidth(content->width());
	};

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	if (!peer) {
		const auto tabs = top->add(
			object_ptr<Ui::SettingsSlider>(top, st::defaultTabsSlider));
		tabs->setSections(JournalTypeNames());
		tabs->sectionActivated(
		) | rpl::on_next([=](int index) {
			state->type = JournalType(index);
			state->limit = kJournalPage;
			(*rebuild)();
		}, tabs->lifetime());
	}
	Ui::AddSkip(top);
	const auto field = top->add(
		object_ptr<Ui::InputField>(
			top,
			st::defaultInputField,
			tr::lng_dlg_filter(),
			QString()),
		st::boxRowPadding);
	Ui::AddSkip(top);
	state->searchTimer.setCallback([=] {
		const auto query = field->getLastText().trimmed();
		if (state->query != query) {
			state->query = query;
			state->limit = kJournalPage;
			(*rebuild)();
		}
	});
	field->changes(
	) | rpl::on_next([=] {
		state->searchTimer.callOnce(kSearchDelay);
	}, field->lifetime());
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	(*rebuild)();

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addLeftButton(rpl::single(u"Очистить"_q), [=] {
		const auto text = peer
			? (deleted
				? u"Удалённые сообщения этого чата пропадут из журнала. Это нельзя отменить."_q
				: u"Прежние версии сообщений этого чата будут стёрты. Это нельзя отменить."_q)
			: (deleted
				? u"Все удалённые сообщения этого аккаунта пропадут из журнала. Это нельзя отменить."_q
				: u"Прежние версии всех сообщений этого аккаунта будут стёрты. Это нельзя отменить."_q);
		box->uiShow()->show(Ui::MakeConfirmBox({
			.text = text,
			.confirmed = [=](Fn<void()> close) {
				const auto archive = ArchiveFor(session);
				const auto peerId = peer ? peer->id : PeerId();
				if (deleted) {
					archive->clearDeleted(peerId);
				} else {
					archive->clearEdited(peerId);
				}
				close();
				refresh();
			},
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
}

} // namespace

void RememberDeleted(not_null<HistoryItem*> item) {
	if (!item->isRegular() || item->isService()) {
		return;
	}
	const auto media = item->media();
	const auto from = item->from();
	const auto archive = ArchiveFor(item);
	archive->rememberDeleted(item->history()->peer->id, {
		.msgId = item->id,
		.date = item->date(),
		.deletedAt = base::unixtime::now(),
		.fromId = from->id,
		.fromName = from->name(),
		.text = item->originalText(),
		.media = media ? media->pinnedTextSubstring() : QString(),
	});
	archive->rememberFile(item);
}

bool HasDeleted(not_null<PeerData*> peer) {
	return ArchiveFor(&peer->session())->hasDeleted(peer->id);
}

std::vector<DeletedMessage> Deleted(not_null<PeerData*> peer) {
	return ArchiveFor(&peer->session())->deleted(peer->id);
}

void ClearDeleted(not_null<PeerData*> peer) {
	ArchiveFor(&peer->session())->clearDeleted(peer->id);
}

void ForgetDeleted(not_null<HistoryItem*> item) {
	ArchiveFor(item)->forgetDeleted(item->history()->peer->id, item->id);
}

void PruneArchives() {
	for (const auto &[session, archive] : AllArchives()) {
		archive->prune();
	}
}

void RememberEditVersion(not_null<HistoryItem*> item, EditVersion version) {
	const auto from = item->from();
	const auto edited = item->Get<HistoryMessageEdited>();
	ArchiveFor(item)->rememberEditVersion(
		item->fullId(),
		std::move(version),
		{
			.fromId = from->id,
			.fromName = from->name(),
			.editedAt = edited ? edited->date : base::unixtime::now(),
			.current = item->originalText(),
		});
}

std::vector<EditVersion> EditVersions(not_null<HistoryItem*> item) {
	return ArchiveFor(item)->editVersions(item->fullId());
}

rpl::producer<int> DeletedCountValue(not_null<Main::Session*> session) {
	const auto archive = ArchiveFor(session);
	const auto weak = base::make_weak(archive.get());
	return rpl::single(rpl::empty) | rpl::then(
		archive->changes()
	) | rpl::map([=] {
		const auto strong = weak.get();
		return strong ? strong->countDeleted(PeerId()) : 0;
	});
}

rpl::producer<int> EditedCountValue(not_null<Main::Session*> session) {
	const auto archive = ArchiveFor(session);
	const auto weak = base::make_weak(archive.get());
	return rpl::single(rpl::empty) | rpl::then(
		archive->changes()
	) | rpl::map([=] {
		const auto strong = weak.get();
		return strong ? strong->countEdited(PeerId()) : 0;
	});
}

void ShowDeletedJournal(
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	controller->show(
		Box(JournalBox, controller, JournalKind::Deleted, peer));
}

void ShowEditedJournal(
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	controller->show(
		Box(JournalBox, controller, JournalKind::Edited, peer));
}

void AddDeletedMessagesAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	const auto archive = ArchiveFor(&peer->session());
	const auto deleted = archive->countDeleted(peer->id);
	if (deleted > 0) {
		addAction(
			tr::lng_telewhite_deleted_title(tr::now)
				+ u" ("_q
				+ QString::number(deleted)
				+ u")"_q,
			[=] { ShowDeletedJournal(controller, peer); },
			&st::menuIconDelete);
	}
	const auto edited = archive->countEdited(peer->id);
	if (edited > 0) {
		addAction(
			u"Изменённые сообщения ("_q
				+ QString::number(edited)
				+ u")"_q,
			[=] { ShowEditedJournal(controller, peer); },
			&st::menuIconEdit);
	}
}

} // namespace Telewhite
