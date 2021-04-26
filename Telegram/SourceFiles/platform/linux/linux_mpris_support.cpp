/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_mpris_support.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "mainwidget.h"

#include <QtCore/QBuffer>
#include <QtGui/QGuiApplication>

#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace internal {
namespace {

constexpr auto kService = "org.mpris.MediaPlayer2.tdesktop"_cs;
constexpr auto kObjectPath = "/org/mpris/MediaPlayer2"_cs;
constexpr auto kFakeTrackPath = "/org/telegram/desktop/track/0"_cs;
constexpr auto kInterface = "org.mpris.MediaPlayer2"_cs;
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

constexpr auto kIntrospectionXML = R"INTROSPECTION(<node>
	<interface name='org.mpris.MediaPlayer2'>
		<method name='Raise'/>
		<method name='Quit'/>
		<property name='CanQuit' type='b' access='read'/>
		<property name='CanRaise' type='b' access='read'/>
		<property name='HasTrackList' type='b' access='read'/>
		<property name='Identity' type='s' access='read'/>
		<property name='DesktopEntry' type='s' access='read'/>
		<property name='SupportedUriSchemes' type='as' access='read'/>
		<property name='SupportedMimeTypes' type='as' access='read'/>
		<property name='Fullscreen' type='b' access='readwrite'/>
		<property name='CanSetFullscreen' type='b' access='read'/>
	</interface>
</node>)INTROSPECTION"_cs;

constexpr auto kPlayerIntrospectionXML = R"INTROSPECTION(<node>
	<interface name='org.mpris.MediaPlayer2.Player'>
		<method name='Next'/>
		<method name='Previous'/>
		<method name='Pause'/>
		<method name='PlayPause'/>
		<method name='Stop'/>
		<method name='Play'/>
		<method name='Seek'>
			<arg direction='in' name='Offset' type='x'/>
		</method>
		<method name='SetPosition'>
			<arg direction='in' name='TrackId' type='o'/>
			<arg direction='in' name='Position' type='x'/>
		</method>
		<method name='OpenUri'>
			<arg direction='in' name='Uri' type='s'/>
		</method>
		<signal name='Seeked'>
			<arg name='Position' type='x'/>
		</signal>
		<property name='PlaybackStatus' type='s' access='read'/>
		<property name='Rate' type='d' access='readwrite'/>
		<property name='Metadata' type='a{sv}' access='read'>
			<annotation name="org.qtproject.QtDBus.QtTypeName" value="QVariantMap"/>
		</property>
		<property name='Volume' type='d' access='readwrite'/>
		<property name='Position' type='x' access='read'/>
		<property name='MinimumRate' type='d' access='read'/>
		<property name='MaximumRate' type='d' access='read'/>
		<property name='CanGoNext' type='b' access='read'/>
		<property name='CanGoPrevious' type='b' access='read'/>
		<property name='CanPlay' type='b' access='read'/>
		<property name='CanPause' type='b' access='read'/>
		<property name='CanSeek' type='b' access='read'/>
		<property name='CanControl' type='b' access='read'/>
	</interface>
</node>)INTROSPECTION"_cs;

