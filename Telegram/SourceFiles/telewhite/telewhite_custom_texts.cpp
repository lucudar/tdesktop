/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_custom_texts.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "lang/lang_values.h"

namespace Telewhite {
namespace {

constexpr auto kPluralForms = ushort(Lang::details::kPluralCount);

struct State {
	// Own texts as typed, by the first index of the phrase.
	base::flat_map<ushort, QString> custom;

	// Language pack values under the own texts, by each index.
	base::flat_map<ushort, QString> original;

	rpl::event_stream<> changes;
	bool applying = false;
};

[[nodiscard]] State &GetState() {
	static auto result = State();
	return result;
}

[[nodiscard]] std::string PrefKey(const QByteArray &key) {
	return "telewhite-text-" + key.toStdString();
}

[[nodiscard]] const CustomText *Find(const QByteArray &key) {
	for (const auto &entry : CustomTexts()) {
		if (entry.key == key) {
			return &entry;
		}
	}
	return nullptr;
}

[[nodiscard]] ushort ValuesCount(ushort index) {
	for (const auto &entry : CustomTexts()) {
		if (entry.index == index) {
			return entry.count;
		}
	}
	return 1;
}

// The language pack keeps tags like {count} as text commands, the own
// text gets them the same way: only the tags of this phrase, each one
// once, anything else stays as typed.
[[nodiscard]] QString PrepareValue(ushort index, const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	auto used = base::flat_set<ushort>();
	auto from = qsizetype(0);
	while (from < text.size()) {
		auto open = text.indexOf(QChar('{'), from);
		if (open < 0) {
			break;
		}
		const auto close = text.indexOf(QChar('}'), open + 1);
		if (close < 0) {
			break;
		}
		open = text.lastIndexOf(QChar('{'), close);
		result.append(text.mid(from, open - from));
		const auto name = text.mid(open + 1, close - open - 1).toLatin1();
		const auto tag = Lang::GetTagIndex(
			QLatin1String(name.constData(), name.size()));
		if (!name.isEmpty()
			&& Lang::IsTagReplaced(index, tag)
			&& !used.contains(tag)) {
			used.emplace(tag);
			auto command = QString(
				Lang::kTagReplacementSize,
				QChar(Lang::kTextCommand));
			command[1] = QChar(Lang::kTextCommandLangTag);
			command[2] = QChar(0x0020 + tag);
			result.append(command);
		} else {
			result.append(text.mid(open, close + 1 - open));
		}
		from = close + 1;
	}
	result.append(text.mid(from));
	return result;
}

// Puts own texts over the language pack, remembering the pack values.
void ApplyAll() {
	auto &state = GetState();
	auto &lang = Lang::GetInstance();
	state.applying = true;
	for (const auto &[first, text] : state.custom) {
		const auto count = ValuesCount(first);
		for (auto i = ushort(0); i != count; ++i) {
			const auto index = ushort(first + i);
			const auto value = PrepareValue(index, text);
			const auto now = lang.getValue(index);
			if (now != value) {
				state.original[index] = now;
				lang.setLocalValue(index, value);
			}
		}
	}
	state.applying = false;
}

void Restore(const CustomText &entry) {
	auto &state = GetState();
	for (auto i = ushort(0); i != entry.count; ++i) {
		const auto index = ushort(entry.index + i);
		const auto j = state.original.find(index);
		if (j != end(state.original)) {
			Lang::GetInstance().setLocalValue(index, j->second);
			state.original.erase(j);
		}
	}
}

} // namespace

const std::vector<CustomText> &CustomTexts() {
	static const auto result = std::vector<CustomText>{
		{ tr::lng_status_online.base, "lng_status_online", u"«в сети»"_q },
		{ tr::lng_status_lastseen_now.base, "lng_status_lastseen_now", u"«был(а) только что»"_q },
		{
			tr::lng_status_lastseen_minutes.base,
			"lng_status_lastseen_minutes",
			u"«был(а) N минут назад»"_q,
			u"Напишите {count} там, где должно стоять число минут. Например: заходил(а) {count} мин назад"_q,
			kPluralForms,
		},
		{
			tr::lng_status_lastseen_hours.base,
			"lng_status_lastseen_hours",
			u"«был(а) N часов назад»"_q,
			u"Напишите {count} там, где должно стоять число часов. Например: заходил(а) {count} ч назад"_q,
			kPluralForms,
		},
		{
			tr::lng_status_lastseen_today.base,
			"lng_status_lastseen_today",
			u"«был(а) сегодня в …»"_q,
			u"Напишите {time} там, где должно стоять время. Например: заходил(а) в {time}"_q,
		},
		{
			tr::lng_status_lastseen_yesterday.base,
			"lng_status_lastseen_yesterday",
			u"«был(а) вчера в …»"_q,
			u"Напишите {time} там, где должно стоять время. Например: вчера в {time}"_q,
		},
		{
			tr::lng_status_lastseen_date.base,
			"lng_status_lastseen_date",
			u"«был(а) <дата>»"_q,
			u"Напишите {date} там, где должна стоять дата. Например: заходил(а) {date}"_q,
		},
		{
			tr::lng_status_lastseen_date_time.base,
			"lng_status_lastseen_date_time",
			u"«был(а) <дата> в …»"_q,
			u"Напишите {date} и {time} там, где должны стоять дата и время. Например: {date} в {time}"_q,
		},
		{ tr::lng_status_recently.base, "lng_status_recently", u"«был(а) недавно»"_q },
		{ tr::lng_status_last_week.base, "lng_status_last_week", u"«был(а) на этой неделе»"_q },
		{ tr::lng_status_last_month.base, "lng_status_last_month", u"«был(а) в этом месяце»"_q },
		{ tr::lng_status_offline.base, "lng_status_offline", u"«был(а) давно»"_q },
		{ tr::lng_typing.base, "lng_typing", u"«печатает» в личном чате"_q },
		{
			tr::lng_user_typing.base,
			"lng_user_typing",
			u"«Имя печатает» в группах"_q,
			u"Напишите {user} там, где должно стоять имя. Например: {user} строчит"_q,
		},
		{ tr::lng_send_action_record_audio.base, "lng_send_action_record_audio", u"«записывает голосовое»"_q },
		{ tr::lng_send_action_record_round.base, "lng_send_action_record_round", u"«записывает кружок»"_q },
		{ tr::lng_send_action_upload_photo.base, "lng_send_action_upload_photo", u"«отправляет фото»"_q },
		{ tr::lng_send_action_upload_video.base, "lng_send_action_upload_video", u"«отправляет видео»"_q },
		{ tr::lng_send_action_upload_file.base, "lng_send_action_upload_file", u"«отправляет файл»"_q },
	};
	return result;
}

void SetupCustomTexts(rpl::lifetime &lifetime) {
	auto &state = GetState();
	for (const auto &entry : CustomTexts()) {
		const auto value = QString::fromUtf8(
			Core::App().settings().readPref<QByteArray>(PrefKey(entry.key)));
		if (!value.isEmpty()) {
			state.custom[entry.index] = value;
		}
	}
	ApplyAll();
	// A new language or a language pack update brings the pack texts back.
	Lang::GetInstance().updated(
	) | rpl::on_next([] {
		if (!GetState().applying) {
			ApplyAll();
		}
	}, lifetime);
}

QString CustomTextValue(const QByteArray &key) {
	const auto entry = Find(key);
	if (!entry) {
		return QString();
	}
	const auto &custom = GetState().custom;
	const auto i = custom.find(entry->index);
	return (i != end(custom)) ? i->second : QString();
}

void SetCustomText(const QByteArray &key, const QString &value) {
	const auto entry = Find(key);
	if (!entry) {
		return;
	}
	auto &state = GetState();
	Restore(*entry);
	if (value.isEmpty()) {
		state.custom.remove(entry->index);
	} else {
		state.custom[entry->index] = value;
	}
	Core::App().settings().writePref<QByteArray>(
		PrefKey(entry->key),
		value.toUtf8());
	Core::App().saveSettingsDelayed();
	ApplyAll();
	state.applying = true;
	Lang::GetInstance().notifyLocalValuesChanged();
	state.applying = false;
	state.changes.fire({});
}

int CustomTextsCount() {
	return int(GetState().custom.size());
}

rpl::producer<> CustomTextsChanges() {
	return GetState().changes.events();
}

} // namespace Telewhite
