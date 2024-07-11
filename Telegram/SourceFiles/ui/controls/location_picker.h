/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/current_geo_location.h"
#include "mtproto/sender.h"
#include "webview/webview_common.h"

namespace Data {
struct InputVenue;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Webview {
class Window;
} // namespace Webview

namespace Ui {

class SeparatePanel;
class RpWidget;
class ScrollArea;

struct LocationInfo {
	float64 lat = 0.;
	float64 lon = 0.;
};

struct PickerVenueLoading {
	friend inline bool operator==(
		PickerVenueLoading,
		PickerVenueLoading) = default;
};

struct PickerVenueNothingFound {
	QString query;

	friend inline bool operator==(
		const PickerVenueNothingFound&,
		const PickerVenueNothingFound&) = default;
};

struct PickerVenueWaitingForLocation {
	friend inline bool operator==(
		PickerVenueWaitingForLocation,
		PickerVenueWaitingForLocation) = default;
};

struct PickerVenueList {
	std::vector<Data::InputVenue> list;

	friend inline bool operator==(
		const PickerVenueList&,
		const PickerVenueList&) = default;
};

using PickerVenueState = std::variant<
	PickerVenueLoading,
	PickerVenueNothingFound,
	PickerVenueWaitingForLocation,
	PickerVenueList>;

class LocationPicker final : public base::has_weak_ptr {
public:
	struct Descriptor {
		RpWidget *parent = nullptr;
		not_null<Main::Session*> session;
		Fn<void(LocationInfo)> callback;
		Fn<void()> quit;
		Webview::StorageId storageId;
		rpl::producer<> closeRequests;
	};

	[[nodiscard]] static bool Available(
		const QString &mapsToken,
		const QString &geocodingToken);
	static not_null<LocationPicker*> Show(Descriptor &&descriptor);

	void close();
	void minimize();
	void quit();

private:
	struct VenuesCacheEntry {
		Core::GeoLocation location;
		PickerVenueList result;
	};

	explicit LocationPicker(Descriptor &&descriptor);

	[[nodiscard]] std::shared_ptr<Main::SessionShow> uiShow();

	void setup(const Descriptor &descriptor);
	void setupWindow(const Descriptor &descriptor);
	void setupWebview(const Descriptor &descriptor);
	void processKey(const QString &key, const QString &modifier);
	void resolveCurrentLocation();
	void resolveAddressByTimer();
	void resolveAddress(Core::GeoLocation location);
	void mapReady();

	void venuesRequest(Core::GeoLocation location, QString query = {});
	void venuesSendRequest();

	rpl::lifetime _lifetime;

	Fn<void(LocationInfo)> _callback;
	Fn<void()> _quit;
	std::unique_ptr<SeparatePanel> _window;
	not_null<RpWidget*> _body;
	RpWidget *_container = nullptr;
	ScrollArea *_scroll = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	SingleQueuedInvokation _updateStyles;
	bool _subscribedToColors = false;

	base::Timer _geocoderResolveTimer;
	Core::GeoLocation _geocoderResolvePostponed;
	Core::GeoLocation _geocoderResolvingFor;
	rpl::variable<QString> _geocoderAddress;

	rpl::variable<PickerVenueState> _venueState;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	UserData *_venuesBot = nullptr;
	mtpRequestId _venuesBotRequestId = 0;
	mtpRequestId _venuesRequestId = 0;
	Core::GeoLocation _venuesRequestLocation;
	QString _venuesRequestQuery;
	base::flat_map<QString, std::vector<VenuesCacheEntry>> _venuesCache;

};

} // namespace Ui
