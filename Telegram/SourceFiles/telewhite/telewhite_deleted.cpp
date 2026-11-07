/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_deleted.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "telewhite/telewhite_archive.h"
#include "telewhite/telewhite_mods.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Telewhite {
namespace {

// Colorized entity data, the color is the ninth one in the span:
// eight syntax highlight colors are passed to the message text.
constexpr auto kVersionsColorData = 9;
constexpr auto kVersionsColorIndex = kVersionsColorData - 1;

// Older versions are available in the "Edit history" box.
constexpr auto kMaxInlineVersions = 10;

// Self-destructing ("view once" and timed) media should not outlive it.
[[nodiscard]] bool HasSelfDestructMedia(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return media && (media->ttlSeconds() > 0);
}

[[nodiscard]] std::string FoldersKey(not_null<Main::Session*> session) {
	return "telewhite-exclude-folders-"
		+ std::to_string(session->userId().bare);
}

[[nodiscard]] auto FoldersCache()
-> base::flat_map<uint64, base::flat_set<FilterId>> & {
	static auto result = base::flat_map<uint64, base::flat_set<FilterId>>();
	return result;
}

[[nodiscard]] const base::flat_set<FilterId> &Folders(
		not_null<Main::Session*> session) {
	auto &cache = FoldersCache();
	const auto userId = session->userId().bare;
	if (const auto i = cache.find(userId); i != end(cache)) {
		return i->second;
	}
	auto ids = base::flat_set<FilterId>();
	const auto serialized = Core::App().settings().readPref<QByteArray>(
		FoldersKey(session));
	for (const auto &part : serialized.split(',')) {
		auto ok = false;
		const auto id = part.toInt(&ok);
		if (ok && id) {
			ids.emplace(FilterId(id));
		}
	}
	return cache.emplace(userId, std::move(ids)).first->second;
}

[[nodiscard]] std::string ChatsKey(not_null<Main::Session*> session) {
	return "telewhite-exclude-chats-"
		+ std::to_string(session->userId().bare);
}

[[nodiscard]] auto ChatsCache()
-> base::flat_map<uint64, base::flat_set<uint64>> & {
	static auto result = base::flat_map<uint64, base::flat_set<uint64>>();
	return result;
}

[[nodiscard]] rpl::event_stream<> &ChatsChanges() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] const base::flat_set<uint64> &Chats(
		not_null<Main::Session*> session) {
	auto &cache = ChatsCache();
	const auto userId = session->userId().bare;
	if (const auto i = cache.find(userId); i != end(cache)) {
		return i->second;
	}
	auto ids = base::flat_set<uint64>();
	const auto serialized = Core::App().settings().readPref<QByteArray>(
		ChatsKey(session));
	for (const auto &part : serialized.split(',')) {
		auto ok = false;
		const auto id = part.toULongLong(&ok);
		if (ok && id) {
			ids.emplace(uint64(id));
		}
	}
	return cache.emplace(userId, std::move(ids)).first->second;
}

[[nodiscard]] double Opacity(Option option) {
	return 1. - std::clamp(OptionNow(option), 0, 90) / 100.;
}

} // namespace

bool ExcludedChat(not_null<History*> history) {
	const auto peer = history->peer;
	if (ExcludedPeer(peer)) {
		return true;
	}
	if (Enabled(Mod::ExcludeGroups)
		&& (peer->isChat() || peer->isMegagroup())) {
		return true;
	} else if (Enabled(Mod::ExcludeChannels) && peer->isBroadcast()) {
		return true;
	} else if (Enabled(Mod::ExcludeBots)) {
		if (const auto user = peer->asUser(); user && user->isBot()) {
			return true;
		}
	}
	const auto &folders = Folders(&history->session());
	if (folders.empty()) {
		return false;
	}
	for (const auto &filter : history->owner().chatsFilters().list()) {
		if (filter.id()
			&& folders.contains(filter.id())
			&& filter.contains(history)) {
			return true;
		}
	}
	return false;
}

bool KeepDeletedIn(not_null<History*> history) {
	return Enabled(Mod::SaveDeleted) && !ExcludedChat(history);
}

bool KeepEditsIn(not_null<History*> history) {
	return Enabled(Mod::EditHistory)
		&& (Enabled(Mod::EditsInExcluded) || !ExcludedChat(history));
}

bool KeepDeleted(not_null<HistoryItem*> item) {
	return item->isRegular()
		&& !item->isService()
		&& !HasSelfDestructMedia(item)
		&& KeepDeletedIn(item->history());
}

bool KeepExpired(not_null<HistoryItem*> item) {
	return Enabled(Mod::KeepTimerMessages)
		&& item->isRegular()
		&& !item->isService()
		&& !HasSelfDestructMedia(item)
		&& !ExcludedChat(item->history());
}

bool KeepOwnOnDelete(not_null<HistoryItem*> item, bool revoke) {
	const auto history = item->history();
	const auto peer = history->peer;
	return Enabled(Mod::KeepOwnDeleted)
		&& item->out()
		&& item->isRegular()
		&& !item->isService()
		&& !peer->isSelf()
		&& !HasSelfDestructMedia(item)
		&& (revoke || peer->isChannel())
		&& !IsMarkedDeleted(item)
		&& !ExcludedChat(history);
}

