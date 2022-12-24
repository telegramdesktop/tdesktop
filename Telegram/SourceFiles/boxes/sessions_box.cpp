/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sessions_box.h"

#include "apiwrap.h"
#include "api/api_authorizations.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "base/algorithm.h"
#include "base/platform/base_platform_info.h"
#include "boxes/self_destruction_box.h"
#include "boxes/peer_lists_box.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "lottie/lottie_icon.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kSessionsShortPollTimeout = 60 * crl::time(1000);
constexpr auto kMaxDeviceModelLength = 32;

using EntryData = Api::Authorizations::Entry;

enum class Type {
	Windows,
	Mac,
	Ubuntu,
	Linux,
	iPhone,
	iPad,
	Android,
	Web,
	Chrome,
	Edge,
	Firefox,
	Safari,
	Other,
};

class Row;

class RowDelegate {
public:
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
};

class Row final : public PeerListRow {
public:
	Row(not_null<RowDelegate*> delegate, const EntryData &data);

	void update(const EntryData &data);
	void updateName(const QString &name);

	[[nodiscard]] EntryData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback() override;

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	const not_null<RowDelegate*> _delegate;
	Ui::Text::String _location;
	Type _type = Type::Other;
	EntryData _data;
	QImage _userpic;

};

void RenameBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_settings_rename_device_title());

	const auto skip = st::settingsSubsectionTitlePadding.top();
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_settings_device_name(),
			st::settingsSubsectionTitle),
		st::boxRowPadding + style::margins(0, skip, 0, 0));
	const auto name = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::settingsDeviceName,
			rpl::single(Platform::DeviceModelPretty()),
			Core::App().settings().customDeviceModel()),
		st::boxRowPadding - style::margins(
			st::settingsDeviceName.textMargins.left(),
			0,
			st::settingsDeviceName.textMargins.right(),
			0));
	name->setMaxLength(kMaxDeviceModelLength);
	box->setFocusCallback([=] {
		name->setFocusFast();
	});
	const auto submit = [=] {
		const auto result = base::CleanAndSimplify(
			name->getLastText());
		box->closeBox();
		Core::App().settings().setCustomDeviceModel(result);
		Core::App().saveSettingsDelayed();
	};
	QObject::connect(name, &Ui::InputField::submitted, submit);
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] QString LocationAndDate(const EntryData &entry) {
	return (entry.location.isEmpty() ? entry.ip : entry.location)
		+ (entry.hash
			? (QString::fromUtf8(" \xE2\x80\xA2 ") + entry.active)
			: QString());
}

[[nodiscard]] Type TypeFromEntry(const EntryData &entry) {
	const auto platform = entry.platform.toLower();
	const auto device = entry.name.toLower();
	const auto system = entry.system.toLower();
	const auto apiId = entry.apiId;
	const auto kDesktop = std::array{ 2040, 17349, 611335 };
	const auto kMac = std::array{ 2834 };
	const auto kAndroid
		= std::array{ 5, 6, 24, 1026, 1083, 2458, 2521, 21724 };
	const auto kiOS = std::array{ 1, 7, 10840, 16352 };
	const auto kWeb = std::array{ 2496, 739222, 1025907 };

	const auto detectBrowser = [&]() -> std::optional<Type> {
		if (device.contains("edg/")
			|| device.contains("edgios/")
			|| device.contains("edga/")) {
			return Type::Edge;
		} else if (device.contains("chrome")) {
			return Type::Chrome;
		} else if (device.contains("safari")) {
			return Type::Safari;
		} else if (device.contains("firefox")) {
			return Type::Firefox;
		}
		return {};
	};
	const auto detectDesktop = [&]() -> std::optional<Type> {
		if (platform.contains("windows") || system.contains("windows")) {
			return Type::Windows;
		} else if (platform.contains("macos") || system.contains("macos")) {
			return Type::Mac;
		} else if (platform.contains("ubuntu")
			|| system.contains("ubuntu")
			|| platform.contains("unity")
			|| system.contains("unity")) {
			return Type::Ubuntu;
		} else if (platform.contains("linux") || system.contains("linux")) {
			return Type::Linux;
		}
		return {};
	};

	if (ranges::contains(kAndroid, apiId)) {
		return Type::Android;
	} else if (ranges::contains(kDesktop, apiId)) {
		return detectDesktop().value_or(Type::Linux);
	} else if (ranges::contains(kMac, apiId)) {
		return Type::Mac;
	} else if (ranges::contains(kWeb, apiId)) {
		return detectBrowser().value_or(Type::Web);
	} else if (device.contains("chromebook")) {
		return Type::Other;
	} else if (const auto browser = detectBrowser()) {
		return *browser;
    } else if (device.contains("iphone")) {
		return Type::iPhone;
	} else if (device.contains("ipad")) {
		return Type::iPad;
	} else if (ranges::contains(kiOS, apiId)) {
		return Type::iPhone;
	} else if (const auto desktop = detectDesktop()) {
		return *desktop;
	} else if (platform.contains("android") || system.contains("android")) {
		return Type::Android;
    } else if (platform.contains("ios") || system.contains("ios")) {
		return Type::iPhone;
    }
	return Type::Other;
}

