/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_now_playing.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "telewhite/telewhite_mods.h"

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#endif // Q_OS_WIN

#include <QtCore/QRegularExpression>

#include <atomic>

namespace Telewhite::NowPlaying {
namespace {

constexpr auto kPollInterval = crl::time(4000);
constexpr auto kPollTimeout = crl::time(20000);

// A new track goes to the profile when it plays for a couple of seconds,
// a paused one stays there for a minute, so short pauses change nothing.
constexpr auto kApplyDelay = crl::time(2000);
constexpr auto kRestoreDelay = crl::time(60000);

// Telegram limits profile updates, so tracks change at most twice a minute.
constexpr auto kMinInterval = crl::time(30000);
constexpr auto kRestoreInterval = crl::time(3000);
constexpr auto kFailRetry = crl::time(60000);
constexpr auto kMinLimit = 16;
constexpr auto kShrinkStep = 8;

constexpr auto kStateVersion = "1";

struct Snapshot {
	Track track;
	QString blockedApp;
};

struct Filter {
	bool spotify = false;
	bool yandex = false;
	bool vk = false;
	bool other = false;
};

[[nodiscard]] Source Classify(const QString &app) {
	const auto lower = app.toLower();
	if (lower.contains(u"telewhite"_q) || lower.contains(u"telegram"_q)) {
		// Music played by Telewhite itself.
		return Source::None;
	} else if (lower.contains(u"spotify"_q)) {
		return Source::Spotify;
	}
	const auto music = lower.contains(u"music"_q)
		|| lower.contains(u"музык"_q);
	if (music
		&& (lower.contains(u"yandex"_q) || lower.contains(u"яндекс"_q))) {
		return Source::Yandex;
	} else if (music
		&& (lower.contains(u"vk"_q) || lower.contains(u"вк"_q))) {
		return Source::Vk;
	}
	return Source::Other;
}

[[nodiscard]] Filter CurrentFilter() {
	return {
		.spotify = Enabled(Mod::NowPlayingSpotify),
		.yandex = Enabled(Mod::NowPlayingYandex),
		.vk = Enabled(Mod::NowPlayingVk),
		.other = Enabled(Mod::NowPlayingOther),
	};
}

[[nodiscard]] bool Allowed(Source source, const Filter &filter) {
	switch (source) {
	case Source::Spotify: return filter.spotify;
	case Source::Yandex: return filter.yandex;
	case Source::Vk: return filter.vk;
	case Source::Other: return filter.other;
	case Source::None: return false;
	}
	return false;
}

[[nodiscard]] QString Clean(const QString &text) {
	return text.simplified();
}

[[nodiscard]] QString Fit(const QString &text, int limit) {
	if (text.size() <= limit) {
		return text;
	}
	auto result = text.left(std::max(limit - 1, 0));
	if (!result.isEmpty() && result.back().isHighSurrogate()) {
		result.chop(1);
	}
	return result.trimmed() + QChar(0x2026);
}

[[nodiscard]] int FloodSeconds(const QString &type) {
	static const auto Expression = QRegularExpression(
		u"^FLOOD(?:_PREMIUM)?_WAIT_(\\d+)$"_q);
	const auto match = Expression.match(type);
	return match.hasMatch() ? match.captured(1).toInt() : 60;
}

#ifdef Q_OS_WIN

namespace Control = winrt::Windows::Media::Control;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;
using Manager = Control::GlobalSystemMediaTransportControlsSessionManager;
using MediaSession = Control::GlobalSystemMediaTransportControlsSession;
using Properties
	= Control::GlobalSystemMediaTransportControlsSessionMediaProperties;
using PlaybackStatus
	= Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

std::atomic<bool> ErrorLogged = false;

[[nodiscard]] QString FromHString(const winrt::hstring &value) {
	return QString::fromWCharArray(value.c_str(), int(value.size()));
}

void LogError(const char *where, const QString &message) {
	// The system is polled every few seconds, log only the first error.
	if (!ErrorLogged.exchange(true)) {
		LOG(("Telewhite Now Playing Error: %1, %2."
			).arg(QString::fromUtf8(where)
			).arg(message));
	}
}

// Callbacks come on the Windows thread pool.
void RequestProperties(
		MediaSession session,
		Snapshot snapshot,
		Fn<void(Snapshot)> done) {
	try {
		session.TryGetMediaPropertiesAsync().Completed([=](
				const IAsyncOperation<Properties> &operation,
				AsyncStatus status) {
			auto result = snapshot;
			try {
				if (status == AsyncStatus::Completed) {
					if (const auto properties = operation.GetResults()) {
						result.track.artist = Clean(
							FromHString(properties.Artist()));
						result.track.title = Clean(
							FromHString(properties.Title()));
					}
				}
			} catch (const winrt::hresult_error &error) {
				LogError("properties", FromHString(error.message()));
			} catch (...) {
				LogError("properties", u"unknown exception"_q);
			}
			if (result.track.title.isEmpty()
				|| (result.track.source == Source::Spotify
					&& result.track.artist.isEmpty())) {
				// Spotify plays ads without an artist.
				result.track = Track();
			}
			done(std::move(result));
		});
		return;
	} catch (const winrt::hresult_error &error) {
		LogError("properties request", FromHString(error.message()));
	} catch (...) {
		LogError("properties request", u"unknown exception"_q);
	}
	snapshot.track = Track();
	done(std::move(snapshot));
}

// Returns false if the request could not be started.
[[nodiscard]] bool RequestSnapshot(Filter filter, Fn<void(Snapshot)> done) {
	try {
		Manager::RequestAsync().Completed([=](
				const IAsyncOperation<Manager> &operation,
				AsyncStatus status) {
			auto snapshot = Snapshot();
			auto chosen = MediaSession(nullptr);
			try {
				if (status == AsyncStatus::Completed) {
					const auto manager = operation.GetResults();
					const auto current = manager.GetCurrentSession();
					const auto currentId = current
						? current.SourceAppUserModelId()
						: winrt::hstring();
					auto chosenCurrent = false;
					for (const auto &session : manager.GetSessions()) {
						try {
							const auto info = session.GetPlaybackInfo();
							if (!info
								|| (info.PlaybackStatus()
									!= PlaybackStatus::Playing)) {
								continue;
							}
							const auto id = session.SourceAppUserModelId();
							const auto app = FromHString(id);
							const auto source = Classify(app);
							if (source == Source::None) {
								continue;
							} else if (!Allowed(source, filter)) {
								if (snapshot.blockedApp.isEmpty()) {
									snapshot.blockedApp = app;
								}
								continue;
							}
							const auto isCurrent = !currentId.empty()
								&& (id == currentId);
							if (!chosen || (isCurrent && !chosenCurrent)) {
								chosen = session;
								chosenCurrent = isCurrent;
								snapshot.track.app = app;
								snapshot.track.source = source;
							}
						} catch (...) {
							// Skip the broken session, check the others.
						}
					}
				}
			} catch (const winrt::hresult_error &error) {
				chosen = nullptr;
				LogError("sessions", FromHString(error.message()));
			} catch (...) {
				chosen = nullptr;
				LogError("sessions", u"unknown exception"_q);
			}
			if (!chosen) {
				snapshot.track = Track();
				done(std::move(snapshot));
				return;
			}
			RequestProperties(chosen, std::move(snapshot), done);
		});
		return true;
	} catch (const winrt::hresult_error &error) {
		LogError("manager", FromHString(error.message()));
	} catch (...) {
		LogError("manager", u"unknown exception"_q);
	}
	return false;
}

#endif // Q_OS_WIN

class Watcher final : public base::has_weak_ptr {
public:
	Watcher();

