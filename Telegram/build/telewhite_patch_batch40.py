'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 40: the design studio, first stage. A box with a live preview: the
strength of the glass blur, its density and shine, the rounding of the
capsules and the cards, the shape of the userpics, the bubble rounding,
the presets and a style code to share. One feature: without any of its
pieces nothing of it is applied. See telewhite_patch.py for the format.
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
import telewhite_patch_batch39

MODS_H = 'Telegram/SourceFiles/telewhite/telewhite_mods.h'
MODS = 'Telegram/SourceFiles/telewhite/telewhite_mods.cpp'
GLASS = 'Telegram/SourceFiles/telewhite/telewhite_glass.h'
BACKDROP = 'Telegram/SourceFiles/telewhite/telewhite_glass_backdrop.h'
TWEAKS = 'Telegram/SourceFiles/telewhite/telewhite_tweaks.cpp'
SETTINGS = 'Telegram/SourceFiles/telewhite/telewhite_settings.cpp'
USERPIC_H = 'Telegram/SourceFiles/ui/userpic_view.h'
USERPIC = 'Telegram/SourceFiles/ui/userpic_view.cpp'
EMPTY = 'Telegram/SourceFiles/ui/empty_userpic.cpp'

DESIGN_CODE = r'''// The design studio: the glass, the rounding, the userpics and the
// presets over a live preview, see telewhite_patch_batch40.py.
struct DesignPreset {
	QString name;
	int blur = 50;
	int tint = 72;
	int shine = 50;
	int radius = 0;
	int avatar = 0;
	int bubbles = 16;
};

constexpr auto kDesignOptions = std::array{
	Option::GlassBlur,
	Option::GlassTint,
	Option::GlassShine,
	Option::UiRadius,
	Option::AvatarShape,
	Option::BubbleRadius,
};

[[nodiscard]] const std::vector<DesignPreset> &DesignPresets() {
	static const auto result = std::vector<DesignPreset>{
		{ u"Как в Telewhite"_q, 50, 72, 50, 0, 0, 16 },
		{ u"iOS 26"_q, 85, 55, 75, 20, 0, 20 },
		{ u"Мягкий"_q, 65, 65, 40, 16, 1, 18 },
		{ u"Строгий"_q, 25, 88, 15, 6, 2, 8 },
		{ u"Чистое стекло"_q, 100, 40, 90, 24, 1, 22 },
	};
	return result;
}

void ApplyDesignPreset(const DesignPreset &preset) {
	SetOption(Option::GlassBlur, preset.blur);
	SetOption(Option::GlassTint, preset.tint);
	SetOption(Option::GlassShine, preset.shine);
	SetOption(Option::UiRadius, preset.radius);
	SetOption(Option::AvatarShape, preset.avatar);
	SetOption(Option::BubbleRadius, preset.bubbles);
}

[[nodiscard]] QString DesignCode() {
	auto parts = QStringList{ u"TW1"_q };
	for (const auto option : kDesignOptions) {
		parts.push_back(QString::number(OptionNow(option)));
	}
	return parts.join(QChar('.'));
}

[[nodiscard]] bool ApplyDesignCode(const QString &code) {
	const auto parts = code.trimmed().split(QChar('.'));
	if (int(parts.size()) != int(kDesignOptions.size()) + 1
		|| parts[0] != u"TW1"_q) {
		return false;
	}
	auto values = std::vector<int>();
	for (auto i = 1; i != int(parts.size()); ++i) {
		auto ok = false;
		const auto value = parts[i].toInt(&ok);
		if (!ok) {
			return false;
		}
		values.push_back(value);
	}
	ApplyDesignPreset(DesignPreset{
		.blur = std::clamp(values[0], 0, 100),
		.tint = std::clamp(values[1], 30, 95),
		.shine = std::clamp(values[2], 0, 100),
		.radius = std::clamp(values[3], 0, 24),
		.avatar = std::clamp(values[4], 0, 2),
		.bubbles = std::clamp(
			values[5],
			kBubbleRadiusMin,
			kBubbleRadiusMax),
	});
	return true;
}

class DesignPreview final : public Ui::RpWidget {
public:
	explicit DesignPreview(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

};

DesignPreview::DesignPreview(QWidget *parent)
: RpWidget(parent) {
	resize(width(), style::ConvertScale(196));
	auto changes = std::vector<rpl::producer<int>>();
	for (const auto option : kDesignOptions) {
		changes.push_back(OptionValue(option));
	}
	rpl::combine(
		std::move(changes)
	) | rpl::on_next([=](const std::vector<int> &) {
		update();
	}, lifetime());
}

void DesignPreview::paintEvent(QPaintEvent *) {
	const auto w = width();
	const auto h = height();
	if (w <= 0 || h <= 0) {
		return;
	}
	const auto s = [](float64 value) {
		return style::ConvertScaleExact(value);
	};
	const auto ratio = style::DevicePixelRatio();
	const auto bubbleRadius = float64(style::ConvertScale(std::clamp(
		OptionNow(Option::BubbleRadius),
		kBubbleRadiusMin,
		kBubbleRadiusMax)));
	const auto uiRadiusValue = OptionNow(Option::UiRadius);
	const auto uiRadius = float64(style::ConvertScale(
		(uiRadiusValue > 0) ? uiRadiusValue : 12));
	const auto shine = std::clamp(OptionNow(Option::GlassShine), 0, 100)
		/ 50.;
	const auto tintAlpha = std::clamp(OptionNow(Option::GlassTint), 30, 95)
		/ 100.;
	const auto blur = std::clamp(OptionNow(Option::GlassBlur), 0, 100);
	const auto shape = std::clamp(OptionNow(Option::AvatarShape), 0, 2);

	// The wallpaper with a few bright spots and two bubbles.
	auto scene = QImage(
		QSize(w, h) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	scene.setDevicePixelRatio(ratio);
	{
		auto q = QPainter(&scene);
		auto hq = PainterHighQualityEnabler(q);
		auto gradient = QLinearGradient(QPointF(0., 0.), QPointF(w, h));
		gradient.setColorAt(0., st::windowBgActive->c.lighter(125));
		gradient.setColorAt(1., st::windowBg->c);
		q.fillRect(QRect(0, 0, w, h), QBrush(gradient));
		q.setPen(Qt::NoPen);
		q.setBrush(QColor(255, 196, 84, 190));
		q.drawEllipse(QPointF(w * 0.22, s(26.)), s(34.), s(34.));
		q.setBrush(QColor(84, 196, 255, 170));
		q.drawEllipse(QPointF(w * 0.72, s(14.)), s(30.), s(30.));
		const auto in = QRectF(s(14.), s(62.), w * 0.6, s(36.));
		const auto out = QRectF(
			w - s(14.) - w * 0.5,
			s(106.),
			w * 0.5,
			s(36.));
		const auto inRadius = std::min(bubbleRadius, in.height() / 2.);
		const auto outRadius = std::min(bubbleRadius, out.height() / 2.);
		q.setBrush(st::msgInBg->c);
		q.drawRoundedRect(in, inRadius, inRadius);
		q.setBrush(st::msgOutBg->c);
		q.drawRoundedRect(out, outRadius, outRadius);
		q.setFont(st::normalFont->f);
		q.setPen(st::historyTextInFg->c);
		q.drawText(
			in.adjusted(s(12.), 0., -s(12.), 0.),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"Как тебе стекло?"_q);
		q.setPen(st::historyTextOutFg->c);
		q.drawText(
			out.adjusted(s(12.), 0., -s(12.), 0.),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"Огонь!"_q);
	}
	auto p = QPainter(this);
	p.drawImage(QPoint(), scene);

	// The glass header: the blurred slice, the tint and the shine.
	const auto header = QRect(0, 0, w, style::ConvertScale(48));
	{
		auto reduced = scene.copy(
			QRect(QPoint(), header.size() * ratio)
		).scaled(
			std::max(header.width() * ratio / 4, 1),
			std::max(header.height() * ratio / 4, 1),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation
		).convertToFormat(QImage::Format_ARGB32_Premultiplied);
		reduced.setDevicePixelRatio(1.);
		reduced = Images::BlurLargeImage(
			std::move(reduced),
			std::max(style::ConvertScale(1 + blur / 10), 1));
		p.drawImage(QRectF(header), reduced);
		auto tint = st::windowBg->c;
		tint.setAlphaF(float(tintAlpha));
		p.fillRect(header, tint);
		auto gloss = QLinearGradient(
			QPointF(header.topLeft()),
			QPointF(header.bottomLeft()));
		gloss.setColorAt(
			0.,
			QColor(255, 255, 255, std::clamp(int(28 * shine), 0, 255)));
		gloss.setColorAt(0.6, QColor(255, 255, 255, 0));
		gloss.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(header, QBrush(gloss));
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto avatar = QRectF(s(12.), s(8.), s(32.), s(32.));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xff, 0x8a, 0xac));
	if (shape > 0) {
		const auto radius = avatar.width() * ((shape == 1) ? 0.36 : 0.22);
		p.drawRoundedRect(avatar, radius, radius);
	} else {
		p.drawEllipse(avatar);
	}
	p.setPen(QColor(255, 255, 255));
	p.setFont(st::semiboldFont->f);
	p.drawText(avatar, Qt::AlignCenter, u"А"_q);
	p.setPen(st::windowFg->c);
	p.drawText(
		QRectF(s(52.), s(8.), w - s(60.), s(32.)),
		Qt::AlignLeft | Qt::AlignVCenter,
		u"Аня"_q);

	// The glass capsule of the input field.
	const auto capsule = QRectF(s(12.), h - s(44.), w - s(24.), s(34.));
	const auto capsuleRadius = std::min(uiRadius, capsule.height() / 2.);
	auto body = st::windowBg->c;
	body.setAlphaF(float(tintAlpha));
	p.setBrush(body);
	p.drawRoundedRect(capsule, capsuleRadius, capsuleRadius);
	auto capsuleGloss = QLinearGradient(
		capsule.topLeft(),
		capsule.bottomLeft());
	capsuleGloss.setColorAt(
		0.,
		QColor(255, 255, 255, std::clamp(int(36 * shine), 0, 255)));
	capsuleGloss.setColorAt(0.55, QColor(255, 255, 255, 0));
	capsuleGloss.setColorAt(1., QColor(255, 255, 255, 0));
	p.setBrush(QBrush(capsuleGloss));
	p.drawRoundedRect(capsule, capsuleRadius, capsuleRadius);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 51), s(1.)));
	p.drawRoundedRect(capsule, capsuleRadius, capsuleRadius);
	p.setPen(st::windowSubTextFg->c);
	p.setFont(st::normalFont->f);
	p.drawText(
		capsule.adjusted(s(14.), 0., -s(14.), 0.),
		Qt::AlignLeft | Qt::AlignVCenter,
		u"Сообщение"_q);
}

// A row with the value on the right and a slider under it, the returned
// callback moves the slider when the value is changed by a preset.
[[nodiscard]] Fn<void()> AddDesignSlider(
		not_null<Ui::VerticalLayout*> container,
		QString title,
		Option option,
		int steps,
		Fn<int(int)> toValue,
		Fn<int(int)> toIndex,
		Fn<QString(int)> label) {
	const auto row = AddButtonWithLabel(
		container,
		rpl::single(title),
		OptionValue(option) | rpl::map(label),
		st::autoDownloadLimitButton);
	row->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::autoDownloadLimitSlider),
		st::autoDownloadLimitPadding);
	slider->resize(st::autoDownloadLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		steps,
		toValue,
		toValue(toIndex(OptionNow(option))),
		[=](int chosen) {
			if (OptionNow(option) != chosen) {
				SetOption(option, chosen);
			}
		});
	return [=] {
		slider->setValue(
			toIndex(OptionNow(option)) / float64(std::max(steps - 1, 1)));
	};
}

void DesignStudioBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"Конструктор дизайна"_q));
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	container->add(
		object_ptr<DesignPreview>(container),
		st::boxRowPadding);
	const auto refreshers = box->lifetime().make_state<
		std::vector<Fn<void()>>>();
	const auto refreshAll = [=] {
		for (const auto &refresh : *refreshers) {
			refresh();
		}
	};
	const auto percent = [](int value) {
		return QString::number(value) + QChar('%');
	};
	const auto pixels = [](int value) {
		return QString::number(value) + u" px"_q;
	};
	const auto addSlider = [&](
			QString title,
			Option option,
			int from,
			int till,
			int step,
			Fn<QString(int)> label) {
		const auto steps = (till - from) / step + 1;
		refreshers->push_back(AddDesignSlider(
			container,
			std::move(title),
			option,
			steps,
			[=](int index) { return from + index * step; },
			[=](int value) {
				return std::clamp((value - from) / step, 0, steps - 1);
			},
			std::move(label)));
	};
	addSlider(u"Размытие стекла"_q, Option::GlassBlur, 0, 100, 5, percent);
	addSlider(u"Плотность стекла"_q, Option::GlassTint, 30, 95, 5, percent);
	addSlider(u"Блик стекла"_q, Option::GlassShine, 0, 100, 5, percent);
	addSlider(
		u"Скругление капсул и карточек"_q,
		Option::UiRadius,
		0,
		24,
		2,
		[=](int value) {
			return (value > 0) ? pixels(value) : u"Авто"_q;
		});
	addSlider(
		u"Форма аватарок"_q,
		Option::AvatarShape,
		0,
		2,
		1,
		[](int value) {
			return (value == 1)
				? u"Сквиркл"_q
				: (value == 2)
				? u"Квадрат"_q
				: u"Круг"_q;
		});
	addSlider(
		u"Скругление пузырей (после перезапуска)"_q,
		Option::BubbleRadius,
		kBubbleRadiusMin,
		kBubbleRadiusMax,
		kBubbleRadiusStep,
		pixels);

	Ui::AddSkip(container);
	for (const auto &preset : DesignPresets()) {
		const auto button = AddButtonWithLabel(
			container,
			rpl::single(preset.name),
			rpl::single(u"Пресет"_q),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			ApplyDesignPreset(preset);
			refreshAll();
		});
	}
	const auto copy = AddButtonWithLabel(
		container,
		rpl::single(u"Скопировать код стиля"_q),
		rpl::single(QString()),
		st::settingsButtonNoIcon);
	copy->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(DesignCode());
		box->showToast(u"Код стиля скопирован: отправьте его другу"_q);
	});
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(u"Вставьте код стиля TW1…"_q),
		QString()));
	box->addButton(rpl::single(u"Готово"_q), [=] { box->closeBox(); });
	box->addLeftButton(rpl::single(u"Применить код"_q), [=] {
		if (ApplyDesignCode(field->getLastText())) {
			refreshAll();
			box->showToast(u"Стиль применён"_q);
		} else {
			field->showError();
		}
	});
}

void AddDesignStudio(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"telewhite/design/studio"_q,
		.title = rpl::single(u"Конструктор дизайна"_q),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(u"Стекло, углы, аватарки"_q),
		.onClick = [=] {
			controller->show(Box(DesignStudioBox));
		},
		.keywords = {
			u"конструктор"_q,
			u"дизайн"_q,
			u"стекло"_q,
			u"размытие"_q,
			u"аватарки"_q,
			u"скругление"_q,
			u"пресет"_q,
			u"design"_q,
		},
	});
}

void BuildDesign(SectionBuilder &builder) {
	AddThemes(builder);
	builder.addSkip();
	AddDesignStudio(builder);'''