[[nodiscard]] style::color ColorForType(Type type) {
	switch (type) {
	case Type::Windows:
	case Type::Mac:
	case Type::Other:
		return st::historyPeer4UserpicBg; // blue
	case Type::Ubuntu:
		return st::historyPeer8UserpicBg; // orange
	case Type::Linux:
		return st::historyPeer5UserpicBg; // purple
	case Type::iPhone:
	case Type::iPad:
		return st::historyPeer7UserpicBg; // sea
	case Type::Android:
		return st::historyPeer2UserpicBg; // green
	case Type::Web:
	case Type::Chrome:
	case Type::Edge:
	case Type::Firefox:
	case Type::Safari:
		return st::historyPeer6UserpicBg; // pink
	}
	Unexpected("Type in ColorForType.");
}

[[nodiscard]] const style::icon &IconForType(Type type) {
	switch (type) {
	case Type::Windows: return st::sessionIconWindows;
	case Type::Mac: return st::sessionIconMac;
	case Type::Ubuntu: return st::sessionIconUbuntu;
	case Type::Linux: return st::sessionIconLinux;
	case Type::iPhone: return st::sessionIconiPhone;
	case Type::iPad: return st::sessionIconiPad;
	case Type::Android: return st::sessionIconAndroid;
	case Type::Web: return st::sessionIconWeb;
	case Type::Chrome: return st::sessionIconChrome;
	case Type::Edge: return st::sessionIconEdge;
	case Type::Firefox: return st::sessionIconFirefox;
	case Type::Safari: return st::sessionIconSafari;
	case Type::Other: return st::sessionIconOther;
	}
	Unexpected("Type in IconForType.");
}

[[nodiscard]] const style::icon *IconBigForType(Type type) {
	switch (type) {
	case Type::Web: return &st::sessionBigIconWeb;
	case Type::Other: return &st::sessionBigIconOther;
	}
	return nullptr;
}

[[nodiscard]] std::unique_ptr<Lottie::Icon> LottieForType(Type type) {
	if (IconBigForType(type)) {
		return nullptr;
	}
	const auto path = [&] {
		switch (type) {
		case Type::Windows: return "device_desktop_win";
		case Type::Mac: return "device_desktop_mac";
		case Type::Ubuntu: return "device_linux_ubuntu";
		case Type::Linux: return "device_linux";
		case Type::iPhone: return "device_phone_ios";
		case Type::iPad: return "device_tablet_ios";
		case Type::Android: return "device_phone_android";
		case Type::Chrome: return "device_web_chrome";
		case Type::Edge: return "device_web_edge";
		case Type::Firefox: return "device_web_firefox";
		case Type::Safari: return "device_web_safari";
		}
		Unexpected("Type in LottieForType.");
	}();
	const auto size = st::sessionBigLottieSize;
	return Lottie::MakeIcon({
		.path = u":/icons/settings/devices/"_q + path + u".lottie"_q,
		.sizeOverride = QSize(size, size),
	});
}