	[[nodiscard]] rpl::producer<Status> value() const;
	void addViewer();
	void removeViewer();

private:
	void refresh();
	void poll();

	base::Timer _timer;
	rpl::variable<Status> _status;
	crl::time _pollStarted = 0;
	int _viewers = 0;
	bool _polling = false;
	bool _unsupported = false;
	rpl::lifetime _lifetime;

};

class Bio final : public base::has_weak_ptr {
public:
	explicit Bio(not_null<Main::Session*> session);
	~Bio();

	[[nodiscard]] not_null<Main::Session*> session() const;
	[[nodiscard]] bool quitPrevent();

private:
	void setTarget(QString text, crl::time delay);
	void schedule(crl::time delay);
	void apply();
	void send(const QString &text, bool restore);
	void finished();
	void aboutChanged();
	void readState();
	void writeState();
	void clearState();
	[[nodiscard]] int limit() const;

	const not_null<Main::Session*> _session;
	const std::string _key;
	MTP::Sender _api;
	base::Timer _timer;

	// Empty target restores the original "About".
	QString _target;
	QString _skipped;
	QString _original;
	QString _shown;
	Source _source = Source::None;
	mtpRequestId _requestId = 0;
	crl::time _nextAllowed = 0;
	int _shrink = 0;
	bool _active = false;
	bool _restoring = false;
	bool _waitingFull = false;
	bool _quitting = false;