FEATURES = [
    {
        'id': 'design-studio',
        'title': 'design studio: glass, rounding, userpics, presets',
        'pieces': [
            {
                'file': MODS_H,
                'find': r'''	BubbleRadius, // Pixels at 100% scale, 2 - 24, applied after a restart.

	kCount,
};''',
                'replace': r'''	BubbleRadius, // Pixels at 100% scale, 2 - 24, applied after a restart.
	GlassBlur, // Percents, 0 - 100, 50 - as in Telewhite.
	GlassTint, // Percents of the bar color over the glass, 30 - 95.
	GlassShine, // Percents, 0 - 100, 50 - as in Telewhite.
	UiRadius, // Pixels of the capsules and the cards, 0 - as in Telewhite.
	AvatarShape, // 0 - circle, 1 - squircle, 2 - rounded square.

	kCount,
};''',
            },
            {
                'file': MODS_H,
                'find': r'''void SetOption(Option option, int value);''',
                'replace': r'''void SetOption(Option option, int value);

// The design studio values (glass, rounding, userpics) are applied now.
void ApplyDesign();''',
            },
            {
                'file': MODS,
                'find': r'''#include <QtCore/QCoreApplication>

namespace Telewhite {
namespace {''',
                'replace': r'''#include <QtCore/QCoreApplication>
#include <QtWidgets/QApplication>

#include "telewhite/telewhite_glass.h"

namespace Telewhite {
namespace {

// The design studio: the glass of the widgets that can't see the mods
// reads the globals of telewhite_glass.h, the userpics of ui/ read the
// application property.
void ApplyDesignNow(bool repaint) {
	auto &tuning = GlassTuningRef();
	const auto blur = std::clamp(OptionNow(Option::GlassBlur), 0, 100);
	const auto tint = std::clamp(OptionNow(Option::GlassTint), 30, 95);
	const auto shine = std::clamp(OptionNow(Option::GlassShine), 0, 100);
	tuning.blurRadius = 1 + blur / 10;
	tuning.tint = tint / 100.;
	tuning.shine = shine / 50.;
	++tuning.revision;
	const auto app = QCoreApplication::instance();
	if (!app) {
		return;
	}
	app->setProperty(
		"telewhiteAvatarShape",
		std::clamp(OptionNow(Option::AvatarShape), 0, 2));
	if (repaint) {
		for (const auto widget : QApplication::topLevelWidgets()) {
			widget->update();
		}
	}
}''',
            },
            {
                'file': MODS,
                'find': r'''	case Option::BubbleRadius: return "telewhite-bubble-radius";''',
                'replace': r'''	case Option::BubbleRadius: return "telewhite-bubble-radius";
	case Option::GlassBlur: return "telewhite-glass-blur";
	case Option::GlassTint: return "telewhite-glass-tint";
	case Option::GlassShine: return "telewhite-glass-shine";
	case Option::UiRadius: return "telewhite-ui-radius";
	case Option::AvatarShape: return "telewhite-avatar-shape";''',
            },
            {
                'file': MODS,
                'find': r'''	case Option::BubbleRadius: return kDefaultBubbleRadius;''',
                'replace': r'''	case Option::BubbleRadius: return kDefaultBubbleRadius;
	case Option::GlassBlur:
	case Option::GlassShine: return 50;
	case Option::GlassTint: return 72;''',
            },
            {
                'file': MODS,
                'find': r'''			app->setProperty(
				kBubbleRadiusProperty,
				options[int(Option::BubbleRadius)].current());
		}
	}

}''',
                'replace': r'''			app->setProperty(
				kBubbleRadiusProperty,
				options[int(Option::BubbleRadius)].current());
		}
	}
	ApplyDesignNow(false);
}''',
            },
            {
                'file': MODS,
                'find': r'''void SetOption(Option option, int value) {
	OptionValues()[int(option)] = value;
	Core::App().settings().writePref<QByteArray>(
		Key(option),
		QByteArray::number(value));
	Core::App().saveSettingsDelayed();
}''',
                'replace': r'''void SetOption(Option option, int value) {
	OptionValues()[int(option)] = value;
	Core::App().settings().writePref<QByteArray>(
		Key(option),
		QByteArray::number(value));
	Core::App().saveSettingsDelayed();
	switch (option) {
	case Option::GlassBlur:
	case Option::GlassTint:
	case Option::GlassShine:
	case Option::UiRadius:
	case Option::AvatarShape: ApplyDesignNow(true); break;
	default: break;
	}
}

void ApplyDesign() {
	ApplyDesignNow(false);
}''',
            },
            {
                'file': GLASS,
                'find': r'''inline constexpr auto kGlassBarTint = 0.72;
inline constexpr auto kGlassCircleTint = 0.9;''',
                'replace': r'''inline constexpr auto kGlassBarTint = 0.72;
inline constexpr auto kGlassCircleTint = 0.9;

// The design studio values, set by Telewhite::ApplyDesign().
struct GlassTuning {
	double tint = kGlassBarTint;
	double shine = 1.;
	int blurRadius = 6;
	quint64 revision = 0;
};

[[nodiscard]] inline GlassTuning &GlassTuningRef() {
	static auto result = GlassTuning();
	return result;
}

[[nodiscard]] inline int GlassShineAlpha(int alpha) {
	return std::clamp(int(alpha * GlassTuningRef().shine), 0, 255);
}''',
            },
            {
                'file': GLASS,
                'find': r'''	tint.setAlphaF(float(tint.alphaF() * kGlassBarTint));''',
                'replace': r'''	tint.setAlphaF(float(tint.alphaF() * GlassTuningRef().tint));''',
            },
            {
                'file': GLASS,
                'find': r'''		gradient.setColorAt(0., QColor(255, 255, 255, 28));''',
                'replace': r'''		gradient.setColorAt(0., QColor(255, 255, 255, GlassShineAlpha(28)));''',
            },
            {
                'file': GLASS,
                'find': r'''	shine.setColorAt(0., QColor(255, 255, 255, dark ? 44 : 110));''',
                'replace': r'''	shine.setColorAt(
		0.,
		QColor(255, 255, 255, GlassShineAlpha(dark ? 44 : 110)));''',
            },
            {
                'file': GLASS,
                'find': r'''	shine.setColorAt(0., QColor(255, 255, 255, 36));''',
                'replace': r'''	shine.setColorAt(0., QColor(255, 255, 255, GlassShineAlpha(36)));''',
            },
            {
                'file': BACKDROP,
                'find': r'''		std::max(style::ConvertScale(kGlassBlurRadius), 1));''',
                'replace': r'''		std::max(style::ConvertScale(GlassTuningRef().blurRadius), 1));''',
            },
            {
                'file': BACKDROP,
                'find': r'''	// The messages under the glass header, when they are under this widget.''',
                'replace': r'''	// The design studio values change the blur.
	key = GlassKeyMix(key, GlassTuningRef().revision);
	// The messages under the glass header, when they are under this widget.''',
            },
            {
                'file': TWEAKS,
                'find': r'''constexpr auto kGlassRowRadius = 12;''',
                'replace': r'''constexpr auto kGlassRowRadius = 12;

// The design studio: the rounding of the capsules and the cards, the
// built-in one while it is not set.
[[nodiscard]] int UiRadiusOr(int fallback) {
	const auto value = OptionNow(Option::UiRadius);
	return style::ConvertScale((value > 0) ? value : fallback);
}''',
            },
            {
                'file': TWEAKS,
                'count': 2,
                'find': r'''const auto radius = float64(style::ConvertScale(kGlassRowRadius));''',
                'replace': r'''const auto radius = float64(UiRadiusOr(kGlassRowRadius));''',
            },
            {
                'file': TWEAKS,
                'count': 2,
                'find': r'''const auto radius = float64(style::ConvertScale(kCardRadius));''',
                'replace': r'''const auto radius = float64(UiRadiusOr(kCardRadius));''',
            },
            {
                'file': TWEAKS,
                'find': r'''void SetupSessionMods(not_null<Main::Session*> session) {
	if (Enabled(Mod::HideBlocked)) {''',
                'replace': r'''void SetupSessionMods(not_null<Main::Session*> session) {
	ApplyDesign();
	if (Enabled(Mod::HideBlocked)) {''',
            },
            {
                'file': USERPIC_H,
                'find': r'''	uint32 paletteVersion : 30 = 0;
	uint32 shape : 2 = 0;''',
                'replace': r'''	uint32 paletteVersion : 28 = 0;
	uint32 shape : 4 = 0; // Telewhite: two more bits for the design studio.''',
            },
            {
                'file': USERPIC,
                'find': r'''#include <cmath>''',
                'replace': r'''#include <cmath>

#include <QtCore/QCoreApplication>''',
            },
            {
                'file': USERPIC,
                'find': r'''constexpr auto kGap = 0.02; // Tunable.

} // namespace''',
                'replace': r'''constexpr auto kGap = 0.02; // Tunable.

// Telewhite: the shape of the round userpics from the design studio,
// 0 - circle, 1 - squircle, 2 - rounded square.
[[nodiscard]] int TelewhiteAvatarShape() {
	const auto app = QCoreApplication::instance();
	return app
		? std::clamp(app->property("telewhiteAvatarShape").toInt(), 0, 2)
		: 0;
}

[[nodiscard]] float64 TelewhiteAvatarRadius(int shape) {
	return (shape == 1) ? 0.36 : 0.22;
}

} // namespace''',
            },
            {
                'file': USERPIC,
                'find': r'''	const auto shapeValue = static_cast<uint32>(shape) & 3;''',
                'replace': r'''	const auto telewhiteShape = (shape == PeerUserpicShape::Forum
			|| shape == PeerUserpicShape::Monoforum)
		? 0
		: TelewhiteAvatarShape();
	const auto shapeValue = (static_cast<uint32>(shape) & 3)
		| (static_cast<uint32>(telewhiteShape) << 2);''',
            },
            {
                'file': USERPIC,
                'find': r'''		} else {
			view.cached = Images::Circle(std::move(view.cached));
		}''',
                'replace': r'''		} else if (telewhiteShape > 0) {
			view.cached = Images::Round(
				std::move(view.cached),
				Images::CornersMask(int(size
					* TelewhiteAvatarRadius(telewhiteShape)
					/ style::DevicePixelRatio())));
		} else {
			view.cached = Images::Circle(std::move(view.cached));
		}''',
            },
            {
                'file': EMPTY,
                'find': r'''#include <QtCore/QMutex>''',
                'replace': r'''#include <QtCore/QMutex>
#include <QtCore/QCoreApplication>''',
            },
            {
                'file': EMPTY,
                'find': r'''namespace Ui {
namespace {''',
                'replace': r'''namespace Ui {
namespace {

// Telewhite: the shape of the round userpics from the design studio,
// 0 - circle, 1 - squircle, 2 - rounded square.
void TelewhiteUserpicShape(QPainter &p, int x, int y, int size) {
	const auto app = QCoreApplication::instance();
	const auto shape = app
		? app->property("telewhiteAvatarShape").toInt()
		: 0;
	if (shape == 1 || shape == 2) {
		const auto radius = size * ((shape == 1) ? 0.36 : 0.22);
		p.drawRoundedRect(QRectF(x, y, size, size), radius, radius);
	} else {
		p.drawEllipse(x, y, size, size);
	}
}''',
            },
            {
                'file': EMPTY,
                'count': 6,
                'find': r'''p.drawEllipse(x, y, size, size);''',
                'replace': r'''TelewhiteUserpicShape(p, x, y, size);''',
            },
            {
                'file': SETTINGS,
                'find': r'''#include "ui/widgets/continuous_sliders.h"''',
                'replace': r'''#include "ui/widgets/continuous_sliders.h"
#include "ui/image/image_prepare.h"
#include "ui/vertical_list.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>''',
            },
            {
                'file': SETTINGS,
                'find': r'''void BuildDesign(SectionBuilder &builder) {
	AddThemes(builder);
	builder.addSkip();''',
                'replace': DESIGN_CODE,
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