void KeepAsDeleted(not_null<HistoryItem*> item) {
	if (!MarkDeleted(item)) {
		return;
	}
	RememberDeleted(item);

	// The message is painted faded with a trash can near the time.
	const auto owner = &item->history()->owner();
	owner->notifyItemDataChange(item);
	owner->requestItemRepaint(item);
}

double DeletedOpacity() {
	return Opacity(Option::DeletedTransparency);
}

base::flat_set<FilterId> ExcludedFolders(not_null<Main::Session*> session) {
	return Folders(session);
}

void SetExcludedFolders(
		not_null<Main::Session*> session,
		const base::flat_set<FilterId> &ids) {
	auto parts = QByteArrayList();
	for (const auto id : ids) {
		parts.push_back(QByteArray::number(id));
	}
	Core::App().settings().writePref<QByteArray>(
		FoldersKey(session),
		parts.join(','));
	Core::App().saveSettingsDelayed();
	FoldersCache()[session->userId().bare] = ids;
}

bool ExcludedPeer(not_null<PeerData*> peer) {
	return Chats(&peer->session()).contains(SerializePeerId(peer->id));
}

void SetExcludedPeer(not_null<PeerData*> peer, bool excluded) {
	const auto session = &peer->session();
	auto ids = Chats(session);
	const auto id = uint64(SerializePeerId(peer->id));
	if (excluded) {
		ids.emplace(id);
	} else {
		ids.remove(id);
	}
	auto parts = QByteArrayList();
	for (const auto value : ids) {
		parts.push_back(QByteArray::number(qulonglong(value)));
	}
	Core::App().settings().writePref<QByteArray>(
		ChatsKey(session),
		parts.join(','));
	Core::App().saveSettingsDelayed();
	ChatsCache()[session->userId().bare] = std::move(ids);
	ChatsChanges().fire({});
}

std::vector<not_null<PeerData*>> ExcludedPeers(
		not_null<Main::Session*> session) {
	auto result = std::vector<not_null<PeerData*>>();
	for (const auto value : Chats(session)) {
		const auto id = DeserializePeerId(quint64(value));
		if (const auto peer = session->data().peerLoaded(id)) {
			result.push_back(peer);
		}
	}
	return result;
}

rpl::producer<> ExcludedPeersChanges() {
	return ChatsChanges().events();
}

void AddExcludeChatAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!Enabled(Mod::SaveDeleted)
		&& !Enabled(Mod::EditHistory)
		&& !Enabled(Mod::KeepOwnDeleted)
		&& !Enabled(Mod::KeepTimerMessages)) {
		return;
	}
	const auto excluded = ExcludedPeer(peer);
	const auto window = base::make_weak(controller);
	addAction(excluded
		? tr::lng_telewhite_exclude_chat_off(tr::now)
		: tr::lng_telewhite_exclude_chat_on(tr::now), [=] {
		SetExcludedPeer(peer, !excluded);
		if (const auto strong = window.get()) {
			strong->showToast(excluded
				? tr::lng_telewhite_exclude_chat_off_done(tr::now)
				: tr::lng_telewhite_exclude_chat_on_done(tr::now));
		}
	}, excluded ? &st::menuIconRestore : &st::menuIconDelete);
}

TextWithEntities WithEditVersions(
		not_null<HistoryItem*> item,
		TextWithEntities text) {
	if (!Enabled(Mod::EditHistory)
		|| !Enabled(Mod::EditHistoryInline)
		|| !item->isRegular()) {
		return text;
	}
	const auto versions = EditVersions(item);
	auto previous = text.text;
	auto shown = 0;
	for (auto i = int(versions.size()); i != 0 && shown < kMaxInlineVersions;) {
		const auto &version = versions[--i].text;
		if (version.text.trimmed().isEmpty() || version.text == previous) {
			continue;
		}
		previous = version.text;
		++shown;

		text.text.append(QChar('\n'));
		const auto offset = int(text.text.size());
		text.entities.push_back(EntityInText(
			EntityType::Colorized,
			offset,
			int(version.text.size()),
			QString(QChar(kVersionsColorData))));
		for (auto entity : version.entities) {
			entity.shiftRight(offset);
			text.entities.push_back(entity);
		}
		text.text.append(version.text);
	}
	return text;
}

std::span<Ui::Text::SpecialColor> WithVersionsColor(
		std::span<Ui::Text::SpecialColor> colors,
		const QColor &fg) {
	if (!Enabled(Mod::EditHistory) || !Enabled(Mod::EditHistoryInline)) {
		return colors;
	}
	static auto Pen = QPen();
	static auto Storage = std::vector<Ui::Text::SpecialColor>();
	auto color = fg;
	color.setAlphaF(color.alphaF() * Opacity(Option::VersionsTransparency));
	Pen = QPen(color);
	Storage.clear();
	for (auto i = 0; i != kVersionsColorIndex; ++i) {
		Storage.push_back((i < int(colors.size()))
			? colors[i]
			: Ui::Text::SpecialColor{ &Pen, &Pen });
	}
	Storage.push_back({ &Pen, &Pen });
	return Storage;
}

} // namespace Telewhite
