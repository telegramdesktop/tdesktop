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
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/layers/generic_box.h"
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

void SessionInfoBox(
		not_null<Ui::GenericBox*> box,
		const EntryData &data,
		Fn<void(uint64)> terminate) {
	box->setTitle(rpl::single(data.name));
	box->setWidth(st::boxWidth);

	const auto skips = style::margins(0, 0, 0, st::settingsSectionSkip);
	const auto date = base::unixtime::parse(data.activeTime);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(langDateTimeFull(date)),
			st::boxDividerLabel),
		st::boxRowPadding + skips);

	const auto add = [&](rpl::producer<QString> label, QString value) {
		if (value.isEmpty()) {
			return;
		}
		Settings::AddSubsectionTitle(
			box->verticalLayout(),
			std::move(label));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(value),
				st::boxDividerLabel),
			st::boxRowPadding + skips);
	};
	add(tr::lng_sessions_application(), data.info);
	add(tr::lng_sessions_system(), data.system);
	add(tr::lng_sessions_ip(), data.ip);
	add(tr::lng_sessions_location(), data.location);
	if (!data.location.isEmpty()) {
		Settings::AddDividerText(
			box->verticalLayout(),
			tr::lng_sessions_location_about());
	}

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	box->addLeftButton(tr::lng_sessions_terminate(), [=, hash = data.hash] {
		const auto weak = Ui::MakeWeak(box.get());
		terminate(hash);
		if (weak) {
			box->closeBox();
		}
	}, st::attentionBoxButton);
}

[[nodiscard]] QString LocationAndDate(const EntryData &entry) {
	return (entry.location.isEmpty() ? entry.ip : entry.location)
		+ (entry.hash
			? (QString::fromUtf8(" \xE2\x80\xA2 ") + entry.active)
			: QString());
}

[[nodiscard]] Type TypeFromEntry(const EntryData &entry) {
	using List = std::vector<int>;
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

[[nodiscard]] QImage GenerateUserpic(Type type) {
	const auto size = st::sessionUserpicSize;
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
	struct Entry {
		Entry() = default;
		explicit Entry(const EntryData &entry);

		EntryData data;

		bool incomplete = false;
		Type type = Type::Other;
		TimeId activeTime = 0;
		Ui::Text::String name, info, location;
		QImage userpic;
	};
	struct Full {
		Entry current;
		std::vector<Entry> incomplete;
		std::vector<Entry> list;
	};
	class Inner;
	class List;

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
	QPointer<Ui::ConfirmBox> _terminateBox;

	base::Timer _shortPollTimer;

};

class SessionsContent::List : public Ui::RpWidget {
public:
	List(QWidget *parent);

	void showData(gsl::span<const Entry> items);
	rpl::producer<int> itemsCount() const;
	rpl::producer<uint64> terminateRequests() const;
	[[nodiscard]] rpl::producer<EntryData> showRequests() const;

	void terminating(uint64 hash, bool terminating);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	struct RowWidth {
		int available = 0;
		int info = 0;
	};

	void subscribeToCustomDeviceModel();
	void computeRowWidth();

	RowWidth _rowWidth;
	std::vector<Entry> _items;
	std::map<uint64, std::unique_ptr<Ui::IconButton>> _terminateButtons;
	rpl::event_stream<uint64> _terminateRequests;
	rpl::event_stream<int> _itemsCount;
	rpl::event_stream<EntryData> _showRequests;

	int _pressed = -1;

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
	[[nodiscard]] rpl::producer<> renameCurrentRequests() const;

	void terminatingOne(uint64 hash, bool terminating);

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	QPointer<List> _current;
	QPointer<Ui::SettingsButton> _terminateAll;
	QPointer<List> _incomplete;
	QPointer<List> _list;
	rpl::variable<int> _ttlDays;

};

SessionsContent::Entry::Entry(const EntryData &entry)
: data(entry)
, incomplete(entry.incomplete)
, type(TypeFromEntry(entry))
, activeTime(entry.activeTime)
, name(st::sessionNameStyle, entry.name)
, info(st::sessionInfoStyle, entry.info)
, location(st::sessionInfoStyle, LocationAndDate(entry))
, userpic(GenerateUserpic(type)) {
};

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
		auto entry = Entry(auth);
		if (!entry.data.hash) {
			_data.current = std::move(entry);
		} else if (entry.incomplete) {
			_data.incomplete.push_back(std::move(entry));
		} else {
			_data.list.push_back(std::move(entry));
		}
	}

	_loading = false;

	ranges::sort(_data.list, std::greater<>(), &Entry::activeTime);
	ranges::sort(_data.incomplete, std::greater<>(), &Entry::activeTime);

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
	_terminateBox = Ui::show(
		Box<Ui::ConfirmBox>(
			message,
			tr::lng_settings_reset_button(tr::now),
			st::attentionBoxButton,
			callback),
		Ui::LayerOption::KeepOther);
}

