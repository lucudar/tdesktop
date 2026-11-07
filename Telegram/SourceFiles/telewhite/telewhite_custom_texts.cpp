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

namespace Telewhite {
namespace {

struct State {
	base::flat_map<ushort, QString> custom;
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

// Puts own texts over the language pack, remembering the pack values.
void ApplyAll() {
	auto &state = GetState();
	auto &lang = Lang::GetInstance();
	state.applying = true;
	for (const auto &[index, text] : state.custom) {
		const auto now = lang.getValue(index);
		if (now != text) {
			state.original[index] = now;
			lang.setLocalValue(index, text);
		}
	}
	state.applying = false;
}

void Restore(ushort index) {
	auto &state = GetState();
	const auto i = state.original.find(index);
	if (i != end(state.original)) {
		Lang::GetInstance().setLocalValue(index, i->second);
		state.original.erase(i);
	}
}

} // namespace

const std::vector<CustomText> &CustomTexts() {
	static const auto result = std::vector<CustomText>{
		{ tr::lng_status_online.base, "lng_status_online", u"«в сети»"_q },
		{ tr::lng_status_recently.base, "lng_status_recently", u"«был(а) недавно»"_q },
		{ tr::lng_status_lastseen_now.base, "lng_status_lastseen_now", u"«был(а) только что»"_q },
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
	Restore(entry->index);
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
