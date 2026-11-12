/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_tweaks.h"

#include "api/api_common.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "telewhite/telewhite_mods.h"
#include "ui/effects/outline_segments.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Telewhite {
namespace {

// Message is converted only if most of its letters are capital.
constexpr auto kMinCapsLetters = 4;
constexpr auto kMinCapsPart = 0.7;

[[nodiscard]] bool KeepsCase(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::CustomUrl:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::MentionName:
	case EntityType::BotCommand:
	case EntityType::CustomEmoji:
	case EntityType::FormattedDate:
	case EntityType::Code:
	case EntityType::Pre: return true;
	default: return false;
	}
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

void ApplyAntiCaps(TextWithEntities &text) {
	if (!Enabled(Mod::AntiCaps)) {
		return;
	}
	auto &string = text.text;
	const auto length = int(string.size());
	auto keep = std::vector<bool>(length, false);
	for (const auto &entity : text.entities) {
		if (!KeepsCase(entity.type())) {
			continue;
		}
		const auto from = std::clamp(entity.offset(), 0, length);
		const auto till = std::clamp(
			entity.offset() + entity.length(),
			from,
			length);
		for (auto i = from; i != till; ++i) {
			keep[i] = true;
		}
	}
	auto letters = 0;
	auto capitals = 0;
	for (auto i = 0; i != length; ++i) {
		const auto ch = string[i];
		if (!keep[i] && ch.isLetter()) {
			++letters;
			if (ch.isUpper()) {
				++capitals;
			}
		}
	}
	if (letters < kMinCapsLetters || capitals < letters * kMinCapsPart) {
		return;
	}
	auto capitalize = true;
	auto sentenceEnd = false;
	for (auto i = 0; i != length; ++i) {
		const auto ch = string[i];
		if (keep[i]) {
			capitalize = sentenceEnd = false;
		} else if (ch.isLetter()) {
			string[i] = capitalize ? ch.toUpper() : ch.toLower();
			capitalize = sentenceEnd = false;
		} else if (ch == QChar('\n')) {
			capitalize = true;
			sentenceEnd = false;
		} else if (ch == QChar('.')
			|| ch == QChar('!')
			|| ch == QChar('?')
			|| ch == QChar(0x2026)) {
			sentenceEnd = true;
		} else if (ch.isSpace()) {
			if (sentenceEnd) {
				capitalize = true;
				sentenceEnd = false;
			}
		} else if (ch.isDigit()) {
			capitalize = sentenceEnd = false;
		}
	}
}

void PaintAuthorOnline(
		QPainter &p,
		not_null<PeerData*> from,
		int x,
		int y,
		int size) {
	if (!Enabled(Mod::AuthorOnline)) {
		return;
	}
	const auto user = from->asUser();
	if (!user
		|| user->isBot()
		|| user->isSelf()
		|| user->isInaccessible()
		|| !Data::IsUserOnline(user)) {
		return;
	}
	const auto stroke = std::max(style::ConvertScaleExact(1.5), 1.);
	const auto radius = style::ConvertScaleExact(4.5);
	const auto center = QPointF(
		x + size - radius + stroke * 0.5,
		y + size - radius + stroke * 0.5);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(st::msgInBg->c, stroke));
	p.setBrush(st::dialogsOnlineBadgeFg);
	p.drawEllipse(center, radius, radius);
}

void SetupAuthorOnline(
		not_null<Ui::RpWidget*> widget,
		not_null<Main::Session*> session,
		Fn<bool(not_null<PeerData*>)> shown) {
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return Enabled(Mod::AuthorOnline) && shown(update.peer);
	}) | rpl::on_next([=] {
		widget->update();
	}, widget->lifetime());
}

const std::vector<StoryRingColor> &StoryRingColors() {
	static const auto result = std::vector<StoryRingColor>{
		{ .name = U("Стандартный") },
		{ .name = U("Цвет темы"), .theme = true },
		{ U("Фиолетовый"), QColor(0xb1, 0x8c, 0xff), QColor(0x6a, 0x3d, 0xe8) },
		{ U("Розовый"), QColor(0xff, 0x8a, 0xc8), QColor(0xe0, 0x36, 0x8c) },
		{ U("Красный"), QColor(0xff, 0x7a, 0x6b), QColor(0xe0, 0x2f, 0x3c) },
		{ U("Оранжевый"), QColor(0xff, 0xc1, 0x5e), QColor(0xff, 0x6f, 0x1a) },
		{ U("Золотой"), QColor(0xff, 0xe0, 0x7a), QColor(0xe0, 0xa3, 0x00) },
		{ U("Зелёный"), QColor(0x8e, 0xe8, 0x7a), QColor(0x1f, 0xaa, 0x5c) },
		{ U("Бирюзовый"), QColor(0x6e, 0xf0, 0xd8), QColor(0x10, 0x9e, 0xb5) },
		{ U("Голубой"), QColor(0x7c, 0xd0, 0xff), QColor(0x2a, 0x7c, 0xe0) },
	};
	return result;
}

int StoryRingsKey() {
	return OptionNow(Option::StoryRingColor) + 1;
}

void SetupStoryRings(rpl::lifetime &lifetime) {
	const auto apply = [] {
		const auto &list = StoryRingColors();
		const auto index = OptionNow(Option::StoryRingColor);
		if (index <= 0 || index >= int(list.size())) {
			Ui::SetUnreadStoryOutlineOverride(std::nullopt);
			return;
		}
		const auto &color = list[index];
		if (color.theme) {
			const auto accent = st::windowBgActive->c;
			Ui::SetUnreadStoryOutlineOverride(std::make_pair(
				accent.lighter(135),
				accent));
		} else {
			Ui::SetUnreadStoryOutlineOverride(std::make_pair(
				color.from,
				color.till));
		}
	};
	rpl::merge(
		OptionValue(Option::StoryRingColor) | rpl::to_empty,
		style::PaletteChanged()
	) | rpl::on_next(apply, lifetime);
}

void ApplyGhostSchedule(Api::SendAction &action) {
	// Telegram doesn't accept the schedule date too close to now.
	constexpr auto kDelay = TimeId(12);
	if (!Enabled(Mod::GhostOnline) || !Enabled(Mod::GhostScheduled)) {
		return;
	}
	const auto &options = action.options;
	if (options.scheduled
		|| options.shortcutId
		|| options.price
		|| options.starsApproved
		|| options.suggest.exists
		|| action.replaceMediaOf) {
		return;
	}
	const auto peer = action.history->peer;
	if (peer->isSelf()
		|| peer->isBot()
		|| peer->isMonoforum()
		|| peer->isRepliesChat()
		|| peer->isVerifyCodes()
		|| peer->starsPerMessageChecked() > 0) {
		return;
	}
	action.options.scheduled = base::unixtime::now() + kDelay;
}

bool FolderTagsShown(bool serverEnabled) {
	// Read once: the colors are given to the chats when they load.
	static const auto local = Enabled(Mod::FolderTags);
	return serverEnabled || local;
}

std::optional<uint8> FolderTagColor(const Data::ChatFilter &filter) {
	constexpr auto kColorsCount = 7;
	if (const auto color = filter.colorIndex()) {
		return color;
	} else if (!filter.id() || !FolderTagsShown(false)) {
		return std::nullopt;
	}
	return uint8(std::abs(int(filter.id())) % kColorsCount);
}

} // namespace Telewhite