void SessionsContent::terminateOne(uint64 hash) {
	const auto weak = Ui::MakeWeak(this);
	auto callback = [=] {
		auto done = crl::guard(weak, [=](const MTPBool &result) {
			if (mtpIsFalse(result)) {
				return;
			}
			_inner->terminatingOne(hash, false);
			const auto removeByHash = [&](std::vector<Entry> &list) {
				list.erase(
					ranges::remove(
						list,
						hash,
						[](const Entry &entry) { return entry.data.hash; }),
					end(list));
			};
			removeByHash(_data.incomplete);
			removeByHash(_data.list);
			_inner->showData(_data);
		});
		auto fail = crl::guard(weak, [=](const MTP::Error &error) {
			_inner->terminatingOne(hash, false);
		});
		_authorizations->requestTerminate(
			std::move(done),
			std::move(fail),
			hash);
		_inner->terminatingOne(hash, true);
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
		Ui::show(Box(RenameBox), Ui::LayerOption::KeepOther);
	});

	_current = content->add(
		object_ptr<List>(content),
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
			st::sessionsTerminateAll,
			&st::sessionsTerminateAllIcon,
			st::sessionsTerminateAllIconLeft,
			&st::attentionButtonFg));
	AddSkip(terminateInner);
	AddDividerText(terminateInner, tr::lng_sessions_terminate_all_about());

	const auto incompleteWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto incompleteInner = incompleteWrap->entity();
	AddSkip(incompleteInner, st::sessionSubtitleSkip);
	AddSubsectionTitle(incompleteInner, tr::lng_sessions_incomplete());
	_incomplete = incompleteInner->add(object_ptr<List>(incompleteInner));
	AddSkip(incompleteInner);
	AddDividerText(incompleteInner, tr::lng_sessions_incomplete_about());

	const auto listWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
	const auto listInner = listWrap->entity();
	AddSkip(listInner, st::sessionSubtitleSkip);
	AddSubsectionTitle(listInner, tr::lng_sessions_other_header());
	_list = listInner->add(object_ptr<List>(listInner));
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
		_ttlDays.value(
	) | rpl::map(SelfDestructionBox::DaysLabel),
		st::settingsButton
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
		_incomplete->showRequests(),
		_list->showRequests());
}

void SessionsContent::Inner::terminatingOne(uint64 hash, bool terminating) {
	_incomplete->terminating(hash, terminating);
	_list->terminating(hash, terminating);
}

SessionsContent::List::List(QWidget *parent) : RpWidget(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SessionsContent::List::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);

	computeRowWidth();
}

void SessionsContent::List::subscribeToCustomDeviceModel() {
	Core::App().settings().deviceModelChanges(
	) | rpl::start_with_next([=](const QString &model) {
		for (auto &entry : _items) {
			if (!entry.data.hash) {
				entry.name.setText(st::sessionNameStyle, model);
			}
		}
		update();
	}, lifetime());
}