auto CreateMetadata(
		const Media::Player::TrackState &state,
		Data::DocumentMedia *trackView) {
	std::map<Glib::ustring, Glib::VariantBase> result;

	if (Media::Player::IsStoppedOrStopping(state.state)) {
		return result;
	}

	result["mpris:trackid"] = Glib::wrap(g_variant_new_object_path(
		kFakeTrackPath.utf8().constData()));
	result["mpris:length"] = Glib::Variant<gint64>::create(
		state.length * 1000);
	result["xesam:title"] = Glib::Variant<Glib::ustring>::create(
		"Unknown Track");

	const auto audioData = state.id.audio();
	if (audioData) {
		if (!audioData->filename().isEmpty()) {
			result["xesam:title"] = Glib::Variant<
				Glib::ustring
			>::create(audioData->filename().toStdString());
		}

		if (audioData->isSong()) {
			const auto songData = audioData->song();
			if (!songData->performer.isEmpty()) {
				result["xesam:artist"] = Glib::Variant<
					std::vector<Glib::ustring>
				>::create({ songData->performer.toStdString() });
			}
			if (!songData->title.isEmpty()) {
				result["xesam:title"] = Glib::Variant<
					Glib::ustring
				>::create(songData->title.toStdString());
			}
		}
	}

	if (trackView) {
		trackView->thumbnailWanted(Data::FileOrigin());
		if (trackView->thumbnail()) {
			QByteArray thumbnailData;
			QBuffer thumbnailBuffer(&thumbnailData);
			trackView->thumbnail()->original().save(
				&thumbnailBuffer,
				"JPG",
				87);

			result["mpris:artUrl"] = Glib::Variant<
				Glib::ustring
			>::create("data:image/jpeg;base64,"
				+ thumbnailData
					.toBase64()
					.toStdString());
		}
	}

	return result;
}

auto PlaybackStatus(Media::Player::State state) {
	return (state == Media::Player::State::Playing)
		? "Playing"
		: Media::Player::IsPausedOrPausing(state)
		? "Paused"
		: "Stopped";
}

void HandleMethodCall(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &method_name,
		const Glib::VariantContainerBase &parameters,
		const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation) {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		try {
			auto parametersCopy = parameters;

			if (method_name == "Quit") {
				if (const auto main = App::main()) {
					main->closeBothPlayers();
				}
			} else if (method_name == "Raise") {
				if (const auto window = App::wnd()) {
					window->showFromTray();
				}
			} else if (method_name == "Next") {
				Media::Player::instance()->next();
			} else if (method_name == "Pause") {
				Media::Player::instance()->pause();
			} else if (method_name == "Play") {
				Media::Player::instance()->play();
			} else if (method_name == "PlayPause") {
				Media::Player::instance()->playPause();
			} else if (method_name == "Previous") {
				Media::Player::instance()->previous();
			} else if (method_name == "Seek") {
				const auto offset = base::Platform::GlibVariantCast<gint64>(
					parametersCopy.get_child(0));

				const auto state = Media::Player::instance()->getState(
					Media::Player::instance()->getActiveType());

				Media::Player::instance()->finishSeeking(
					Media::Player::instance()->getActiveType(),
					float64(state.position * 1000 + offset)
						/ (state.length * 1000));
			} else if (method_name == "SetPosition") {
				const auto position = base::Platform::GlibVariantCast<gint64>(
					parametersCopy.get_child(1));

				const auto state = Media::Player::instance()->getState(
					Media::Player::instance()->getActiveType());

				Media::Player::instance()->finishSeeking(
					Media::Player::instance()->getActiveType(),
					float64(position) / (state.length * 1000));
			} else if (method_name == "Stop") {
				Media::Player::instance()->stop();
			} else {
				return;
			}

			invocation->return_value({});
		} catch (...) {
		}
	});
}

