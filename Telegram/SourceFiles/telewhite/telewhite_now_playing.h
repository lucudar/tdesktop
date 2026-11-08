/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

// "Now playing": the track from Spotify, Yandex Music, VK Music and other
// players goes to the "About" of the profile while it plays.
namespace Telewhite::NowPlaying {

enum class Source {
	None,
	Spotify,
	Yandex,
	Vk,
	SoundCloud,
	Other,
};

struct Track {
	QString artist;
	QString title;
	QString app;
	Source source = Source::None;

	[[nodiscard]] bool empty() const {
		return title.isEmpty();
	}

	friend inline bool operator==(const Track &a, const Track &b) = default;
};

struct Status {
	Track track;
	QString blockedApp;
	bool supported = false;

	friend inline bool operator==(const Status &a, const Status &b) = default;
};

// Players are read from the Windows media sessions (the ones shown in the
// volume flyout), it requires Windows 10 1809 or newer.
[[nodiscard]] bool Supported();

// Core::Application owns the system watcher.
void Setup(rpl::lifetime &lifetime);

// Every session shows the track in its own "About" and restores it.
void Start(not_null<Main::Session*> session);

// The original "About" is restored before the application quits.
[[nodiscard]] bool IsQuitPrevent(not_null<Main::Session*> session);

// The system is polled while there are subscribers or the mod is enabled.
[[nodiscard]] rpl::producer<Status> StatusValue();
[[nodiscard]] QString StatusText(const Status &status);

[[nodiscard]] QString SourceName(Source source);
[[nodiscard]] QString AppName(const QString &app);
[[nodiscard]] QString TrackText(const Track &track);

} // namespace Telewhite::NowPlaying
