/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QFont>

class QPainter;

// Pure Qt painting of the "how it works" previews in Telewhite Mods box.
namespace Telewhite::Preview {

enum class Scene {
	GhostAll,
	GhostRead,
	GhostTyping,
	GhostOnline,
	GhostStories,
	Streamer,
	SaveDeleted,
	EditHistory,
	ForwardNoAuthor,
	MultiForward,
	ConfirmStickers,
	ConfirmGifs,
	ConfirmVoice,
	LocalPins,
	HideStories,
	HideSponsored,
	HideEmojiStatus,
	HidePhone,
	PreciseLastSeen,
	ShowSeconds,
	RegistrationDate,
	Snowfall,
	KeepOwnDeleted,
	KeepTimerMessages,
	EditHistoryInline,
	AntiCaps,
	NoSpoilers,
	NoGreetingSticker,
	BypassRestrictions,
	TranslateChats,
	FakeLocation,
	ThemedIcon,
	NoBubbleTails,
	AuthorOnline,
	HideStoryRings,
	SnowfallChats,
	NowPlaying,
	ReadOnReply,
	TranslateBeforeSend,
	HidePremium,
	PollResults,
	StickersWithoutSet,
	ThemeLight,
	ThemeDark,
	ThemeAmoled,
};

struct Palette {
	QColor bg;
	QColor fg;
	QColor sub;
	QColor accent;
	QColor accentFg;
	QColor inBg;
	QColor inFg;
	QColor inSub;
	QColor outBg;
	QColor outFg;
	QColor outSub;
	QColor attention;
	QColor online;
	QFont font;
	double scale = 1.;
};

[[nodiscard]] QString SceneTitle(Scene scene);
[[nodiscard]] QString SceneHint(Scene scene);
[[nodiscard]] bool SceneIsTheme(Scene scene);

[[nodiscard]] int CardHeight(double scale);
[[nodiscard]] int PanelsHeight(double scale);

// state: 0. - the left panel ("without mod") is current,
//        1. - the right panel ("with mod") is current.
void PaintCard(
	QPainter &p,
	const QRectF &rect,
	Scene scene,
	double state,
	const Palette &palette);

// Only the "without mod" / "with mod" panels, without title and hint.
void PaintPanels(
	QPainter &p,
	const QRectF &rect,
	Scene scene,
	double state,
	const Palette &palette);

struct Banner {
	QString title;
	QString counter;
	QFont font;
	QColor bg = QColor(0xff, 0xff, 0xff);
	QColor fg = QColor(0x00, 0x00, 0x00);
	QColor sub = QColor(0x99, 0x99, 0x99);
	QColor accent = QColor(0x7a, 0x5c, 0xf5);
	double scale = 1.;
	double over = 0.;
};

[[nodiscard]] int BannerHeight(double scale);
[[nodiscard]] int BannerRadius(double scale);

// Flat minimal row tinted by the theme accent, opens Telewhite Mods.
void PaintBanner(QPainter &p, const QRectF &rect, const Banner &banner);

void PaintGhostIcon(QPainter &p, const QRectF &rect, const QColor &color);

// Minimalistic trash can, used next to the time of deleted messages.
void PaintTrashIcon(
	QPainter &p,
	const QRectF &rect,
	const QColor &color,
	double lineWidth);

} // namespace Telewhite::Preview
