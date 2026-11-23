'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
# Batch 32 and 33 settings rows, applied at build time with the rules of
# telewhite_patch.py: "Подтверждать звонки", "Диктовка вместо голосовых",
# "Скрывать заблокированных в группах", "Удалять «присоединился к
# Telegram»", "Капсулы в списке чатов" and "Стеклянный профиль". Each piece is skipped once its text is already in the source.
# Telegram/configure.py imports this module.
#
#   python telewhite_patch_batch32.py [--root <repository>] [--check]

import os
import sys

import telewhite_patch
import telewhite_patch_more  # noqa: F401, keeps the order of the features.

SETTINGS = 'Telegram/SourceFiles/telewhite/telewhite_settings.cpp'

FEATURES = [
    {
        'id': 'settings-batch-32',
        'title': 'settings rows of batch 32',
        'pieces': [
            {
                'file': SETTINGS,
                'find': r'''return u"Без историй, рекламы, статусов и\u00A0Premium"_q;''',
                'replace': r'''return u"Без историй, рекламы, Premium и\u00A0заблокированных"_q;''',
            },
            {
                'file': SETTINGS,
                'find': r'''{ u"стикер"_q, u"анонимные"_q, u"набор"_q, u"sticker"_q });
		break;
	case Group::Chats:''',
                'replace': r'''{ u"стикер"_q, u"анонимные"_q, u"набор"_q, u"sticker"_q });
		result.push_back(ModToggleText(
			u"telewhite/sending/confirm-calls"_q,
			u"Подтверждать звонки"_q,
			Mod::ConfirmCalls,
			Scene::ConfirmCalls,
			{ u"звонок"_q, u"позвонить"_q, u"случайно"_q, u"call"_q }));
		result.back().about = u"Перед звонком или видеозвонком появляется вопрос «Позвонить?»: случайное нажатие на\u00A0трубку в\u00A0шапке чата или профиле никого не\u00A0потревожит."_q;
		if (Platform::IsWindows()) {
			result.push_back(ModToggleText(
				u"telewhite/sending/dictation"_q,
				u"Диктовка вместо голосовых"_q,
				Mod::Dictation,
				Scene::Dictation,
				{ u"диктовка"_q, u"голосом"_q, u"распознавание"_q, u"dictation"_q }));
			result.back().about = u"Нажатие на\u00A0микрофон открывает голосовой ввод Windows (Win+H): вы говорите, а\u00A0текст печатается в\u00A0поле сообщения. Голосовое при этом не\u00A0записывается."_q;
		}
		break;
	case Group::Chats:''',
            },
            {
                'file': SETTINGS,
                'find': r'''Применится при следующем открытии настроек или чата."_q;
		break;
	case Group::Profile:''',
                'replace': r'''Применится при следующем открытии настроек или чата."_q;
		result.push_back(ModToggleText(
			u"telewhite/clean/hide-blocked"_q,
			u"Скрывать заблокированных в группах"_q,
			Mod::HideBlocked,
			Scene::HideBlocked,
			{ u"заблокированные"_q, u"блок"_q, u"группы"_q, u"blocked"_q }));
		result.back().about = u"Сообщения тех, кого вы заблокировали, не\u00A0видны в\u00A0группах и\u00A0комментариях. Список заблокированных читается при запуске, после блокировки чат нужно открыть заново."_q;
		result.push_back(ModToggleText(
			u"telewhite/clean/delete-joined"_q,
			u"Удалять «присоединился к Telegram»"_q,
			Mod::DeleteJoined,
			Scene::DeleteJoined,
			{ u"присоединился"_q, u"новые контакты"_q, u"joined"_q }));
		result.back().about = u"Сообщение «контакт присоединился к\u00A0Telegram» сразу удаляется у\u00A0вас, и\u00A0пустой чат не\u00A0всплывает наверх списка. У\u00A0собеседника ничего не\u00A0меняется."_q;
		break;
	case Group::Profile:''',
            },
            {
                'file': SETTINGS,
                'find': r'''AddToggle(builder, std::move(glass));
	AddFontButton(builder);''',
                'replace': r'''AddToggle(builder, std::move(glass));
	auto glassChats = ModToggleText(
		u"telewhite/design/glass-chats"_q,
		u"Капсулы в списке чатов"_q,
		Mod::GlassChats,
		Scene::GlassChats,
		{ u"стекло"_q, u"капсулы"_q, u"список чатов"_q, u"glass"_q });
	glassChats.sub = true;
	glassChats.about = u"Открытый и\u00A0наведённый чат выделяются скруглённой стеклянной капсулой с\u00A0бликом вместо полосы во\u00A0всю ширину."_q;
	glassChats.shown = EnabledValue(Mod::GlassUi);
	AddToggle(builder, std::move(glassChats));
	auto glassProfile = ModToggleText(
		u"telewhite/design/glass-profile"_q,
		u"Стеклянный профиль"_q,
		Mod::GlassProfile,
		Scene::GlassProfile,
		{ u"профиль"_q, u"стекло"_q, u"карточки"_q, u"profile"_q });
	glassProfile.sub = true;
	glassProfile.about = u"Разделы профиля становятся скруглёнными карточками на\u00A0сером фоне, как в\u00A0новом Telegram для iPhone, а\u00A0кнопки «Чат», «Звонок» и\u00A0другие получают стеклянный блик. Применится при следующем открытии профиля."_q;
	glassProfile.shown = EnabledValue(Mod::GlassUi);
	AddToggle(builder, std::move(glassProfile));
	AddFontButton(builder);''',
            },
            {
                'file': SETTINGS,
                'find': r'''take(u"sending/voice"_q);
	take(u"sending/anti-caps"_q);''',
                'replace': r'''take(u"sending/voice"_q);
	take(u"sending/confirm-calls"_q);
	take(u"sending/dictation"_q);
	take(u"sending/anti-caps"_q);''',
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
