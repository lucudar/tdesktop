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
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "platform/platform_file_utilities.h"
#include "settings.h"
#include "storage/storage_account.h"
#include "telewhite/telewhite_mods.h"
#include "ui/abstract_button.h"
#include "ui/boxes/confirm_box.h"
#include "ui/click_handler.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

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
constexpr auto kMaxShownDeleted = 300;
constexpr auto kMaxPreviews = 50;
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
	void clearDeleted(PeerId peerId);

	void rememberEditVersion(FullMsgId id, EditVersion version);
	[[nodiscard]] std::vector<EditVersion> editVersions(FullMsgId id);

	void flush();

private:
	struct DeletedEntry {
		PeerId peerId = 0;
		DeletedMessage message;
	};

	void load();
	void cleanFiles();
	void changed();
	void save(bool sync);
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
	base::flat_map<FullMsgId, std::vector<EditVersion>> _edits;
	std::vector<PendingFile> _pending;
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
	cleanFiles();
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
}

void Archive::save(bool sync) {
	if (!_dirty) {
		return;
	}
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

void Archive::clearDeleted(PeerId peerId) {
	load();
	auto removed = std::vector<QString>();
	for (const auto &entry : _deleted) {
		if (entry.peerId == peerId && !entry.message.file.key.isEmpty()) {
			removed.push_back(entry.message.file.key);
		}
	}
	const auto was = _deleted.size();
	_deleted.erase(
		ranges::remove(_deleted, peerId, &DeletedEntry::peerId),
		end(_deleted));
	removeFiles(std::move(removed));
	if (_deleted.size() != was) {
		changed();
	}
}

void Archive::rememberEditVersion(FullMsgId id, EditVersion version) {
	load();
	auto &list = _edits[id];
	list.push_back(std::move(version));
	if (int(list.size()) > kMaxVersions) {
		list.erase(begin(list));
	}
	if (int(_edits.size()) > kMaxEdited) {
		auto oldest = end(_edits);
		for (auto i = begin(_edits); i != end(_edits); ++i) {
			if (i->first != id
				&& (oldest == end(_edits)
					|| i->second.back().date < oldest->second.back().date)) {
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
	return (i != end(_edits)) ? i->second : std::vector<EditVersion>();
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
	for (const auto &[id, versions] : _edits) {
		stream
			<< SerializePeerId(id.peer)
			<< qint64(id.msg.bare)
			<< quint32(versions.size());
		for (const auto &version : versions) {
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
			std::move(versions));
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

void DeletedMessagesBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer) {
	box->setTitle(tr::lng_telewhite_deleted_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	const auto session = &peer->session();
	const auto list = Deleted(peer);
	const auto count = int(list.size());
	const auto from = std::max(count - kMaxShownDeleted, 0);
	auto previews = 0;
	for (auto i = count; i != from;) {
		const auto &message = list[--i];
		Ui::AddSubsectionTitle(
			container,
			tr::lng_telewhite_deleted_entry(
				lt_name,
				rpl::single(message.fromName),
				lt_date,
				rpl::single(FormatExactDateTime(message.date))));
		auto text = TextWithEntities();
		if (!message.media.isEmpty()) {
			text.append(u"["_q + message.media + u"]"_q);
			if (!message.text.text.isEmpty()) {
				text.append(u"\n"_q);
			}
		}
		text.append(message.text);
		if (text.text.isEmpty()) {
			text.append(tr::lng_telewhite_edit_history_empty(tr::now));
		}
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(std::move(text)),
				st::boxLabel),
			st::boxRowPadding);
		label->setSelectable(true);
		if (!message.file.key.isEmpty()) {
			const auto preview = message.file.photo
				&& (previews < kMaxPreviews);
			if (preview) {
				++previews;
			}
			AddFileActions(container, show, session, message.file, preview);
		}
		Ui::AddSkip(container);
	}

	const auto weak = base::make_weak(box);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addLeftButton(tr::lng_telewhite_deleted_clear(), [=] {
		box->uiShow()->show(Ui::MakeConfirmBox({
			.text = tr::lng_telewhite_deleted_clear_sure(),
			.confirmed = [=](Fn<void()> close) {
				ClearDeleted(peer);
				close();
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
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

void RememberEditVersion(not_null<HistoryItem*> item, EditVersion version) {
	ArchiveFor(item)->rememberEditVersion(item->fullId(), std::move(version));
}

std::vector<EditVersion> EditVersions(not_null<HistoryItem*> item) {
	return ArchiveFor(item)->editVersions(item->fullId());
}

void AddDeletedMessagesAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!HasDeleted(peer)) {
		return;
	}
	addAction(tr::lng_telewhite_deleted_title(tr::now), [=] {
		controller->show(Box(DeletedMessagesBox, controller->uiShow(), peer));
	}, &st::menuIconDelete);
}

} // namespace Telewhite
