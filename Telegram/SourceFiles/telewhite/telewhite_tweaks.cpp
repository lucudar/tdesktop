/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_tweaks.h"

#include "api/api_blocked_peers.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "boxes/abstract_box.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "telewhite/telewhite_mods.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/outline_segments.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/style/style_core_scale.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#endif // Q_OS_WIN

namespace Telewhite {
namespace {

// Message is converted only if most of its letters are capital.
constexpr auto kMinCapsLetters = 4;
constexpr auto kMinCapsPart = 0.7;

// The text is sent right after its send action is prepared.
constexpr auto kMentionFreshFor = crl::time(1000);

struct PendingMention {
	TextWithEntities text;
	crl::time when = 0;
};

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

[[nodiscard]] PendingMention &Pending() {
	static auto result = PendingMention();
	return result;
}

// @username of the author or a link to him by the name.
[[nodiscard]] TextWithEntities MentionOf(not_null<PeerData*> from) {
	const auto username = from->username();
	if (!username.isEmpty()) {
		const auto text = u"@"_q + username;
		auto result = TextWithEntities{ text };
		result.entities.push_back(
			EntityInText(EntityType::Mention, 0, int(text.size())));
		return result;
	}
	const auto user = from->asUser();
	if (!user || user->isInaccessible()) {
		return TextWithEntities();
	}
	const auto name = user->firstName.isEmpty()
		? user->name()
		: user->firstName;
	if (name.isEmpty()) {
		return TextWithEntities();
	}
	auto fields = TextUtilities::MentionNameFields();
	fields.selfId = user->session().userId().bare;
	fields.userId = peerToUser(user->id).bare;
	fields.accessHash = user->accessHash();
	auto result = TextWithEntities{ name };
	result.entities.push_back(EntityInText(
		EntityType::MentionName,
		0,
		int(name.size()),
		TextUtilities::MentionNameDataFromFields(fields)));
	return result;
}

// The server doesn't accept a reply to a message deleted there: the reply
// goes without it (staying in its topic) and the text sent right after
// starts with a mention of the author of the deleted message.
void ApplyDeletedReply(Api::SendAction &action) {
	auto &replyTo = action.replyTo;
	if (!replyTo.messageId) {
		return;
	}
	const auto history = action.history;
	const auto item = history->owner().message(replyTo.messageId);
	if (!item || !IsMarkedDeleted(item)) {
		return;
	}
	replyTo.messageId = replyTo.topicRootId
		? FullMsgId(history->peer->id, replyTo.topicRootId)
		: FullMsgId();
	replyTo.quote = TextWithEntities();
	replyTo.quoteOffset = 0;
	replyTo.todoItemId = 0;
	replyTo.pollOption = QByteArray();
	replyTo.richQuote = nullptr;
	const auto from = item->from();
	if (!Enabled(Mod::MentionDeletedReply) || from->isSelf()) {
		return;
	}
	auto mention = MentionOf(from);
	if (!mention.empty()) {
		auto &pending = Pending();
		pending.text = std::move(mention);
		pending.when = crl::now();
	}
}

void ApplyPendingMention(TextWithEntities &text) {
	auto &pending = Pending();
	if (pending.text.empty()) {
		return;
	}
	auto mention = base::take(pending.text);
	const auto when = base::take(pending.when);
	if (crl::now() - when > kMentionFreshFor) {
		return;
	}
	mention.append(u" "_q);
	mention.append(text);
	text = std::move(mention);
}

} // namespace

void ApplyAntiCaps(TextWithEntities &text) {
	ApplyPendingMention(text);
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
	ApplyDeletedReply(action);

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

void ApplyStyleTweaks() {
	static auto applied = false;
	if (applied || !Enabled(Mod::CompactChats)) {
		return;
	}
	applied = true;

	// What the compact row has less than the usual one, at 100% scale.
	struct Shift {
		int height = 0;
		int photoTop = 0;
		int nameTop = 0;
		int textTop = 0;
		int tagTop = 0;
	};
	const auto compact = [](const style::DialogRow &base, Shift shift) {
		// The styles are filled once on the start, changing them right
		// after that is the same as other values in the .style files.
		auto &row = const_cast<style::DialogRow&>(base);
		const auto scaled = [](int value) {
			return style::ConvertScale(value);
		};
		row.photoSize -= scaled(8);
		row.nameLeft -= scaled(10);
		row.textLeft -= scaled(10);
		row.height -= scaled(shift.height);
		row.padding.setTop(row.padding.top() - scaled(shift.photoTop));
		row.nameTop -= scaled(shift.nameTop);
		row.textTop -= scaled(shift.textTop);
		row.tagTop -= scaled(shift.tagTop);
	};
	compact(st::defaultDialogRow, {
		.height = 12,
		.photoTop = 2,
		.nameTop = 4,
		.textTop = 8,
	});
	compact(st::taggedDialogRow, {
		.height = 10,
		.photoTop = 2,
		.nameTop = 4,
		.textTop = 6,
		.tagTop = 8,
	});

	// Forums keep their height for the topics line, the userpics match.
	compact(st::forumDialogRow, {});
	compact(st::taggedForumDialogRow, {});

	// The search results and the list paddings count with this one.
	const_cast<int&>(st::dialogsRowHeight) = st::defaultDialogRow.height;
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

namespace {

// The "joined Telegram" service messages, marked when they are made.
using JoinedKey = std::pair<uint64, FullMsgId>;

[[nodiscard]] base::flat_set<JoinedKey> &JoinedMessages() {
	static auto result = base::flat_set<JoinedKey>();
	return result;
}

[[nodiscard]] JoinedKey JoinedKeyOf(not_null<const HistoryItem*> item) {
	return { item->history()->session().uniqueId(), item->fullId() };
}

// The glass chat list: the hovered and the opened chats are floating
// rounded capsules, the rest of the row shows the list background.
constexpr auto kGlassRowSkipX = 6;
constexpr auto kGlassRowSkipY = 2;
constexpr auto kGlassRowRadius = 12;

[[nodiscard]] bool GlassRowsShown() {
	return Enabled(Mod::GlassUi) && Enabled(Mod::GlassChats);
}

[[nodiscard]] QRect GlassRowPill(QRect geometry) {
	const auto skipX = style::ConvertScale(kGlassRowSkipX);
	const auto skipY = style::ConvertScale(kGlassRowSkipY);
	return geometry.marginsRemoved({ skipX, skipY, skipX, skipY });
}

// The server tells who is blocked only in the list of the blocked,
// it is read once on the start, page by page.
void LoadBlocked(not_null<Main::Session*> session, int offset) {
	const auto weak = base::make_weak(session);
	session->api().blockedPeers().request(offset, [=](
			const Api::BlockedPeers::Slice &slice) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		for (const auto &entry : slice.list) {
			if (const auto peer = strong->data().peerLoaded(entry.id)) {
				peer->setIsBlocked(true);
			}
		}
		const auto next = offset + int(slice.list.size());
		if (!slice.list.empty() && next < slice.total) {
			LoadBlocked(strong, next);
		}
	});
}

} // namespace

bool ConfirmOutgoingCall(
		not_null<UserData*> user,
		bool video,
		Fn<void()> start) {
	static auto confirmed = false;
	if (base::take(confirmed) || !Enabled(Mod::ConfirmCalls)) {
		return false;
	}
	const auto name = user->name();
	Ui::show(Ui::MakeConfirmBox({
		.text = (video
			? (u"Начать видеозвонок с "_q + name + u"?"_q)
			: (u"Позвонить "_q + name + u"?"_q)),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			confirmed = true;
			start();
		},
		.confirmText = (video ? u"Видеозвонок"_q : u"Позвонить"_q),
	}));
	return true;
}

bool HiddenFromBlocked(not_null<const HistoryItem*> item) {
	if (!Enabled(Mod::HideBlocked) || item->out()) {
		return false;
	}
	const auto peer = item->history()->peer;
	if (peer->isUser()) {
		return false;
	}
	const auto from = item->from();
	return (from != peer) && from->isBlocked();
}

void MarkContactSignUp(not_null<const HistoryItem*> item) {
	JoinedMessages().emplace(JoinedKeyOf(item));
}

void SetupSessionMods(not_null<Main::Session*> session) {
	if (Enabled(Mod::HideBlocked)) {
		LoadBlocked(session, 0);
	}
	session->data().newItemAdded(
	) | rpl::filter([](not_null<HistoryItem*> item) {
		return Enabled(Mod::DeleteJoined)
			&& JoinedMessages().contains(JoinedKeyOf(item));
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		JoinedMessages().remove(JoinedKeyOf(item));
		const auto id = item->fullId();
		crl::on_main(session.get(), [=] {
			if (session->data().message(id)) {
				session->data().histories().deleteMessages(
					MessageIdsList{ id },
					false);
				session->data().sendHistoryChangeNotifications();
			}
		});
	}, session->lifetime());
}

bool PaintGlassRow(
		QPainter &p,
		QRect geometry,
		const QBrush &bg,
		const QBrush &listBg,
		bool highlighted,
		bool active) {
	if (!GlassRowsShown()) {
		return false;
	}
	p.fillRect(geometry, listBg);
	if (!highlighted) {
		return true;
	}
	const auto pill = QRectF(GlassRowPill(geometry));
	const auto radius = float64(style::ConvertScale(kGlassRowRadius));
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(pill, radius, radius);
	auto shine = QLinearGradient(pill.topLeft(), pill.bottomLeft());
	shine.setColorAt(0., QColor(255, 255, 255, active ? 46 : 22));
	shine.setColorAt(0.55, QColor(255, 255, 255, 0));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.setBrush(QBrush(shine));
	p.drawRoundedRect(pill, radius, radius);
	if (active) {
		const auto line = style::ConvertScaleExact(1.);
		const auto half = line / 2.;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, 52), line));
		p.drawRoundedRect(
			pill.marginsRemoved(QMarginsF(half, half, half, half)),
			radius - half,
			radius - half);
	}
	return true;
}

