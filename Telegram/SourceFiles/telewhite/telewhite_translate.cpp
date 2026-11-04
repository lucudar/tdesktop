/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_translate.h"

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
	networkRequest.setTransferTimeout(kTimeout);
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

} // namespace Telewhite