[[nodiscard]] QImage GenerateUserpic(Type type) {
	const auto size = st::sessionListItem.photoSize;
	const auto full = size * style::DevicePixelRatio();
	const auto rect = QRect(0, 0, size, size);

	auto result = QImage(full, full, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(ColorForType(type));
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	IconForType(type).paintInCenter(p, rect);
	p.end();

	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> GenerateUserpicBig(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<> shown,
		Type type) {
	const auto size = st::sessionBigUserpicSize;
	const auto full = size * style::DevicePixelRatio();
	const auto rect = QRect(0, 0, size, size);

	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->resize(rect.size());
	struct State {
		QImage background;
		std::unique_ptr<Lottie::Icon> lottie;
		QImage lottieFrame;
		QImage colorizedFrame;
	};
	const auto state = result->lifetime().make_state<State>();
	state->background = QImage(
		full,
		full,
		QImage::Format_ARGB32_Premultiplied);
	state->background.fill(Qt::transparent);
	state->background.setDevicePixelRatio(style::DevicePixelRatio());
	state->colorizedFrame = state->lottieFrame = state->background;

	auto p = QPainter(&state->background);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(ColorForType(type));
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	if (const auto icon = IconBigForType(type)) {
		icon->paintInCenter(p, rect);
	}
	p.end();

	if ((state->lottie = LottieForType(type))) {
		std::move(
			shown
		) | rpl::start_with_next([=] {
			state->lottie->animate(
				[=] { result->update(); },
				0,
				state->lottie->framesCount() - 1);
		}, result->lifetime());
	}

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(result);
		p.drawImage(QPoint(0, 0), state->background);
		if (state->lottie) {
			state->lottieFrame.fill(Qt::black);
			auto q = QPainter(&state->lottieFrame);
			state->lottie->paintInCenter(q, result->rect());
			q.end();
			style::colorizeImage(
				state->lottieFrame,
				st::historyPeerUserpicFg->c,
				&state->colorizedFrame);
			p.drawImage(QPoint(0, 0), state->colorizedFrame);

		}
	}, result->lifetime());

	return result;
}

void SessionInfoBox(
		not_null<Ui::GenericBox*> box,
		const EntryData &data,
		Fn<void(uint64)> terminate) {
	box->setWidth(st::boxWideWidth);

	const auto shown = box->lifetime().make_state<rpl::event_stream<>>();
	box->setShowFinishedCallback([=] {
		shown->fire({});
	});

	const auto userpicWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::sessionBigUserpicSize),
		st::sessionBigCoverPadding);
	const auto big = GenerateUserpicBig(
		userpicWrap,
		shown->events(),
		TypeFromEntry(data));
	userpicWrap->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		big->move((size.width() - big->width()) / 2, 0);
	}, userpicWrap->lifetime());

	const auto nameWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::sessionBigName.maxHeight));
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		nameWrap,
		rpl::single(data.name),
		st::sessionBigName);
	nameWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		name->resizeToWidth(width);
		name->move((width - name->width()) / 2, 0);
	}, name->lifetime());

	const auto dateWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::sessionDateLabel.style.font->height),
		style::margins(0, 0, 0, st::sessionDateSkip));
	const auto date = Ui::CreateChild<Ui::FlatLabel>(
		dateWrap,
		rpl::single(
			langDateTimeFull(base::unixtime::parse(data.activeTime))),
		st::sessionDateLabel);
	rpl::combine(
		dateWrap->widthValue(),
		date->widthValue()
	) | rpl::start_with_next([=](int outer, int inner) {
		date->move((outer - inner) / 2, 0);
	}, date->lifetime());

	using namespace Settings;
	const auto container = box->verticalLayout();
	AddDivider(container);
	AddSkip(container, st::sessionSubtitleSkip);
	AddSubsectionTitle(container, tr::lng_sessions_info());

	const auto add = [&](rpl::producer<QString> label, QString value) {
		if (value.isEmpty()) {
			return;
		}
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(value),
				st::boxLabel),
			st::boxRowPadding + st::sessionValuePadding);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(label),
				st::sessionValueLabel),
			(st::boxRowPadding
				+ style::margins{ 0, 0, 0, st::sessionValueSkip }));
	};
	add(tr::lng_sessions_application(), data.info);
	add(tr::lng_sessions_system(), data.system);
	add(tr::lng_sessions_ip(), data.ip);
	add(tr::lng_sessions_location(), data.location);
	AddSkip(container, st::sessionValueSkip);
	if (!data.location.isEmpty()) {
		AddDividerText(container, tr::lng_sessions_location_about());
	}

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	if (const auto hash = data.hash) {
		box->addLeftButton(tr::lng_sessions_terminate(), [=] {
			const auto weak = Ui::MakeWeak(box.get());
			terminate(hash);
			if (weak) {
				box->closeBox();
			}
		}, st::attentionBoxButton);
	}
}