QPainterPath GlassRowClip(QRect geometry) {
	auto result = QPainterPath();
	if (GlassRowsShown()) {
		const auto radius = float64(style::ConvertScale(kGlassRowRadius));
		result.addRoundedRect(QRectF(GlassRowPill(geometry)), radius, radius);
	}
	return result;
}

namespace {

// A section of the glass profile: a rounded card with a thin rim, the
// content is inset so the rounded corners stay visible around it.
constexpr auto kCardMargin = 12;
constexpr auto kCardInset = 4;
constexpr auto kCardRadius = 12;

class ProfileCard final : public Ui::RpWidget {
public:
	ProfileCard(QWidget *parent, object_ptr<Ui::RpWidget> content);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const object_ptr<Ui::RpWidget> _content;

};

ProfileCard::ProfileCard(QWidget *parent, object_ptr<Ui::RpWidget> content)
: RpWidget(parent)
, _content(std::move(content)) {
	_content->setParent(this);
	_content->show();
	_content->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		resizeToWidth(width());
	}, lifetime());
}

int ProfileCard::resizeGetHeight(int newWidth) {
	const auto margin = style::ConvertScale(kCardMargin);
	const auto inset = style::ConvertScale(kCardInset);
	const auto inner = std::max(newWidth - 2 * (margin + inset), 1);
	_content->resizeToWidth(inner);
	_content->moveToLeft(margin + inset, inset, newWidth);
	const auto height = _content->height();
	return (height > 0) ? (height + 2 * inset) : 0;
}

