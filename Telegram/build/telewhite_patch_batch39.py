'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 39: the pins kept on this computer survive the reload of the pinned
chats (every start), a pinned chat with unread messages keeps its pin icon,
"Hide ads" hides all the ads: the search ads, the proxy sponsor, the promos
of Telegram and the channel posts marked as ads (erid, #реклама). See
telewhite_patch.py for the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33
import telewhite_patch_batch34
import telewhite_patch_batch35
import telewhite_patch_batch36
import telewhite_patch_batch37
import telewhite_patch_batch38

APIWRAP = 'Telegram/SourceFiles/apiwrap.cpp'
DIALOGS_LAYOUT = 'Telegram/SourceFiles/dialogs/ui/dialogs_layout.cpp'
PEER_SEARCH = 'Telegram/SourceFiles/api/api_peer_search.cpp'
PROMO = 'Telegram/SourceFiles/data/components/promo_suggestions.cpp'
TOP_BAR = 'Telegram/SourceFiles/dialogs/dialogs_top_bar_suggestion.cpp'
TWEAKS = 'Telegram/SourceFiles/telewhite/telewhite_tweaks.cpp'
PREVIEW = 'Telegram/SourceFiles/telewhite/telewhite_mods_preview.cpp'

FEATURES = [
    {
        'id': 'pins-tail-after-reload',
        'title': 'pins: the local pins come back after the pinned chats reload',
        'pieces': [
            {
                'file': APIWRAP,
                'find': r'''			_session->data().clearPinnedChats(folder);
			_session->data().applyDialogs(
				folder,
				data.vmessages().v,
				data.vdialogs().v);
			_session->data().chatsListChanged(folder);''',
                'replace': r'''			_session->data().clearPinnedChats(folder);
			_session->data().applyDialogs(
				folder,
				data.vmessages().v,
				data.vdialogs().v);
			// Telewhite: the pins of this computer only are cleared with
			// the server ones on every start, they get their places back.
			Telewhite::RestorePinnedTail(&_session->data(), folder);
			_session->data().chatsListChanged(folder);''',
            },
        ],
    },
    {
        'id': 'pins-icon-with-badge',
        'title': 'pins: the pin icon stays next to the unread counter',
        'pieces': [
            {
                'file': DIALOGS_LAYOUT,
                'find': r'''	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = context.st->textTop;
		const auto availableWidth = PaintWideCounter(
			p,
			context,
			badgesState,
			texttop,
			namewidth,
			displayPinnedIcon);
		const auto &color = context.active''',
                'replace': r'''	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = context.st->textTop;
		auto availableWidth = PaintWideCounter(
			p,
			context,
			badgesState,
			texttop,
			namewidth,
			displayPinnedIcon);
		// Telewhite: a pinned chat with unread messages keeps the pin icon,
		// on the left of the counters.
		if (!displayPinnedIcon
			&& !badgesState.empty()
			&& DisplayPinned(entry, context.filter)) {
			const auto &telewhitePin = ThreeStateIcon(
				st::dialogsPinnedIcon,
				context.active,
				context.selected);
			const auto telewhiteSkip = telewhitePin.width()
				+ st::dialogsUnreadPadding;
			if (availableWidth > telewhiteSkip) {
				const auto telewhiteUsed = namewidth - availableWidth;
				telewhitePin.paint(
					p,
					(context.width
						- context.st->padding.right()
						- telewhiteUsed
						- telewhitePin.width()),
					texttop,
					context.width);
				availableWidth -= telewhiteSkip;
			}
		}
		const auto &color = context.active''',
            },
        ],
    },
    {
        'id': 'ads-search',
        'title': 'hide ads: no sponsored chats in the search',
        'pieces': [
            {
                'file': PEER_SEARCH,
                'find': r'''#include "api/api_single_message_search.h"''',
                'replace': r'''#include "api/api_single_message_search.h"
#include "telewhite/telewhite_mods.h"''',
            },
            {
                'file': PEER_SEARCH,
                'find': r'''	if (_type != Type::WithSponsored
		|| _query.size() < kMinSponsoredQueryLength) {''',
                'replace': r'''	if (_type != Type::WithSponsored
		|| _query.size() < kMinSponsoredQueryLength
		|| Telewhite::Enabled(Telewhite::Mod::HideSponsored)) {''',
            },
        ],
    },
    {
        'id': 'ads-proxy-sponsor',
        'title': 'hide ads: no proxy sponsor on the top of the chats list',
        'pieces': [
            {
                'file': PROMO,
                'find': r'''#include "api/api_text_entities.h"''',
                'replace': r'''#include "api/api_text_entities.h"
#include "telewhite/telewhite_mods.h"''',
            },
            {
                'file': PROMO,
                'find': r'''		refreshTopPromotion();
	}, _lifetime);
}

PromoSuggestions::~PromoSuggestions() = default;''',
                'replace': r'''		refreshTopPromotion();
	}, _lifetime);

	// Telewhite: "Hide ads" hides the chat on the top of the list too,
	// the proxy sponsor and the announcements.
	Telewhite::EnabledValue(
		Telewhite::Mod::HideSponsored
	) | rpl::skip(1) | rpl::on_next([=](bool telewhiteHide) {
		if (telewhiteHide) {
			setTopPromoted(nullptr, QString(), QString());
		} else {
			_topPromotionNextRequestTime = 0;
			refreshTopPromotion();
		}
	}, _lifetime);
}

PromoSuggestions::~PromoSuggestions() = default;''',
            },
            {
                'file': PROMO,
                'find': r'''			if (const auto peer = data.vpeer()) {
				const auto peerId = peerFromMTP(*peer);''',
                'replace': r'''			if (const auto peer = data.vpeer()
				; peer
				&& !Telewhite::Enabled(Telewhite::Mod::HideSponsored)) {
				const auto peerId = peerFromMTP(*peer);''',
            },
        ],
    },
    {
        'id': 'ads-top-promos',
        'title': 'hide ads: no promos of Telegram over the chats list',
        'pieces': [
            {
                'file': TOP_BAR,
                'find': r'''#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"''',
                'replace': r'''#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "telewhite/telewhite_mods.h"''',
            },
            {
                'file': TOP_BAR,
                'find': r'''		const auto specs = lifetime.make_state<std::vector<
			TopBarSuggestions::Spec>>(TopBarSuggestions::AllSpecs());''',
                'replace': r'''		const auto specs = lifetime.make_state<std::vector<
			TopBarSuggestions::Spec>>(TopBarSuggestions::AllSpecs());
		// Telewhite: "Hide ads" hides the promos of Telegram itself,
		// "Hide Premium" the offers of Premium.
		for (auto &telewhiteSpec : *specs) {
			const auto telewhitePriority = telewhiteSpec.priority;
			const auto telewhitePromo = (telewhitePriority
					== TopBarSuggestions::Priority::CustomPromo)
				|| (telewhitePriority
					== TopBarSuggestions::Priority::GiftAuctions)
				|| (telewhitePriority
					== TopBarSuggestions::Priority::PremiumOffer);
			if (!telewhitePromo || !telewhiteSpec.available) {
				continue;
			}
			telewhiteSpec.available = [
				telewhitePriority,
				telewhiteOriginal = std::move(telewhiteSpec.available)
			](const TopBarSuggestions::Context &telewhiteContext) {
				const auto telewhitePremium = (telewhitePriority
					== TopBarSuggestions::Priority::PremiumOffer);
				if (Telewhite::Enabled(Telewhite::Mod::HideSponsored)
					|| (telewhitePremium
						&& Telewhite::Enabled(Telewhite::Mod::HidePremium))) {
					return false;
				}
				return telewhiteOriginal(telewhiteContext);
			};
		}''',
            },
        ],
    },
    {
        'id': 'ads-posts',
        'title': 'hide ads: channel posts marked as ads are hidden',
        'pieces': [
            {
                'file': TWEAKS,
                'find': r'''bool HiddenFromBlocked(not_null<const HistoryItem*> item) {
	if (!Enabled(Mod::HideBlocked) || item->out()) {
		return false;
	}''',
                'replace': r'''namespace {

// "Hide ads": the posts the channels mark as ads themselves, by the
// advertising id (erid) of the Russian ad marking or by a hashtag.
constexpr auto kAdChecksLimit = 20000;

struct AdCheck {
	int length = -1;
	bool ad = false;
};

[[nodiscard]] bool HasAdToken(const QString &text) {
	const auto token = u"erid"_q;
	const auto length = int(text.size());
	const auto size = int(token.size());
	auto from = 0;
	while (from < length) {
		const auto index = int(text.indexOf(token, from, Qt::CaseInsensitive));
		if (index < 0) {
			return false;
		}
		const auto till = index + size;
		const auto before = (index > 0) ? text[index - 1] : QChar(' ');
		const auto after = (till < length) ? text[till] : QChar(' ');
		if (!before.isLetterOrNumber() && !after.isLetterOrNumber()) {
			return true;
		}
		from = index + 1;
	}
	return false;
}

[[nodiscard]] bool IsAdHashtag(QStringView tag) {
	static const auto tags = std::array<QString, 5>{ {
		u"#реклама"_q,
		u"#ad"_q,
		u"#ads"_q,
		u"#промо"_q,
		u"#sponsored"_q,
	} };
	for (const auto &known : tags) {
		if (tag.compare(QStringView(known), Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool LooksLikeAd(const TextWithEntities &text) {
	const auto &string = text.text;
	if (HasAdToken(string)) {
		return true;
	}
	const auto length = int(string.size());
	for (const auto &entity : text.entities) {
		const auto type = entity.type();
		if (type == EntityType::Hashtag) {
			const auto from = std::clamp(entity.offset(), 0, length);
			const auto till = std::clamp(
				entity.offset() + entity.length(),
				from,
				length);
			if (IsAdHashtag(QStringView(string).mid(from, till - from))) {
				return true;
			}
		} else if (type == EntityType::CustomUrl
			&& HasAdToken(entity.data())) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool CachedLooksLikeAd(not_null<const HistoryItem*> item) {
	using Key = std::pair<uint64, FullMsgId>;
	static auto checks = std::map<Key, AdCheck>();
	const auto &text = item->originalText();
	const auto length = int(text.text.size());
	const auto key = Key(
		item->history()->session().uniqueId(),
		item->fullId());
	auto i = checks.find(key);
	if (i == checks.end()) {
		if (int(checks.size()) >= kAdChecksLimit) {
			checks.clear();
		}
		i = checks.emplace(key, AdCheck()).first;
	}
	if (i->second.length != length) {
		i->second.length = length;
		i->second.ad = LooksLikeAd(text);
	}
	return i->second.ad;
}

[[nodiscard]] bool HiddenAdPost(not_null<const HistoryItem*> item) {
	if (!Enabled(Mod::HideSponsored)
		|| item->out()
		|| item->isService()
		|| !item->history()->peer->isBroadcast()) {
		return false;
	}
	const auto &groups = item->history()->owner().groups();
	if (const auto group = groups.find(item)) {
		return ranges::any_of(group->items, [](not_null<HistoryItem*> part) {
			return CachedLooksLikeAd(part);
		});
	}
	return CachedLooksLikeAd(item);
}

} // namespace

bool HiddenFromBlocked(not_null<const HistoryItem*> item) {
	// "Hide ads": the ad posts of the channels are hidden the same way.
	if (HiddenAdPost(item)) {
		return true;
	} else if (!Enabled(Mod::HideBlocked) || item->out()) {
		return false;
	}''',
            },
        ],
    },
    {
        'id': 'ads-texts',
        'title': 'hide ads: the description of what is hidden',
        'pieces': [
            {
                'file': PREVIEW,
                'find': r'''	case Scene::HideSponsored: return U("Скрыть рекламу в каналах");''',
                'replace': r'''	case Scene::HideSponsored: return U("Скрыть рекламу");''',
            },
            {
                'file': PREVIEW,
                'find': r'''	case Scene::HideSponsored: return U("Спонсорские сообщения в каналах не показываются.");''',
                'replace': r'''	case Scene::HideSponsored: return U("Нет спонсорских сообщений в каналах и ботах, рекламы в поиске, спонсора прокси, промо Telegram и постов с пометкой erid или #реклама.");''',
            },
        ],
    },
]

_known = {feature['id'] for feature in telewhite_patch.FEATURES}
telewhite_patch.FEATURES.extend(
    feature for feature in FEATURES if feature['id'] not in _known)


if __name__ == '__main__':
    arguments = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    if '--root' in arguments:
        root = arguments[arguments.index('--root') + 1]
    ok = telewhite_patch.run(root, check=('--check' in arguments))
    sys.exit(0 if ok else 1)