Row::Row(not_null<RowDelegate*> delegate, const EntryData &data)
: PeerListRow(data.hash)
, _delegate(delegate)
, _location(st::defaultTextStyle, LocationAndDate(data))
, _type(TypeFromEntry(data))
, _data(data)
, _userpic(GenerateUserpic(_type)) {
	setCustomStatus(_data.info);
}

void Row::update(const EntryData &data) {
	_data = data;
	setCustomStatus(_data.info);
	refreshName(st::sessionListItem);
	_location.setText(st::defaultTextStyle, LocationAndDate(_data));
	_type = TypeFromEntry(_data);
	_userpic = GenerateUserpic(_type);
	_delegate->rowUpdateRow(this);
}

void Row::updateName(const QString &name) {
	_data.name = name;
	refreshName(st::sessionListItem);
	_delegate->rowUpdateRow(this);
}

EntryData Row::data() const {
	return _data;
}

QString Row::generateName() {
	return _data.name;
}

QString Row::generateShortName() {
	return generateName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback() {
	return [=](
			QPainter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		p.drawImage(x, y, _userpic);
	};
}

int Row::elementsCount() const {
	return 2;
}

QRect Row::elementGeometry(int element, int outerWidth) const {
	switch (element) {
	case 1: {
		return QRect(
			st::sessionListItem.namePosition.x(),
			st::sessionLocationTop,
			outerWidth,
			st::normalFont->height);
	} break;
	case 2: {
		const auto size = QSize(
			st::sessionTerminate.width,
			st::sessionTerminate.height);
		const auto right = st::sessionTerminateSkip;
		const auto top = st::sessionTerminateTop;
		const auto left = outerWidth - right - size.width();
		return QRect(QPoint(left, top), size);
	} break;
	}
	return QRect();
}

bool Row::elementDisabled(int element) const {
	return !id() || (element == 1);
}

bool Row::elementOnlySelect(int element) const {
	return false;
}

void Row::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
}

void Row::elementsStopLastRipple() {
}

void Row::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	if (id()) {
		const auto geometry = elementGeometry(2, outerWidth);
		const auto position = geometry.topLeft()
			+ st::sessionTerminate.iconPosition;
		const auto &icon = (selectedElement == 2)
			? st::sessionTerminate.iconOver
			: st::sessionTerminate.icon;
		icon.paint(p, position.x(), position.y(), outerWidth);
	}
	p.setFont(st::normalFont);
	p.setPen(st::sessionInfoFg);
	const auto locationLeft = st::sessionListItem.namePosition.x();
	const auto available = outerWidth - locationLeft;
	_location.drawLeftElided(
		p,
		locationLeft,
		st::sessionLocationTop,
		available,
		outerWidth);
}

} // namespace

class SessionsContent : public Ui::RpWidget {
public:
	SessionsContent(
		QWidget*,
		not_null<Window::SessionController*> controller);

	void setupContent();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Full {
		EntryData current;
		std::vector<EntryData> incomplete;
		std::vector<EntryData> list;
	};
	class Inner;
	class ListController;

	void shortPollSessions();
	void parse(const Api::Authorizations::List &list);

	void terminate(Fn<void()> terminateRequest, QString message);
	void terminateOne(uint64 hash);
	void terminateAll();

	const not_null<Window::SessionController*> _controller;
	const not_null<Api::Authorizations*> _authorizations;

	rpl::variable<bool> _loading = false;
	Full _data;

	object_ptr<Inner> _inner;
	QPointer<Ui::BoxContent> _terminateBox;

	base::Timer _shortPollTimer;

};

class SessionsContent::ListController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	explicit ListController(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(not_null<PeerListRow*> row, int element) override;

	void rowUpdateRow(not_null<Row*> row) override;

	void showData(gsl::span<const EntryData> items);
	rpl::producer<int> itemsCount() const;
	rpl::producer<uint64> terminateRequests() const;
	[[nodiscard]] rpl::producer<EntryData> showRequests() const;

	[[nodiscard]] static std::unique_ptr<ListController> Add(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		style::margins margins = {});

private:
	void subscribeToCustomDeviceModel();

	const not_null<Main::Session*> _session;

	rpl::event_stream<uint64> _terminateRequests;
	rpl::event_stream<int> _itemsCount;
	rpl::event_stream<EntryData> _showRequests;

};