	rpl::lifetime _lifetime;

};

Watcher *GlobalWatcher = nullptr;
bool GlobalWatcherDestroyed = false;

[[nodiscard]] Watcher *ResolveWatcher() {
	if (!GlobalWatcher && !GlobalWatcherDestroyed) {
		GlobalWatcher = new Watcher();
	}
	return GlobalWatcher;
}

[[nodiscard]] std::vector<not_null<Bio*>> &Bios() {
	static auto result = std::vector<not_null<Bio*>>();
	return result;
}

[[nodiscard]] rpl::producer<Track> TrackValue() {
	if (const auto watcher = ResolveWatcher()) {
		return watcher->value(
		) | rpl::map([](const Status &status) {
			return status.track;
		}) | rpl::distinct_until_changed();
	}
	return rpl::single(Track());
}

[[nodiscard]] bool SourceEnabled(Source source) {
	return Allowed(source, CurrentFilter());
}

Watcher::Watcher()
: _timer([=] { poll(); })
, _status(Status{ .supported = Supported() }) {
	rpl::merge(
		EnabledValue(Mod::NowPlaying) | rpl::to_empty,
		EnabledValue(Mod::NowPlayingSpotify) | rpl::skip(1) | rpl::to_empty,
		EnabledValue(Mod::NowPlayingYandex) | rpl::skip(1) | rpl::to_empty,
		EnabledValue(Mod::NowPlayingVk) | rpl::skip(1) | rpl::to_empty,
		EnabledValue(Mod::NowPlayingOther) | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

rpl::producer<Status> Watcher::value() const {
	return _status.value();
}

void Watcher::addViewer() {
	++_viewers;
	refresh();
}

void Watcher::removeViewer() {
	--_viewers;
	refresh();
}

void Watcher::refresh() {
	const auto needed = !_unsupported
		&& Supported()
		&& (_viewers > 0 || Enabled(Mod::NowPlaying));
	if (!needed) {
		_timer.cancel();
		_status = Status{ .supported = !_unsupported && Supported() };
		return;
	}
	if (!_timer.isActive()) {
		_timer.callEach(kPollInterval);
	}
	poll();
}

void Watcher::poll() {
	const auto now = crl::now();
	if (_polling && now - _pollStarted < kPollTimeout) {
		return;
	}
#ifdef Q_OS_WIN
	_polling = true;
	_pollStarted = now;
	const auto weak = base::make_weak(this);
	const auto done = [=](Snapshot snapshot) {
		crl::on_main(weak, [=] {
			_polling = false;
			if (_timer.isActive()) {
				_status = Status{
					.track = snapshot.track,
					.blockedApp = snapshot.blockedApp,
					.supported = true,
				};
			}
		});
	};
	if (!RequestSnapshot(CurrentFilter(), done)) {
		// Windows before 10 1809 has no media sessions manager.
		_polling = false;
		_unsupported = true;
		refresh();
	}
#endif // Q_OS_WIN
}

Bio::Bio(not_null<Main::Session*> session)
: _session(session)
, _key("telewhite-now-playing-bio-"
	+ std::to_string(session->uniqueId()))
, _api(&session->mtp())
, _timer([=] { apply(); }) {
	Bios().push_back(this);
	readState();

	using Flag = Data::PeerUpdate::Flag;
	_session->changes().peerUpdates(
		_session->user(),
		Flag::About | Flag::FullInfo
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		if (update.flags & Flag::About) {
			aboutChanged();
		}
		if ((update.flags & Flag::FullInfo) && _waitingFull) {
			_waitingFull = false;
			schedule(0);
		}
	}, _lifetime);

	rpl::combine(
		EnabledValue(Mod::NowPlaying),
		TrackValue()
	) | rpl::on_next([=](bool enabled, const Track &track) {
		if (!enabled) {
			setTarget(QString(), 0);
		} else if (!track.empty()) {
			_source = track.source;
			setTarget(TrackText(track), kApplyDelay);
		} else {
			// The source was turned off - restore right away.
			setTarget(
				QString(),
				SourceEnabled(_source) ? kRestoreDelay : 0);
		}
	}, _lifetime);

	if (_active) {
		// Telewhite was closed without restoring the "About".
		schedule(Enabled(Mod::NowPlaying) ? kRestoreDelay : 0);
	}
}

Bio::~Bio() {
	auto &list = Bios();
	list.erase(ranges::remove(list, not_null<Bio*>(this)), end(list));
}

not_null<Main::Session*> Bio::session() const {
	return _session;
}

void Bio::setTarget(QString text, crl::time delay) {
	if (_target == text) {
		return;
	}
	_target = std::move(text);
	if (_target != _skipped) {
		_skipped = QString();
	}
	schedule(delay);
}

void Bio::schedule(crl::time delay) {
	if (_quitting) {
		return;
	}
	const auto wait = std::max(delay, _nextAllowed - crl::now());
	_timer.callOnce(std::max(wait, crl::time(0)));
}

void Bio::apply() {
	if (_requestId || _quitting) {
		return;
	} else if (const auto wait = _nextAllowed - crl::now(); wait > 0) {
		_timer.callOnce(wait);
		return;
	} else if (_target.isEmpty() && !_active) {
		return;
	}
	const auto self = _session->user();
	if (!self->wasFullUpdated()) {
		// The actual "About" is required, FullInfo update calls us again.
		_waitingFull = true;
		self->updateFull();
		return;
	}
	const auto about = self->about();
	if (_active && about != _shown) {
		// Changed in the settings or on another device - keep it.
		LOG(("Telewhite Now Playing: the bio was changed, keeping it."));
		_skipped = _target;
		clearState();
		return;
	} else if (_target.isEmpty()) {
		if (about == _original) {
			clearState();
		} else {
			send(_original, true);
		}
		return;
	} else if (_target == _skipped) {
		return;
	}
	const auto text = Fit(_target, limit());
	if (_active && about == text) {
		return;
	} else if (!_active) {
		_active = true;
		_original = about;
		_shown = about;
	}
	send(text, false);
}

void Bio::send(const QString &text, bool restore) {
	// The state is saved before the request, so after a crash it is known
	// what the "About" was and what was shown instead of it.
	const auto previous = _shown;
	if (!restore) {
		_shown = text;
		writeState();
	}
	_restoring = restore;
	_requestId = _api.request(MTPaccount_UpdateProfile(
		MTP_flags(MTPaccount_UpdateProfile::Flag::f_about),
		MTPstring(),
		MTPstring(),
		MTP_string(text)
	)).done([=](const MTPUser &result) {
		_requestId = 0;
		_nextAllowed = crl::now()
			+ (restore ? kRestoreInterval : kMinInterval);
		if (restore) {
			clearState();
		}
		_session->data().processUser(result);
		_session->user()->setAbout(text);
		finished();
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		const auto &type = error.type();
		if (!restore) {
			_shown = previous;
			writeState();
		}
		auto retry = kFailRetry;
		if (MTP::IsFloodError(error)) {
			retry = FloodSeconds(type) * crl::time(1000) + crl::time(1000);
		} else if (type == u"ABOUT_TOO_LONG"_q) {
			_shrink += kShrinkStep;
			retry = crl::time(1000);
		}
		LOG(("Telewhite Now Playing: could not update the bio, %1."
			).arg(type));
		_nextAllowed = crl::now() + retry;
		finished();
	}).send();
}

void Bio::finished() {
	if (_quitting) {
		Core::App().quitPreventFinished();
		return;
	}
	schedule(0);
}

void Bio::aboutChanged() {
	if (_requestId || !_active || !_session->user()->wasFullUpdated()) {
		return;
	} else if (_session->user()->about() != _shown) {
		LOG(("Telewhite Now Playing: the bio was changed, keeping it."));
		_skipped = _target;
		clearState();
	}
}

bool Bio::quitPrevent() {
	if (_quitting) {
		return (_requestId != 0);
	} else if (!_active) {
		return false;
	}
	_quitting = true;
	_timer.cancel();
	if (_requestId) {
		if (_restoring) {
			return true;
		}
		_api.request(base::take(_requestId)).cancel();
	}
	LOG(("Telewhite Now Playing: restoring the bio before quit."));
	send(_original, true);
	return true;
}

void Bio::readState() {
	const auto value = Core::App().settings().readPref<QByteArray>(_key);
	const auto parts = value.split('\n');
	if (parts.size() != 3 || parts[0] != QByteArray(kStateVersion)) {
		return;
	}
	_original = QString::fromUtf8(QByteArray::fromBase64(parts[1]));
	_shown = QString::fromUtf8(QByteArray::fromBase64(parts[2]));
	_active = true;
}

void Bio::writeState() {
	Core::App().settings().writePref<QByteArray>(
		_key,
		QByteArray(kStateVersion)
			+ '\n'
			+ _original.toUtf8().toBase64()
			+ '\n'
			+ _shown.toUtf8().toBase64());
	Core::App().saveSettings();
}

void Bio::clearState() {
	_active = false;
	_original = QString();
	_shown = QString();
	Core::App().settings().clearPref(_key);
	Core::App().saveSettings();
}

int Bio::limit() const {
	const auto limits = Data::PremiumLimits(_session);
	const auto full = _session->premium()
		? limits.aboutLengthPremium()
		: limits.aboutLengthDefault();
	return std::max(full - _shrink, kMinLimit);
}

} // namespace

bool Supported() {
#ifdef Q_OS_WIN
	return Platform::IsWindows10OrGreater() && base::WinRT::Supported();
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

void Setup(rpl::lifetime &lifetime) {
	[[maybe_unused]] const auto watcher = ResolveWatcher();
	lifetime.add([] {
		GlobalWatcherDestroyed = true;
		delete base::take(GlobalWatcher);
	});
}

void Start(not_null<Main::Session*> session) {
	session->lifetime().make_state<Bio>(session);
}

bool IsQuitPrevent(not_null<Main::Session*> session) {
	for (const auto bio : Bios()) {
		if (bio->session() == session) {
			return bio->quitPrevent();
		}
	}
	return false;
}

rpl::producer<Status> StatusValue() {
	return [](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto watcher = ResolveWatcher();
		if (!watcher) {
			consumer.put_next(Status());
			return lifetime;
		}
		lifetime = watcher->value().start_existing(consumer);
		watcher->addViewer();
		lifetime.add([weak = base::make_weak(watcher)] {
			if (const auto strong = weak.get()) {
				strong->removeViewer();
			}
		});
		return lifetime;
	};
}

QString StatusText(const Status &status) {
	if (!status.supported) {
		return tr::lng_telewhite_mods_music_unsupported(tr::now);
	} else if (!status.track.empty()) {
		const auto source = (status.track.source == Source::Other)
			? AppName(status.track.app)
			: SourceName(status.track.source);
		return TrackText(status.track) + u" \u00B7 "_q + source;
	} else if (!status.blockedApp.isEmpty()) {
		return tr::lng_telewhite_mods_music_blocked(
			tr::now,
			lt_app,
			AppName(status.blockedApp));
	}
	return tr::lng_telewhite_mods_music_nothing(tr::now);
}

QString SourceName(Source source) {
	switch (source) {
	case Source::Spotify:
		return tr::lng_telewhite_mods_now_playing_spotify(tr::now);
	case Source::Yandex:
		return tr::lng_telewhite_mods_now_playing_yandex(tr::now);
	case Source::Vk: return tr::lng_telewhite_mods_now_playing_vk(tr::now);
	case Source::Other:
	case Source::None: break;
	}
	return QString();
}

QString AppName(const QString &app) {
	const auto source = Classify(app);
	if (source != Source::Other && source != Source::None) {
		return SourceName(source);
	}
	const auto lower = app.toLower();
	const auto known = std::array<std::pair<QString, QString>, 8>{ {
		{ u"chrome"_q, u"Google Chrome"_q },
		{ u"msedge"_q, u"Microsoft Edge"_q },
		{ u"firefox"_q, u"Firefox"_q },
		{ u"308046b0af4a39cb"_q, u"Firefox"_q },
		{ u"opera"_q, u"Opera"_q },
		{ u"yandex"_q, u"Yandex Browser"_q },
		{ u"vivaldi"_q, u"Vivaldi"_q },
		{ u"brave"_q, u"Brave"_q },
	} };
	for (const auto &[part, name] : known) {
		if (lower.contains(part)) {
			return name;
		}
	}
	// "C:\Path\Player.exe", "Player.exe" or "Company.Player_hash!App".
	auto result = app.mid(app.lastIndexOf(QChar('\\')) + 1);
	if (const auto bang = result.indexOf(QChar('!')); bang > 0) {
		result = result.mid(bang + 1);
	}
	if (result.endsWith(u".exe"_q, Qt::CaseInsensitive)) {
		result.chop(4);
	}
	return result.isEmpty() ? app : result;
}

QString TrackText(const Track &track) {
	if (track.empty()) {
		return QString();
	}
	return u"\U0001F3A7 "_q + (track.artist.isEmpty()
		? track.title
		: (track.artist + u" \u2014 "_q + track.title));
}

} // namespace Telewhite::NowPlaying