void HandleGetProperty(
		Glib::VariantBase &property,
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &property_name) {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		if (property_name == "CanQuit") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanRaise") {
			property = Glib::Variant<bool>::create(!IsWayland());
		} else if (property_name == "CanSetFullscreen") {
			property = Glib::Variant<bool>::create(false);
		} else if (property_name == "DesktopEntry") {
			property = Glib::Variant<Glib::ustring>::create(
				QGuiApplication::desktopFileName().chopped(8).toStdString());
		} else if (property_name == "Fullscreen") {
			property = Glib::Variant<bool>::create(false);
		} else if (property_name == "HasTrackList") {
			property = Glib::Variant<bool>::create(false);
		} else if (property_name == "Identity") {
			property = Glib::Variant<Glib::ustring>::create(
				std::string(AppName));
		} else if (property_name == "SupportedMimeTypes") {
			property = Glib::Variant<std::vector<Glib::ustring>>::create({});
		} else if (property_name == "SupportedUriSchemes") {
			property = Glib::Variant<std::vector<Glib::ustring>>::create({});
		} else if (property_name == "CanControl") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanGoNext") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanGoPrevious") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanPause") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanPlay") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "CanSeek") {
			property = Glib::Variant<bool>::create(true);
		} else if (property_name == "MaximumRate") {
			property = Glib::Variant<float64>::create(1.0);
		} else if (property_name == "Metadata") {
			const auto state = Media::Player::instance()->getState(
				Media::Player::instance()->getActiveType());

			const auto trackView = [&]() -> std::shared_ptr<Data::DocumentMedia> {
				const auto audioData = state.id.audio();
				if (audioData && audioData->isSongWithCover()) {
					return audioData->activeMediaView();
				}
				return nullptr;
			}();

			property = base::Platform::MakeGlibVariant(
				CreateMetadata(state, trackView.get()));
		} else if (property_name == "MinimumRate") {
			property = Glib::Variant<float64>::create(1.0);
		} else if (property_name == "PlaybackStatus") {
			const auto state = Media::Player::instance()->getState(
				Media::Player::instance()->getActiveType());

			property = Glib::Variant<Glib::ustring>::create(
				PlaybackStatus(state.state));
		} else if (property_name == "Position") {
			const auto state = Media::Player::instance()->getState(
				Media::Player::instance()->getActiveType());

			property = Glib::Variant<gint64>::create(state.position * 1000);
		} else if (property_name == "Rate") {
			property = Glib::Variant<float64>::create(1.0);
		} else if (property_name == "Volume") {
			property = Glib::Variant<float64>::create(
				Core::App().settings().songVolume());
		}
	});
}

bool HandleSetProperty(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &property_name,
		const Glib::VariantBase &value) {
	try {
		if (property_name == "Fullscreen") {
		} else if (property_name == "Rate") {
		} else if (property_name == "Volume") {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				Core::App().settings().setSongVolume(
					base::Platform::GlibVariantCast<float64>(value));
			});
		} else {
			return false;
		}

		return true;
	} catch (...) {
	}

	return false;
}

const Gio::DBus::InterfaceVTable InterfaceVTable(
	sigc::ptr_fun(&HandleMethodCall),
	sigc::ptr_fun(&HandleGetProperty),
	sigc::ptr_fun(&HandleSetProperty));

void PlayerPropertyChanged(
		const Glib::ustring &name,
		const Glib::VariantBase &value) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		connection->emit_signal(
			std::string(kObjectPath),
			std::string(kPropertiesInterface),
			"PropertiesChanged",
			{},
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(kPlayerInterface)),
				std::map<Glib::ustring, Glib::VariantBase>{
					{ name, value },
				},
				std::vector<Glib::ustring>{},
			}));
	} catch (...) {
	}
}

void Seeked(gint64 position) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		connection->emit_signal(
			std::string(kObjectPath),
			std::string(kPlayerInterface),
			"Seeked",
			{},
			base::Platform::MakeGlibVariant(std::tuple{
				position,
			}));
	} catch (...) {
	}
}

} // namespace

class MPRISSupport::Private {
public:
	void updateTrackState(const Media::Player::TrackState &state);

	Glib::RefPtr<Gio::DBus::Connection> dbusConnection;
	Glib::RefPtr<Gio::DBus::NodeInfo> introspectionData;
	Glib::RefPtr<Gio::DBus::NodeInfo> playerIntrospectionData;

	uint ownId = 0;
	uint registerId = 0;
	uint playerRegisterId = 0;

	Glib::ustring playbackStatus;
	gint64 position = 0;

	DocumentData *audioData = nullptr;
	std::shared_ptr<Data::DocumentMedia> trackView;
	Image *thumbnail = nullptr;