class SessionsContent::Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<int> ttlDays);

	void showData(const Full &data);
	[[nodiscard]] rpl::producer<EntryData> showRequests() const;
	[[nodiscard]] rpl::producer<uint64> terminateOne() const;
	[[nodiscard]] rpl::producer<> terminateAll() const;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	std::unique_ptr<ListController> _current;
	QPointer<Ui::SettingsButton> _terminateAll;
	std::unique_ptr<ListController> _incomplete;
	std::unique_ptr<ListController> _list;
	rpl::variable<int> _ttlDays;

};

//, location(st::sessionInfoStyle, LocationAndDate(entry))

SessionsContent::SessionsContent(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _authorizations(&controller->session().api().authorizations())
, _inner(this, controller, _authorizations->ttlDays())
, _shortPollTimer([=] { shortPollSessions(); }) {
}

void SessionsContent::setupContent() {
	_inner->resize(width(), st::noContactsHeight);

	_inner->heightValue(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, _inner->lifetime());

	_inner->showRequests(
	) | rpl::start_with_next([=](const EntryData &data) {
		_controller->show(Box(
			SessionInfoBox,
			data,
			[=](uint64 hash) { terminateOne(hash); }));
	}, lifetime());

	_inner->terminateOne(
	) | rpl::start_with_next([=](uint64 hash) {
		terminateOne(hash);
	}, lifetime());

	_inner->terminateAll(
	) | rpl::start_with_next([=] {
		terminateAll();
	}, lifetime());

	_loading.changes(
	) | rpl::start_with_next([=](bool value) {
		_inner->setVisible(!value);
	}, lifetime());

	_authorizations->listChanges(
	) | rpl::start_with_next([=](const Api::Authorizations::List &list) {
		parse(list);
	}, lifetime());

	_loading = true;
	shortPollSessions();
}

void SessionsContent::parse(const Api::Authorizations::List &list) {
	if (list.empty()) {
		return;
	}
	_data = Full();
	for (const auto &auth : list) {
		if (!auth.hash) {
			_data.current = auth;
		} else if (auth.incomplete) {
			_data.incomplete.push_back(auth);
		} else {
			_data.list.push_back(auth);
		}
	}

	_loading = false;

	ranges::sort(_data.list, std::greater<>(), &EntryData::activeTime);
	ranges::sort(_data.incomplete, std::greater<>(), &EntryData::activeTime);

	_inner->showData(_data);

	_shortPollTimer.callOnce(kSessionsShortPollTimeout);
}

void SessionsContent::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void SessionsContent::paintEvent(QPaintEvent *e) {
	RpWidget::paintEvent(e);

	Painter p(this);

	if (_loading.current()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			QRect(0, 0, width(), st::noContactsHeight),
			tr::lng_contacts_loading(tr::now),
			style::al_center);
	}
}

void SessionsContent::shortPollSessions() {
	const auto left = kSessionsShortPollTimeout
		- (crl::now() - _authorizations->lastReceivedTime());
	if (left > 0) {
		parse(_authorizations->list());
		_shortPollTimer.cancel();
		_shortPollTimer.callOnce(left);
	} else {
		_authorizations->reload();
	}
	update();
}

void SessionsContent::terminate(Fn<void()> terminateRequest, QString message) {
	if (_terminateBox) {
		_terminateBox->deleteLater();
	}
	const auto callback = crl::guard(this, [=] {
		if (_terminateBox) {
			_terminateBox->closeBox();
			_terminateBox = nullptr;
		}
		terminateRequest();
	});
	auto box = Ui::MakeConfirmBox({
		.text = message,
		.confirmed = callback,
		.confirmText = tr::lng_settings_reset_button(),
		.confirmStyle = &st::attentionBoxButton,
	});
	_terminateBox = Ui::MakeWeak(box.data());
	_controller->show(std::move(box), Ui::LayerOption::KeepOther);
}

void SessionsContent::terminateOne(uint64 hash) {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=] {
		auto done = crl::guard(weak, [=](const MTPBool &result) {
			if (mtpIsFalse(result)) {
				return;
			}
			const auto removeByHash = [&](std::vector<EntryData> &list) {
				list.erase(
					ranges::remove(
						list,
						hash,
						[](const EntryData &entry) { return entry.hash; }),
					end(list));
			};
			removeByHash(_data.incomplete);
			removeByHash(_data.list);
			_inner->showData(_data);
		});
		auto fail = crl::guard(weak, [=](const MTP::Error &error) {
		});
		_authorizations->requestTerminate(
			std::move(done),
			std::move(fail),
			hash);
	};
	terminate(std::move(callback), tr::lng_settings_reset_one_sure(tr::now));
}

