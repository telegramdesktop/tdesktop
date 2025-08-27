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

class AbstractButton;
class SeparatePanel;
class RpWidget;
class ScrollArea;
class VerticalLayout;
template <typename Widget>
class SlideWrap;

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

struct LocationPickerConfig {
	QString mapsToken;
	QString geoToken;
};

class LocationPicker final : public base::has_weak_ptr {
public:
	struct Descriptor {
		RpWidget *parent = nullptr;
		LocationPickerConfig config;
		rpl::producer<QString> chooseLabel;
		PeerData *recipient = nullptr;
		not_null<Main::Session*> session;
		Core::GeoLocation initial;
		Fn<void(Data::InputVenue)> callback;
		Fn<void()> quit;
		Webview::StorageId storageId;
		rpl::producer<> closeRequests;
	};

	[[nodiscard]] static bool Available(const LocationPickerConfig &config);
	static not_null<LocationPicker*> Show(Descriptor &&descriptor);

	void activate();
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
	void setupWebview();
	void processKey(const QString &key, const QString &modifier);
	void resolveCurrentLocation();
	void resolveAddressByTimer();
	void resolveAddress(Core::GeoLocation location);
	void mapReady();

	bool venuesFromCache(Core::GeoLocation location, QString query = {});
	void venuesRequest(Core::GeoLocation location, QString query = {});
	void venuesSendRequest();
	void venuesApplyResults(PickerVenueList venues);
	void venuesSearchEnableAt(Core::GeoLocation location);
	void venuesSearchChanged(const std::optional<QString> &query);

	LocationPickerConfig _config;
	Fn<void(Data::InputVenue)> _callback;
	Fn<void()> _quit;
	std::unique_ptr<SeparatePanel> _window;
	not_null<RpWidget*> _body;
	RpWidget *_container = nullptr;
	RpWidget *_mapPlaceholder = nullptr;
	RpWidget *_mapLoading = nullptr;
	AbstractButton *_mapButton = nullptr;
	SlideWrap<VerticalLayout> *_mapControlsWrap = nullptr;
	rpl::variable<QString> _chooseButtonLabel;
	ScrollArea *_scroll = nullptr;
	Webview::StorageId _webviewStorageId;
	std::unique_ptr<Webview::Window> _webview;
	SingleQueuedInvokation _updateStyles;
	Core::GeoLocation _initialProvided;
	int _mapPlaceholderAdded = 0;
	bool _subscribedToColors = false;

	base::Timer _geocoderResolveTimer;
	Core::GeoLocation _geocoderResolvePostponed;
	Core::GeoLocation _geocoderResolvingFor;
	QString _geocoderSavedAddress;
	rpl::variable<QString> _geocoderAddress;

	rpl::variable<PickerVenueState> _venueState;

	const not_null<Main::Session*> _session;
	std::optional<Core::GeoLocation> _venuesSearchLocation;
	std::optional<QString> _venuesSearchQuery;
	base::Timer _venuesSearchDebounceTimer;
	MTP::Sender _api;
	PeerData *_venueRecipient = nullptr;
	UserData *_venuesBot = nullptr;
	mtpRequestId _venuesBotRequestId = 0;
	mtpRequestId _venuesRequestId = 0;
	Core::GeoLocation _venuesRequestLocation;
	QString _venuesRequestQuery;
	QString _venuesInitialQuery;
	base::flat_map<QString, std::vector<VenuesCacheEntry>> _venuesCache;
	Core::GeoLocation _venuesNoSearchLocation;
	rpl::variable<bool> _venuesSearchShown = false;

	rpl::lifetime _lifetime;

};

} // namespace Ui