void ProfileCard::paintEvent(QPaintEvent *) {
	if (_content->height() <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto margin = style::ConvertScale(kCardMargin);
	const auto radius = float64(style::ConvertScale(kCardRadius));
	const auto card = QRectF(rect().marginsRemoved({ margin, 0, margin, 0 }));
	const auto bg = st::windowBg->c;
	const auto dark = (bg.lightnessF() < 0.5);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(card, radius, radius);
	auto shine = QLinearGradient(card.topLeft(), card.bottomLeft());
	shine.setColorAt(0., QColor(255, 255, 255, dark ? 14 : 40));
	shine.setColorAt(0.35, QColor(255, 255, 255, 0));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.setBrush(QBrush(shine));
	p.drawRoundedRect(card, radius, radius);
	const auto line = style::ConvertScaleExact(1.);
	const auto half = line / 2.;
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(dark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 16), line));
	p.drawRoundedRect(
		card.marginsRemoved(QMarginsF(half, half, half, half)),
		radius - half,
		radius - half);
}

} // namespace

bool StartDictation(not_null<Ui::InputField*> field, bool voice) {
	if (!voice || !Enabled(Mod::Dictation)) {
		return false;
	}
#ifdef Q_OS_WIN
	field->setFocusFast();
	// Windows voice typing writes to the focused field, Win+H opens it.
	crl::on_main(field.get(), [] {
		auto inputs = std::array<INPUT, 4>();
		for (auto &input : inputs) {
			input.type = INPUT_KEYBOARD;
		}
		inputs[0].ki.wVk = VK_LWIN;
		inputs[1].ki.wVk = WORD('H');
		inputs[2].ki.wVk = WORD('H');
		inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
		inputs[3].ki.wVk = VK_LWIN;
		inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(UINT(inputs.size()), inputs.data(), int(sizeof(INPUT)));
	});
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

bool GlassProfileShown() {
	return Enabled(Mod::GlassUi) && Enabled(Mod::GlassProfile);
}

object_ptr<Ui::RpWidget> WrapProfileCard(
		not_null<QWidget*> parent,
		object_ptr<Ui::RpWidget> content) {
	return object_ptr<ProfileCard>(parent.get(), std::move(content));
}

void PaintGlassButtonShine(QPainter &p, QRect rect, int radius) {
	if (!GlassProfileShown() || rect.isEmpty()) {
		return;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto shape = QRectF(rect);
	const auto rounding = float64(radius);
	auto shine = QLinearGradient(shape.topLeft(), shape.bottomLeft());
	shine.setColorAt(0., QColor(255, 255, 255, 56));
	shine.setColorAt(0.5, QColor(255, 255, 255, 0));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(QBrush(shine));
	p.drawRoundedRect(shape, rounding, rounding);
	const auto line = style::ConvertScaleExact(1.);
	const auto half = line / 2.;
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 60), line));
	p.drawRoundedRect(
		shape.marginsRemoved(QMarginsF(half, half, half, half)),
		rounding - half,
		rounding - half);
	p.restore();
}

} // namespace Telewhite