void SessionsContent::terminateAll() {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=] {
		const auto reset = crl::guard(weak, [=] {
			_authorizations->cancelCurrentRequest();
			_authorizations->reload();
		});
		_authorizations->requestTerminate(
			[=](const MTPBool &result) { reset(); },
			[=](const MTP::Error &result) { reset(); });
		_loading = true;
	};
	terminate(std::move(callback), tr::lng_settings_reset_sure(tr::now));
}

SessionsContent::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<int> ttlDays)
: RpWidget(parent)
, _controller(controller)
, _ttlDays(std::move(ttlDays)) {
	setupContent();
}

void SessionsContent::Inner::setupContent() {
	using namespace Settings;
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto header = AddSubsectionTitle(
		content,
		tr::lng_sessions_header());
	const auto rename = Ui::CreateChild<Ui::LinkButton>(
		content,
		tr::lng_settings_rename_device(tr::now),
		st::defaultLinkButton);
	rpl::combine(
		content->sizeValue(),
		header->positionValue()
	) | rpl::start_with_next([=](QSize outer, QPoint position) {
		const auto x = st::sessionTerminateSkip
			+ st::sessionTerminate.iconPosition.x();
		const auto y = st::settingsSubsectionTitlePadding.top()
			+ st::settingsSubsectionTitle.style.font->ascent
			- st::defaultLinkButton.font->ascent;
		rename->moveToRight(x, y, outer.width());
	}, rename->lifetime());
	rename->setClickedCallback([=] {
		_controller->show(Box(RenameBox), Ui::LayerOption::KeepOther);
	});

	const auto session = &_controller->session();
	_current = ListController::Add(
		content,
		session,
		style::margins{ 0, 0, 0, st::sessionCurrentSkip });
	const auto terminateWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto terminateInner = terminateWrap->entity();
	_terminateAll = terminateInner->add(
		CreateButton(
			terminateInner,
			tr::lng_sessions_terminate_all(),
			st::infoBlockButton,
			{ .icon = &st::infoIconBlock }));
	AddSkip(terminateInner);
	AddDividerText(terminateInner, tr::lng_sessions_terminate_all_about());

	const auto incompleteWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto incompleteInner = incompleteWrap->entity();
	AddSkip(incompleteInner, st::sessionSubtitleSkip);
	AddSubsectionTitle(incompleteInner, tr::lng_sessions_incomplete());
	_incomplete = ListController::Add(incompleteInner, session);
	AddSkip(incompleteInner);
	AddDividerText(incompleteInner, tr::lng_sessions_incomplete_about());

	const auto listWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto listInner = listWrap->entity();
	AddSkip(listInner, st::sessionSubtitleSkip);
	AddSubsectionTitle(listInner, tr::lng_sessions_other_header());
	_list = ListController::Add(listInner, session);
	AddSkip(listInner);
	AddDividerText(listInner, tr::lng_sessions_about_apps());

	const auto ttlWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto ttlInner = ttlWrap->entity();
	AddSkip(ttlInner, st::sessionSubtitleSkip);
	AddSubsectionTitle(ttlInner, tr::lng_settings_terminate_title());

	AddButtonWithLabel(
		ttlInner,
		tr::lng_settings_terminate_if(),
		_ttlDays.value() | rpl::map(SelfDestructionBox::DaysLabel),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		_controller->show(Box<SelfDestructionBox>(
			&_controller->session(),
			SelfDestructionBox::Type::Sessions,
			_ttlDays.value()));
	});

	AddSkip(ttlInner);

	const auto placeholder = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_sessions_other_desc(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding))->setDuration(0);

	terminateWrap->toggleOn(
		rpl::combine(
			_incomplete->itemsCount(),
			_list->itemsCount(),
			(_1 + _2) > 0));
	incompleteWrap->toggleOn(_incomplete->itemsCount() | rpl::map(_1 > 0));
	listWrap->toggleOn(_list->itemsCount() | rpl::map(_1 > 0));
	ttlWrap->toggleOn(_list->itemsCount() | rpl::map(_1 > 0));
	placeholder->toggleOn(_list->itemsCount() | rpl::map(_1 == 0));

	Ui::ResizeFitChild(this, content);
}