void SessionsContent::List::showData(gsl::span<const Entry> items) {
	computeRowWidth();

	auto buttons = base::take(_terminateButtons);
	_items.clear();
	_items.insert(begin(_items), items.begin(), items.end());
	for (const auto &entry : _items) {
		const auto hash = entry.data.hash;
		if (!hash) {
			continue;
		}
		const auto button = [&] {
			const auto i = buttons.find(hash);
			return _terminateButtons.emplace(
				hash,
				(i != end(buttons)
					? std::move(i->second)
					: std::make_unique<Ui::IconButton>(
						this,
						st::sessionTerminate))).first->second.get();
		}();
		button->setClickedCallback([=] {
			_terminateRequests.fire_copy(hash);
		});
		button->show();
		const auto number = _terminateButtons.size() - 1;
		widthValue(
		) | rpl::start_with_next([=] {
			button->moveToRight(
				st::sessionTerminateSkip,
				(number * st::sessionHeight + st::sessionTerminateTop));
		}, lifetime());
	}
	resizeToWidth(width());
	_itemsCount.fire(_items.size());
}

rpl::producer<EntryData> SessionsContent::List::showRequests() const {
	return _showRequests.events();
}

rpl::producer<int> SessionsContent::List::itemsCount() const {
	return _itemsCount.events_starting_with(_items.size());
}

rpl::producer<uint64> SessionsContent::List::terminateRequests() const {
	return _terminateRequests.events();
}

void SessionsContent::List::terminating(uint64 hash, bool terminating) {
	const auto i = _terminateButtons.find(hash);
	if (i != _terminateButtons.cend()) {
		if (terminating) {
			i->second->clearState();
			i->second->hide();
		} else {
			i->second->show();
		}
	}
}

int SessionsContent::List::resizeGetHeight(int newWidth) {
	return _items.size() * st::sessionHeight;
}

void SessionsContent::List::computeRowWidth() {
	const auto available = width()
		- st::sessionPadding.left()
		- st::sessionTerminateSkip;
	_rowWidth = {
		.available = available,
		.info = available - st::sessionTerminate.width,
	};
}

void SessionsContent::List::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.fillRect(r, st::boxBg);
	p.setFont(st::linkFont);
	const auto count = int(_items.size());
	const auto from = floorclamp(r.y(), st::sessionHeight, 0, count);
	const auto till = ceilclamp(
		r.y() + r.height(),
		st::sessionHeight,
		0,
		count);

	const auto available = _rowWidth.available;
	const auto x = st::sessionPadding.left();
	const auto y = st::sessionPadding.top();
	const auto w = width();
	p.translate(0, from * st::sessionHeight);
	for (auto i = from; i != till; ++i) {
		const auto &entry = _items[i];

		p.drawImage(st::sessionUserpicPosition, entry.userpic);

		const auto nameW = _rowWidth.info;
		const auto infoW = entry.data.hash ? _rowWidth.info : available;

		p.setPen(st::sessionNameFg);
		entry.name.drawLeftElided(p, x, y, nameW, w);

		p.setPen(st::boxTextFg);
		entry.info.drawLeftElided(p, x, y + st::sessionInfoTop, infoW, w);

		p.setPen(st::sessionInfoFg);
		entry.location.drawLeftElided(
			p,
			x,
			y + st::sessionLocationTop,
			available,
			w);

		p.translate(0, st::sessionHeight);
	}
}

void SessionsContent::List::mousePressEvent(QMouseEvent *e) {
	const auto index = e->pos().y() / st::sessionHeight;
	_pressed = (index >= 0 && index < _items.size()) ? index : -1;
}

void SessionsContent::List::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = e->pos().y() / st::sessionHeight;
	const auto released = (index >= 0 && index < _items.size()) ? index : -1;
	if (released == _pressed && released >= 0) {
		_showRequests.fire_copy(_items[released].data);
	}
	_pressed = -1;
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

void Sessions::setupContent(not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	AddSkip(container, st::settingsPrivacySkip);
	const auto content = container->add(
		object_ptr<SessionsContent>(container, controller));
	content->setupContent();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