	rpl::lifetime lifetime;
};

void MPRISSupport::Private::updateTrackState(
		const Media::Player::TrackState &state) {
	const auto currentAudioData = state.id.audio();
	const auto currentPosition = state.position * 1000;
	const auto currentPlaybackStatus = PlaybackStatus(state.state);

	if (currentAudioData != audioData) {
		audioData = currentAudioData;
		if (audioData && audioData->isSongWithCover()) {
			trackView = audioData->createMediaView();
			thumbnail = trackView->thumbnail();
		} else {
			trackView = nullptr;
			thumbnail = nullptr;
		}

		PlayerPropertyChanged(
			"Metadata",
			Glib::Variant<
				std::map<Glib::ustring, Glib::VariantBase>
			>::create(CreateMetadata(state, trackView.get())));
	}

	if (trackView && (trackView->thumbnail() != thumbnail)) {
		thumbnail = trackView->thumbnail();
		PlayerPropertyChanged(
			"Metadata",
			Glib::Variant<
				std::map<Glib::ustring, Glib::VariantBase>
			>::create(CreateMetadata(state, trackView.get())));
	}

	if (currentPlaybackStatus != playbackStatus) {
		playbackStatus = currentPlaybackStatus;
		PlayerPropertyChanged(
			"PlaybackStatus",
			Glib::Variant<Glib::ustring>::create(playbackStatus));
	}

	if (currentPosition != position) {
		const auto positionDifference = position - currentPosition;
		if (positionDifference > 1000000 || positionDifference < -1000000) {
			Seeked(currentPosition);
		}

		position = currentPosition;
	}
}

MPRISSupport::MPRISSupport()
: _private(std::make_unique<Private>()) {
	try {
		_private->introspectionData = Gio::DBus::NodeInfo::create_for_xml(
			std::string(kIntrospectionXML));

		_private->playerIntrospectionData = Gio::DBus::NodeInfo::create_for_xml(
			std::string(kPlayerIntrospectionXML));

		_private->ownId = Gio::DBus::own_name(
			Gio::DBus::BusType::BUS_TYPE_SESSION,
			std::string(kService));

		_private->dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		_private->registerId = _private->dbusConnection->register_object(
			  std::string(kObjectPath),
			_private->introspectionData->lookup_interface(),
			InterfaceVTable);

		_private->playerRegisterId = _private->dbusConnection->register_object(
			  std::string(kObjectPath),
			_private->playerIntrospectionData->lookup_interface(),
			InterfaceVTable);

		_private->updateTrackState(
			Media::Player::instance()->getState(
				Media::Player::instance()->getActiveType()));

		Core::App().domain().active().session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			_private->updateTrackState(
				Media::Player::instance()->getState(
					Media::Player::instance()->getActiveType()));
		}, _private->lifetime);

		Media::Player::instance()->updatedNotifier(
		) | rpl::filter([=](const Media::Player::TrackState &state) {
			return state.id.type() == Media::Player::instance()->getActiveType();
		}) | rpl::start_with_next([=](
				const Media::Player::TrackState &state) {
			_private->updateTrackState(state);
		}, _private->lifetime);

		Core::App().settings().songVolumeChanges(
		) | rpl::start_with_next([=](float64 volume) {
			PlayerPropertyChanged(
				"Volume",
				Glib::Variant<float64>::create(volume));
		}, _private->lifetime);
	} catch (...) {
	}
}

MPRISSupport::~MPRISSupport() {
	if (_private->dbusConnection) {
		if (_private->playerRegisterId) {
			_private->dbusConnection->unregister_object(
				_private->playerRegisterId);
		}

		if (_private->registerId) {
			_private->dbusConnection->unregister_object(
				_private->registerId);
		}
	}

	if (_private->ownId) {
		Gio::DBus::unown_name(_private->ownId);
	}
}

} // namespace internal
} // namespace Platform
