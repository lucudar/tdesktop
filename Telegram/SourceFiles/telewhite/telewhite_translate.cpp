/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_translate.h"
#include <QtCore/QPointer>
#include <QtGui/QAction>
#include <QtWidgets/QMenu>
#include "chat_helpers/compose/compose_show.h"
#include "lang/lang_keys.h"
#include "lang/translate_provider.h"
#include "ui/widgets/fields/input_field.h"

#include "main/main_session.h"
#include "telewhite/telewhite_mods.h"
#include "translate_provider.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Telewhite {
namespace {

constexpr auto kTimeout = 30 * 1000;

// Response: [[["translated", "original", ...], ...], null, "en", ...].
[[nodiscard]] std::optional<QString> ParseResponse(const QByteArray &body) {
	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !parsed.isArray()) {
		return std::nullopt;
	}
	const auto root = parsed.array();
	if (root.isEmpty() || !root[0].isArray()) {
		return std::nullopt;
	}
	auto result = QString();
	for (const auto &segment : root[0].toArray()) {
		const auto parts = segment.toArray();
		if (!parts.isEmpty() && parts[0].isString()) {
			result += parts[0].toString();
		}
	}
	if (result.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return result;
}

class GoogleTranslateProvider final : public Ui::TranslateProvider {
public:
	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
		Ui::TranslateProviderRequest request,
		LanguageId to,
		Fn<void(Ui::TranslateProviderResult)> done) override;

private:
	QNetworkAccessManager _network;

};

void GoogleTranslateProvider::request(
		Ui::TranslateProviderRequest request,
		LanguageId to,
		Fn<void(Ui::TranslateProviderResult)> done) {
	const auto &text = request.text.text;
	const auto code = to.twoLetterCode();
	if (text.trimmed().isEmpty() || code.isEmpty()) {
		done({ .error = Ui::TranslateProviderError::Unknown });
		return;
	}
	auto url = QUrl(u"https://translate.googleapis.com/translate_a/single"_q
		+ u"?client=gtx&sl=auto&dt=t&tl="_q
		+ QString::fromLatin1(QUrl::toPercentEncoding(code)));
	auto networkRequest = QNetworkRequest(url);
	networkRequest.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/x-www-form-urlencoded;charset=UTF-8"_q);
	// Static Qt 6.8+ keeps the int overload out of line next to the
	// QDnsLookup code, which needs dnsapi.lib: use the chrono one there.
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	networkRequest.setTransferTimeout(std::chrono::milliseconds(kTimeout));
#else // Qt >= 6.7
	networkRequest.setTransferTimeout(kTimeout);
#endif // Qt >= 6.7
	const auto body = "q=" + QUrl::toPercentEncoding(text);
	const auto reply = _network.post(networkRequest, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		auto result = Ui::TranslateProviderResult();
		if (reply->error() != QNetworkReply::NoError) {
			result.error = Ui::TranslateProviderError::Unknown;
		} else if (const auto parsed = ParseResponse(reply->readAll())) {
			result.text = TextWithEntities{ *parsed };
		} else {
			result.error = Ui::TranslateProviderError::Unknown;
		}
		reply->deleteLater();
		done(std::move(result));
	});
}

} // namespace

bool UseGoogleTranslate(not_null<Main::Session*> session) {
	return (TranslatorKind(OptionNow(Option::Translator))
			== TranslatorKind::Google)
		|| (Enabled(Mod::TranslateChats) && !session->premium());
}

std::unique_ptr<Ui::TranslateProvider> CreateGoogleTranslateProvider() {
	return std::make_unique<GoogleTranslateProvider>();
}

namespace {

constexpr auto kSendLanguages = std::array{
	QLocale::English,
	QLocale::Russian,
	QLocale::Ukrainian,
	QLocale::German,
	QLocale::Spanish,
	QLocale::French,
	QLocale::Italian,
	QLocale::Portuguese,
	QLocale::Turkish,
	QLocale::Polish,
	QLocale::Chinese,
	QLocale::Japanese,
	QLocale::Korean,
	QLocale::Arabic,
};

// "Translate to <language>", accusative case.
[[nodiscard]] QString SendLanguageName(int index) {
	switch (index) {
	case 0: return u"английский"_q;
	case 1: return u"русский"_q;
	case 2: return u"украинский"_q;
	case 3: return u"немецкий"_q;
	case 4: return u"испанский"_q;
	case 5: return u"французский"_q;
	case 6: return u"итальянский"_q;
	case 7: return u"португальский"_q;
	case 8: return u"турецкий"_q;
	case 9: return u"польский"_q;
	case 10: return u"китайский"_q;
	case 11: return u"японский"_q;
	case 12: return u"корейский"_q;
	case 13: return u"арабский"_q;
	}
	return u"английский"_q;
}

[[nodiscard]] int SendLanguageIndex() {
	return std::clamp(
		OptionNow(Option::TranslateSendTo),
		0,
		int(kSendLanguages.size()) - 1);
}

void TranslateField(
		std::shared_ptr<ChatHelpers::Show> show,
		QPointer<Ui::InputField> weak,
		LanguageId to) {
	const auto field = weak.data();
	if (!field) {
		return;
	}
	const auto text = field->getTextWithTags().text;
	if (text.trimmed().isEmpty()) {
		return;
	}
	const auto provider = std::shared_ptr<Ui::TranslateProvider>(
		Ui::CreateTranslateProvider(&show->session()));
	auto request = Ui::TranslateProviderRequest();
	request.text = TextWithEntities{ text };
	provider->request(std::move(request), to, [=](
			Ui::TranslateProviderResult result) {
		// Keeps the provider alive until the answer.
		[[maybe_unused]] const auto keep = provider;
		const auto strong = weak.data();
		if (!strong) {
			return;
		} else if (!result.text || result.text->text.isEmpty()) {
			show->showToast(tr::lng_telewhite_translate_field_failed(tr::now));
			return;
		}
		strong->setTextWithTags(
			{ result.text->text, {} },
			Ui::InputField::HistoryAction::NewEntry);
	});
}

} // namespace

std::vector<QString> SendLanguageNames() {
	auto result = std::vector<QString>();
	for (auto i = 0; i != int(kSendLanguages.size()); ++i) {
		const auto name = SendLanguageName(i);
		result.push_back(name.left(1).toUpper() + name.mid(1));
	}
	return result;
}

void SetupTranslateBeforeSend(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::InputField*> field) {
	const auto weak = QPointer<Ui::InputField>(field.get());
	field->addContextMenuHook([=](Ui::InputField::ContextMenuRequest request) {
		if (!Enabled(Mod::TranslateBeforeSend)) {
			return;
		}
		const auto strong = weak.data();
		if (!strong || strong->getTextWithTags().text.trimmed().isEmpty()) {
			return;
		}
		const auto index = SendLanguageIndex();
		const auto to = LanguageId{ kSendLanguages[index] };
		request.menu->addSeparator();
		const auto action = request.menu->addAction(
			tr::lng_telewhite_translate_field(
				tr::now,
				lt_language,
				SendLanguageName(index)));
		QObject::connect(action, &QAction::triggered, action, [=] {
			TranslateField(show, weak, to);
		});
	});
}

} // namespace Telewhite