void SessionsContent::Inner::showData(const Full &data) {
	_current->showData({ &data.current, &data.current + 1 });
	_list->showData(data.list);
	_incomplete->showData(data.incomplete);
}

rpl::producer<> SessionsContent::Inner::terminateAll() const {
	return _terminateAll->clicks() | rpl::to_empty;
}

rpl::producer<uint64> SessionsContent::Inner::terminateOne() const {
	return rpl::merge(
		_incomplete->terminateRequests(),
		_list->terminateRequests());
}

rpl::producer<EntryData> SessionsContent::Inner::showRequests() const {
	return rpl::merge(
		_current->showRequests(),
		_incomplete->showRequests(),
		_list->showRequests());
}

SessionsContent::ListController::ListController(
	not_null<Main::Session*> session)
: _session(session) {
}

Main::Session &SessionsContent::ListController::session() const {
	return *_session;
}

void SessionsContent::ListController::subscribeToCustomDeviceModel() {
	Core::App().settings().deviceModelChanges(
	) | rpl::start_with_next([=](const QString &model) {
		for (auto i = 0; i != delegate()->peerListFullRowsCount(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			if (!row->id()) {
				static_cast<Row*>(row.get())->updateName(model);
			}
		}
	}, lifetime());
}

void SessionsContent::ListController::prepare() {
}

void SessionsContent::ListController::rowClicked(
		not_null<PeerListRow*> row) {
	_showRequests.fire_copy(static_cast<Row*>(row.get())->data());
}

void SessionsContent::ListController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	if (element == 2) {
		if (const auto hash = static_cast<Row*>(row.get())->data().hash) {
			_terminateRequests.fire_copy(hash);
		}
	}
}

void SessionsContent::ListController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void SessionsContent::ListController::showData(
		gsl::span<const EntryData> items) {
	auto index = 0;
	auto positions = base::flat_map<uint64, int>();
	positions.reserve(items.size());
	for (const auto &entry : items) {
		const auto id = entry.hash;
		positions.emplace(id, index++);
		if (const auto row = delegate()->peerListFindRow(id)) {
			static_cast<Row*>(row)->update(entry);
		} else {
			delegate()->peerListAppendRow(
				std::make_unique<Row>(this, entry));
		}
	}
	for (auto i = 0; i != delegate()->peerListFullRowsCount();) {
		const auto row = delegate()->peerListRowAt(i);
		if (positions.contains(row->id())) {
			++i;
			continue;
		}
		delegate()->peerListRemoveRow(row);
	}
	delegate()->peerListSortRows([&](
			const PeerListRow &a,
			const PeerListRow &b) {
		return positions[a.id()] < positions[b.id()];
	});
	delegate()->peerListRefreshRows();
	_itemsCount.fire(delegate()->peerListFullRowsCount());
}

rpl::producer<int> SessionsContent::ListController::itemsCount() const {
	return _itemsCount.events_starting_with(
		delegate()->peerListFullRowsCount());
}

rpl::producer<uint64> SessionsContent::ListController::terminateRequests() const {
	return _terminateRequests.events();
}

rpl::producer<EntryData> SessionsContent::ListController::showRequests() const {
	return _showRequests.events();
}

auto SessionsContent::ListController::Add(
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	style::margins margins)
-> std::unique_ptr<ListController> {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	auto controller = std::make_unique<ListController>(session);
	controller->setStyleOverrides(&st::sessionList);
	const auto content = container->add(
		object_ptr<PeerListContent>(
			container,
			controller.get()),
		margins);
	delegate->setContent(content);
	controller->setDelegate(delegate);
	return controller;
}

SessionsBox::SessionsBox(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void SessionsBox::prepare() {
	setTitle(tr::lng_sessions_other_header());

	addButton(tr::lng_close(), [=] { closeBox(); });

	const auto w = st::boxWideWidth;

	const auto content = setInnerWidget(
		object_ptr<SessionsContent>(this, _controller),
		st::sessionsScroll);
	content->resize(w, st::noContactsHeight);
	content->setupContent();

	setDimensions(w, st::sessionsHeight);
}

namespace Settings {

Sessions::Sessions(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Sessions::title() {
	return tr::lng_settings_sessions_title();
}

void Sessions::setupContent(not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	AddSkip(container, st::settingsPrivacySkip);
	const auto content = container->add(
		object_ptr<SessionsContent>(container, controller));
	content->setupContent();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
