/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_touch.h"

#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/scroll_area.h"
#include "ui/abstract_button.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPointingDevice>
#include <QtGui/QScreen>
#include <QtGui/QTouchEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qwindowsysteminterface_p.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include <array>
#include <cmath>

#include "styles/style_widgets.h"

namespace Test {
namespace {

// The identities the QR-swipe task's driver used
// (2026/10/20/swipe-the-qr-box-emoji-and-gift-layer-by-touch), far from
// the small ids platform plugins hand out: QInputDevicePrivate::isRegistered
// compares systemId() only.
constexpr auto kScreenSystemId = qint64(0x7e5700000001);
constexpr auto kPadSystemId = qint64(0x7e5700000002);
constexpr auto kMaxContacts = 10;

struct HarnessDevices {
	const QPointingDevice *screen = nullptr;
	const QPointingDevice *pad = nullptr;
};

// One Begin climb of a frame: the contact ids its TouchBegin carries, the
// last widget QApplication::notify offered it to and that row's label.
struct OpenClimb {
	std::vector<int> ids;
	QPointer<QWidget> last;
	QString label;
	int number = 0;
};

// Copies touch, mouse and widget Enter events into |rows| for its own
// lifetime and never vetoes one. Installed last, it runs before every
// other application filter, Core::Application's included.
class Observer final : public QObject {
public:
	Observer(not_null<std::vector<TouchEventRow>*> rows, int climbsBefore);
	~Observer();

	[[nodiscard]] int climbs() const;
	[[nodiscard]] const OpenClimb *climb(int number) const;

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	[[nodiscard]] int assignClimb(
		not_null<QWidget*> widget,
		const TouchEventRow &row);

	const not_null<std::vector<TouchEventRow>*> _rows;
	std::vector<OpenClimb> _open;
	int _climbs = 0;

};

[[nodiscard]] HarnessDevices &Devices() {
	static auto result = HarnessDevices();
	return result;
}

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] const QPointingDevice *RegisterDevice(
		const QString &name,
		qint64 systemId,
		QInputDevice::DeviceType type,
		QInputDevice::Capabilities capabilities,
		not_null<QObject*> parent) {
	const auto result = new QPointingDevice(
		name,
		systemId,
		type,
		QPointingDevice::PointerType::Finger,
		capabilities,
		kMaxContacts,
		0,
		QString(),
		QPointingDeviceUniqueId(),
		parent);
	QWindowSystemInterface::registerInputDevice(result);
	return result;
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

void RegisterDevices() {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	auto &devices = Devices();
	const auto application = QCoreApplication::instance();
	if (devices.screen || !application) {
		return;
	}
	QPointingDevice::primaryPointingDevice();

	using Capability = QInputDevice::Capability;
	devices.screen = RegisterDevice(
		u"harness touchscreen"_q,
		kScreenSystemId,
		QInputDevice::DeviceType::TouchScreen,
		Capability::Position | Capability::Area,
		application);
	devices.pad = RegisterDevice(
		u"harness touchpad"_q,
		kPadSystemId,
		QInputDevice::DeviceType::TouchPad,
		(Capability::Position
			| Capability::NormalizedPosition
			| Capability::MouseEmulation),
		application);
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] bool SubmitTouch(
		not_null<QWindow*> window,
		not_null<const QPointingDevice*> device,
		const std::vector<TouchEventPoint> &sent) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	auto points = QList<QEventPoint>();
	points.reserve(int(sent.size()));
	for (const auto &point : sent) {
		points.push_back(QEventPoint(
			point.id,
			point.state,
			point.globalPosition,
			point.globalPosition));
	}
	using Delivery = QWindowSystemInterface::SynchronousDelivery;
	return QWindowSystemInterface::handleTouchEvent<Delivery>(
		window.get(),
		device.get(),
		QWindowSystemInterfacePrivate::toNativeTouchPoints(
			points,
			window.get()));
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] bool SubmitCancel(
		not_null<QWindow*> window,
		not_null<const QPointingDevice*> device) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	using Delivery = QWindowSystemInterface::SynchronousDelivery;
	return QWindowSystemInterface::handleTouchCancelEvent<Delivery>(
		window.get(),
		device.get());
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] bool IsTouchType(QEvent::Type type) {
	return (type == QEvent::TouchBegin)
		|| (type == QEvent::TouchUpdate)
		|| (type == QEvent::TouchEnd)
		|| (type == QEvent::TouchCancel);
}

[[nodiscard]] bool IsMouseType(QEvent::Type type) {
	return (type == QEvent::MouseButtonPress)
		|| (type == QEvent::MouseButtonRelease)
		|| (type == QEvent::MouseButtonDblClick)
		|| (type == QEvent::MouseMove);
}

[[nodiscard]] bool IsHoverRow(const TouchEventRow &row) {
	return !row.window
		&& (IsMouseType(row.type) || (row.type == QEvent::Enter));
}

// Every live widget that received a mouse event or an Enter, the terminal
// cleanup's rows included, once, in the order of its first row.
void CollectHoverReceivers(
		std::vector<QPointer<QWidget>> &receivers,
		const TouchFrame &frame) {
	for (const auto rows : { &frame.rows, &frame.cleanupRows }) {
		for (const auto &row : *rows) {
			const auto widget = IsHoverRow(row)
				? qobject_cast<QWidget*>(row.receiver.data())
				: nullptr;
			if (!widget) {
				continue;
			}
			const auto known = ranges::any_of(receivers, [&](const auto &p) {
				return (p.data() == widget);
			});
			if (!known) {
				receivers.push_back(widget);
			}
		}
	}
}

[[nodiscard]] bool IsFinite(QPointF point) {
	return qIsFinite(point.x()) && qIsFinite(point.y());
}

[[nodiscard]] QString ObjectLabel(not_null<QObject*> object) {
	const auto name = object->objectName();
	const auto described = object->isWidgetType()
		? WidgetDescription(static_cast<QWidget*>(object.get()))
		: QString::fromLatin1(object->metaObject()->className());
	return name.isEmpty() ? described : (name + u" "_q + described);
}

[[nodiscard]] TouchEventRow CopyRow(
		not_null<QObject*> receiver,
		not_null<QEvent*> event) {
	auto row = TouchEventRow();
	row.receiver = receiver.get();
	row.label = ObjectLabel(receiver);
	row.type = event->type();
	row.window = receiver->isWindowType();
	row.spontaneous = event->spontaneous();
	if (event->isInputEvent()) {
		const auto input = static_cast<QInputEvent*>(event.get());
		const auto device = input->device();
		row.device = device;
		row.deviceName = device ? device->name() : QString();
	}
	if (event->isSinglePointEvent()) {
		const auto single = static_cast<QSinglePointEvent*>(event.get());
		row.position = single->position();
		row.globalPosition = single->globalPosition();
	}
	if (const auto touch = dynamic_cast<QTouchEvent*>(event.get())) {
		for (const auto &point : touch->points()) {
			row.points.push_back({
				.id = point.id(),
				.state = point.state(),
				.position = point.position(),
				.globalPosition = point.globalPosition(),
			});
		}
	} else if (const auto mouse = dynamic_cast<QMouseEvent*>(event.get())) {
		row.source = mouse->source();
		row.button = mouse->button();
		row.buttons = mouse->buttons();
	}
	return row;
}

// The initial climb is the sequence's climb 1. Its last offered widget
// owns the sequence only when Qt marked it - no parent walk - and only
// when no later climb of the frame was offered that widget as well: Qt
// rewrote the mark for that climb's Begin, so it answers for either.
[[nodiscard]] std::optional<TouchOwner> ReadInitialOwner(
		const Observer &observer,
		const std::vector<TouchEventRow> &rows,
		int frame) {
	const auto initial = observer.climb(1);
	if (!initial) {
		return std::nullopt;
	}
	auto result = TouchOwner();
	result.frame = frame;
	const auto last = initial->last.data();
	const auto shared = last
		&& ranges::any_of(rows, [&](const TouchEventRow &row) {
			return (row.climb > 1) && (row.receiver.data() == last);
		});
	if (shared) {
		result.status = TouchOwnerStatus::Ambiguous;
		result.label = initial->label;
	} else if (last
		&& last->testAttribute(Qt::WA_WState_AcceptedTouchBeginEvent)) {
		result.status = TouchOwnerStatus::Accepted;
		result.widget = last;
		result.label = initial->label;
	} else {
		result.status = TouchOwnerStatus::Refused;
	}
	return result;
}

void Refuse(TouchFrame &frame, const QString &name, const QString &reason) {
	frame.refusal = name;
	frame.reason = reason;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString CoordinateText(QPointF point) {
	return u"%1,%2"_q.arg(
		QString::number(point.x(), 'f', 3),
		QString::number(point.y(), 'f', 3));
}

[[nodiscard]] QString TypeName(QEvent::Type type) {
	switch (type) {
	case QEvent::TouchBegin: return u"TouchBegin"_q;
	case QEvent::TouchUpdate: return u"TouchUpdate"_q;
	case QEvent::TouchEnd: return u"TouchEnd"_q;
	case QEvent::TouchCancel: return u"TouchCancel"_q;
	case QEvent::MouseButtonPress: return u"MouseButtonPress"_q;
	case QEvent::MouseButtonRelease: return u"MouseButtonRelease"_q;
	case QEvent::MouseButtonDblClick: return u"MouseButtonDblClick"_q;
	case QEvent::MouseMove: return u"MouseMove"_q;
	case QEvent::Enter: return u"Enter"_q;
	case QEvent::Leave: return u"Leave"_q;
	default: return QString::number(int(type));
	}
}

[[nodiscard]] QString StateName(QEventPoint::State state) {
	switch (state) {
	case QEventPoint::State::Unknown: return u"Unknown"_q;
	case QEventPoint::State::Stationary: return u"Stationary"_q;
	case QEventPoint::State::Pressed: return u"Pressed"_q;
	case QEventPoint::State::Updated: return u"Updated"_q;
	case QEventPoint::State::Released: return u"Released"_q;
	}
	return QString::number(int(state));
}

[[nodiscard]] QString SourceName(Qt::MouseEventSource source) {
	switch (source) {
	case Qt::MouseEventNotSynthesized: return u"none"_q;
	case Qt::MouseEventSynthesizedBySystem: return u"system"_q;
	case Qt::MouseEventSynthesizedByQt: return u"qt"_q;
	case Qt::MouseEventSynthesizedByApplication: return u"application"_q;
	}
	return QString::number(int(source));
}

[[nodiscard]] QString PointText(const TouchEventPoint &point) {
	return u"%1:%2 local=%3 global=%4"_q.arg(
		QString::number(point.id),
		StateName(point.state),
		CoordinateText(point.position),
		CoordinateText(point.globalPosition));
}

[[nodiscard]] QString RowText(const TouchEventRow &row) {
	auto result = u"%1 %2 window=%3 spontaneous=%4 device=%5"_q.arg(
		TypeName(row.type),
		row.label,
		FlagText(row.window),
		FlagText(row.spontaneous),
		row.deviceName.isEmpty() ? u"none"_q : row.deviceName);
	if (IsTouchType(row.type)) {
		auto points = QStringList();
		for (const auto &point : row.points) {
			points.push_back(PointText(point));
		}
		return result + u" climb=%1 points=[%2]"_q.arg(
			QString::number(row.climb),
			points.join(u", "_q));
	}
	result += u" local=%1 global=%2"_q.arg(
		CoordinateText(row.position),
		CoordinateText(row.globalPosition));
	if (IsMouseType(row.type)) {
		result += u" source=%1 button=%2 buttons=%3"_q.arg(
			SourceName(row.source),
			QString::number(int(row.button)),
			QString::number(row.buttons.toInt()));
	}
	return result;
}

Observer::Observer(
	not_null<std::vector<TouchEventRow>*> rows,
	int climbsBefore)
: _rows(rows)
, _climbs(climbsBefore) {
	QCoreApplication::instance()->installEventFilter(this);
}

Observer::~Observer() {
	if (const auto instance = QCoreApplication::instance()) {
		instance->removeEventFilter(this);
	}
}

int Observer::climbs() const {
	return _climbs;
}

const OpenClimb *Observer::climb(int number) const {
	const auto i = ranges::find(_open, number, &OpenClimb::number);
	return (i != end(_open)) ? &*i : nullptr;
}

bool Observer::eventFilter(QObject *receiver, QEvent *event) {
	const auto type = event->type();
	const auto widget = receiver->isWidgetType();
	if (!IsTouchType(type)
		&& !IsMouseType(type)
		&& !(widget && (type == QEvent::Enter))) {
		return false;
	}
	auto row = CopyRow(receiver, event);
	if (widget && (type == QEvent::TouchBegin)) {
		row.climb = assignClimb(static_cast<QWidget*>(receiver), row);
	}
	_rows->push_back(std::move(row));
	return false;
}

// A Begin offered to an ancestor of a climb's last widget, with the same
// contact ids, continues that climb; anything else starts a new one.
int Observer::assignClimb(
		not_null<QWidget*> widget,
		const TouchEventRow &row) {
	auto ids = std::vector<int>();
	ids.reserve(row.points.size());
	for (const auto &point : row.points) {
		ids.push_back(point.id);
	}
	for (auto &open : _open) {
		const auto last = open.last.data();
		if (last && (open.ids == ids) && widget->isAncestorOf(last)) {
			open.last = widget.get();
			open.label = row.label;
			return open.number;
		}
	}
	_open.push_back({
		.ids = std::move(ids),
		.last = widget.get(),
		.label = row.label,
		.number = ++_climbs,
	});
	return _climbs;
}

} // namespace

const QPointingDevice *HarnessTouchDevice(TouchDeviceKind kind) {
	RegisterDevices();
	const auto &devices = Devices();
	return (kind == TouchDeviceKind::Pad) ? devices.pad : devices.screen;
}

bool IsHarnessTouchDevice(const QInputDevice *device) {
	const auto &devices = Devices();
	return device && ((device == devices.screen) || (device == devices.pad));
}

TouchSequence::TouchSequence(
	not_null<QWidget*> target,
	TouchDeviceKind kind)
: _target(target.get())
, _device(HarnessTouchDevice(kind)) {
}

TouchFrame TouchSequence::send(const std::vector<TouchContact> &changes) {
	auto frame = TouchFrame();
	if (!ready(frame)) {
		return record(std::move(frame));
	}
	const auto reason = invalid(changes);
	if (!reason.isEmpty()) {
		Refuse(frame, u"invalid-frame"_q, reason);
		return record(std::move(frame));
	} else if (!bindWindow()) {
		Refuse(
			frame,
			u"no-window"_q,
			u"the target's top-level widget has no QWindow handle; "
			u"nothing was sent"_q);
		return record(std::move(frame));
	}
	fillSent(frame, changes);
	submit(frame, [&](not_null<QWindow*> window) {
		return SubmitTouch(window, _device, frame.sent);
	});
	if (frame.submitted) {
		applySent(frame.sent);
		_finished = _down.empty();
	} else {
		Refuse(
			frame,
			u"not-submitted"_q,
			u"QWindowSystemInterface::handleTouchEvent did not take the "
			u"frame; the sequence's contacts are unchanged"_q);
	}
	conclude(frame);
	return record(std::move(frame));
}

TouchFrame TouchSequence::cancel() {
	auto frame = TouchFrame();
	frame.cancel = true;
	if (!ready(frame)) {
		return record(std::move(frame));
	} else if (_down.empty()) {
		Refuse(
			frame,
			u"nothing-down"_q,
			u"no contact of this sequence is down, so a cancel would reach "
			u"only points other sequences left; nothing was sent"_q);
		return record(std::move(frame));
	}
	submit(frame, [&](not_null<QWindow*> window) {
		return SubmitCancel(window, _device);
	});
	if (frame.submitted) {
		_down.clear();
		_finished = true;
	} else {
		Refuse(
			frame,
			u"not-submitted"_q,
			u"QWindowSystemInterface::handleTouchCancelEvent did not take "
			u"the cancel; the sequence's contacts are unchanged"_q);
	}
	conclude(frame);
	return record(std::move(frame));
}

const std::vector<TouchFrame> &TouchSequence::frames() const {
	return _frames;
}

const TouchOwner &TouchSequence::owner() const {
	return _owner;
}

bool TouchSequence::finished() const {
	return _finished;
}

bool TouchSequence::stopped() const {
	return _stopped;
}

bool TouchSequence::ready(TouchFrame &frame) {
	readAlive(frame);
	if (!_device) {
		Refuse(
			frame,
			u"unsupported"_q,
			u"no harness touch device is registered: injection needs the "
			u"private Qt API; nothing was sent"_q);
		return false;
	} else if (_stopped) {
		Refuse(
			frame,
			u"stopped"_q,
			u"the sequence's target or window was destroyed earlier; "
			u"nothing was sent"_q);
		return false;
	} else if (frame.targetAlive && (frame.windowAlive || !_windowBound)) {
		if (!_finished) {
			return true;
		}
		Refuse(
			frame,
			u"finished"_q,
			u"the gesture already ended; a new gesture needs a new "
			u"sequence; nothing was sent"_q);
		return false;
	} else if (frame.targetAlive) {
		Refuse(
			frame,
			u"window-gone"_q,
			u"the window the sequence's frames went to was destroyed; "
			u"the frame was not sent"_q);
	} else {
		Refuse(
			frame,
			u"target-gone"_q,
			u"the sequence's target was destroyed; the frame was not "
			u"sent"_q);
	}
	conclude(frame);
	return false;
}

QString TouchSequence::invalid(
		const std::vector<TouchContact> &changes) const {
	if (changes.empty() && _down.empty()) {
		return u"an empty frame while no contact is down"_q;
	}
	auto listed = std::vector<int>();
	auto pressed = 0;
	for (const auto &change : changes) {
		const auto id = change.id;
		const auto number = QString::number(id);
		const auto down = ranges::contains(_down, id, &DownContact::id);
		const auto positioned = change.position.has_value();
		if (id < 0) {
			return u"contact id %1 is negative"_q.arg(number);
		} else if (ranges::contains(listed, id)) {
			return u"contact %1 is listed twice"_q.arg(number);
		} else if (positioned && !IsFinite(*change.position)) {
			return u"contact %1 has a non-finite position"_q.arg(number);
		}
		listed.push_back(id);
		switch (change.state) {
		case QEventPoint::State::Pressed:
			if (down) {
				return u"contact %1 is pressed while already down"_q.arg(
					number);
			} else if (!positioned) {
				return u"pressed contact %1 has no position"_q.arg(number);
			}
			++pressed;
			break;
		case QEventPoint::State::Updated:
			if (!down) {
				return u"updated contact %1 is not down"_q.arg(number);
			} else if (!positioned) {
				return u"updated contact %1 has no position"_q.arg(number);
			}
			break;
		case QEventPoint::State::Stationary:
			if (!down) {
				return u"stationary contact %1 is not down"_q.arg(number);
			} else if (positioned) {
				return u"stationary contact %1 carries a position; it keeps "
					u"its stored one"_q.arg(number);
			}
			break;
		case QEventPoint::State::Released:
			if (!down) {
				return u"released contact %1 is not down"_q.arg(number);
			}
			break;
		default:
			return u"contact %1 has state %2, which no frame carries"_q.arg(
				number,
				StateName(change.state));
		}
	}
	const auto limit = _device->maximumPoints();
	if (int(_down.size()) + pressed > limit) {
		return u"%1 contacts would be down, more than the device's %2"_q.arg(
			QString::number(int(_down.size()) + pressed),
			QString::number(limit));
	}
	return QString();
}

QWindow *TouchSequence::bindWindow() {
	if (!_windowBound) {
		const auto handle = _target->window()->windowHandle();
		if (!handle) {
			return nullptr;
		}
		_window = handle;
		_windowBound = true;
	}
	return _window.data();
}

void TouchSequence::fillSent(
		TouchFrame &frame,
		const std::vector<TouchContact> &changes) const {
	const auto target = _target.data();
	for (const auto &change : changes) {
		const auto stored = ranges::find(_down, change.id, &DownContact::id);
		Expects(change.position.has_value() || (stored != end(_down)));
		const auto global = change.position
			? target->mapToGlobal(*change.position)
			: stored->global;
		frame.sent.push_back({
			.id = change.id,
			.state = change.state,
			.position = (change.position
				? *change.position
				: target->mapFromGlobal(global)),
			.globalPosition = global,
		});
	}
	for (const auto &contact : _down) {
		if (ranges::contains(changes, contact.id, &TouchContact::id)) {
			continue;
		}
		frame.sent.push_back({
			.id = contact.id,
			.state = QEventPoint::State::Stationary,
			.position = target->mapFromGlobal(contact.global),
			.globalPosition = contact.global,
		});
	}
}

void TouchSequence::submit(
		TouchFrame &frame,
		Fn<bool(not_null<QWindow*>)> dispatch) {
	const auto index = int(_frames.size());
	auto observer = Observer(&frame.rows, _climbs);
	Settle([&] {
		const auto window = _window.data();
		if (!_target || !window) {
			return;
		}
		frame.submitted = dispatch(window);
		if (_owner.frame < 0) {
			const auto initial = ReadInitialOwner(observer, frame.rows, index);
			if (initial) {
				_owner = *initial;
			}
		}
	});
	_climbs = observer.climbs();
	trackPress(frame.rows);
}

void TouchSequence::applySent(const std::vector<TouchEventPoint> &sent) {
	for (const auto &point : sent) {
		if (point.state == QEventPoint::State::Pressed) {
			_down.push_back({
				.id = point.id,
				.global = point.globalPosition,
			});
			continue;
		}
		const auto i = ranges::find(_down, point.id, &DownContact::id);
		if (i == end(_down)) {
			continue;
		} else if (point.state == QEventPoint::State::Released) {
			_down.erase(i);
		} else {
			i->global = point.globalPosition;
		}
	}
}

// A Qt-synthesized press from this device that reached a window holds
// QGuiApplication::mouseButtons() until a release from it reaches one.
void TouchSequence::trackPress(const std::vector<TouchEventRow> &rows) {
	for (const auto &row : rows) {
		if (!row.window || (row.device.data() != _device)) {
			continue;
		} else if ((row.type == QEvent::MouseButtonPress)
			&& (row.source == Qt::MouseEventSynthesizedByQt)) {
			_pressHeld = true;
		} else if (row.type == QEvent::MouseButtonRelease) {
			_pressHeld = false;
		}
	}
}

// The cleanup a call owes - a stop's cancel, the ending's Leave, then the
// cancel a press still held after that Leave owes, whether the gesture
// just ended or the Leave destroyed the target - and only then the
// survival readings, so they see every drain. Such a late cancel makes
// the Leave run once more.
void TouchSequence::conclude(TouchFrame &frame) {
	latchLoss(frame);
	if (_stopped) {
		releasePress(frame);
	}
	while ((_finished || _stopped) && !_hoverReleased) {
		releaseHover(frame);
		latchLoss(frame);
		releasePress(frame);
	}
}

void TouchSequence::readAlive(TouchFrame &frame) const {
	frame.targetAlive = !_target.isNull();
	frame.windowAlive = !_window.isNull();
}

void TouchSequence::latchLoss(TouchFrame &frame) {
	readAlive(frame);
	if (!frame.targetAlive || (_windowBound && !frame.windowAlive)) {
		_stopped = true;
	}
}

// The terminal cleanup, once the sequence stopped or ended: one point-free
// cancel through the platform layer, for which
// QGuiApplicationPrivate::processTouchEvent releases the press it
// synthesized in the window that got it. Tried once; never with the
// window gone, when Qt has no window to release in. Its release can end
// a press a widget kept its hover through, so the Leave is owed again.
void TouchSequence::releasePress(TouchFrame &frame) {
	if (!_pressHeld || !_window) {
		return;
	}
	_pressHeld = false;
	_hoverReleased = false;
	auto observer = Observer(&frame.cleanupRows, _climbs);
	Settle([&] {
		if (const auto window = _window.data()) {
			frame.cleanupCancel = SubmitCancel(window, _device);
		}
	});
	if (frame.cleanupCancel) {
		_down.clear();
	}
	latchLoss(frame);
}

// Each receiver is checked alive right before its own Leave, because the
// drain after an earlier Leave may destroy it.
void TouchSequence::releaseHover(TouchFrame &frame) {
	_hoverReleased = true;
	auto receivers = std::vector<QPointer<QWidget>>();
	for (const auto &previous : _frames) {
		CollectHoverReceivers(receivers, previous);
	}
	CollectHoverReceivers(receivers, frame);
	for (const auto &receiver : receivers) {
		const auto widget = receiver.data();
		if (!widget) {
			continue;
		}
		frame.left.push_back(ObjectLabel(widget));
		auto leave = QEvent(QEvent::Leave);
		Settle([&] {
			if (const auto strong = receiver.data()) {
				QApplication::sendEvent(strong, &leave);
			}
		});
	}
}

TouchFrame TouchSequence::record(TouchFrame &&frame) {
	_frames.push_back(std::move(frame));
	return _frames.back();
}

QString TouchFrameDetails(const TouchFrame &frame) {
	auto sent = QStringList();
	for (const auto &point : frame.sent) {
		sent.push_back(PointText(point));
	}
	auto rows = QStringList();
	for (const auto &row : frame.rows) {
		rows.push_back(RowText(row));
	}
	auto line = u"touch frame: kind=%1 submitted=%2 targetAlive=%3 "
		u"windowAlive=%4 sent=[%5] rows=[%6] left=[%7]"_q.arg(
			frame.cancel ? u"cancel"_q : u"touch"_q,
			FlagText(frame.submitted),
			FlagText(frame.targetAlive),
			FlagText(frame.windowAlive),
			sent.join(u", "_q),
			rows.join(u"; "_q),
			frame.left.join(u"; "_q));
	if (frame.cleanupCancel || !frame.cleanupRows.empty()) {
		auto cleanup = QStringList();
		for (const auto &row : frame.cleanupRows) {
			cleanup.push_back(RowText(row));
		}
		line += u" terminalCleanup: cancel=%1 rows=[%2]"_q.arg(
			FlagText(frame.cleanupCancel),
			cleanup.join(u"; "_q));
	}
	return frame.refusal.isEmpty()
		? line
		: (line + u" refusal=%1 - %2"_q.arg(frame.refusal, frame.reason));
}

namespace {

using PointState = QEventPoint::State;

// Contact ids start at 1: QGuiApplicationPrivate::processMouseEvent keeps
// the mouse it synthesizes from a touch device as point 0 of that device's
// QPointingDevicePrivate::activePoints, so a contact 0 would share it.
constexpr auto kFirstId = 1;
constexpr auto kSecondId = 2;
constexpr auto kTolerance = 0.01;
constexpr auto kSwipeUpdates = 4;

const auto kWindowWhere = u"QWindow"_q;
const auto kAcceptName = u"touch.accept"_q;
const auto kPlainName = u"touch.accept.plain"_q;
const auto kRefuseName = u"touch.accept.refuse"_q;
const auto kUnhandledName = u"touch.unhandled"_q;
const auto kDiesName = u"touch.dies"_q;
const auto kPressDiesName = u"touch.press.dies"_q;
const auto kDroppedName = u"touch.dropped"_q;
const auto kLeaveDiesName = u"touch.leave.dies"_q;
const auto kLeaveWindowName = u"touch.leave.window"_q;
const auto kHeldName = u"touch.held"_q;
const auto kHeldButtonName = u"touch.held.button"_q;
const auto kHeldAcceptName = u"touch.held.accept"_q;
const auto kPairName = u"touch.pair"_q;
const auto kPairLeftName = u"touch.pair.left"_q;
const auto kPairRightName = u"touch.pair.right"_q;
const auto kButtonName = u"touch.scroll.button"_q;

// Accept takes every touch event. Refuse sets Qt::WA_AcceptTouchEvents and
// leaves every touch event to QWidget::event, which ignores it. Plain has
// no Qt::WA_AcceptTouchEvents, so QApplication::notify never offers it a
// Begin. DestroyOnBegin accepts, and its first Begin posts its own
// deletion with Ui::PostponeCall. DestroyOnPress, DestroyOnLeave and
// DestroyWindowOnLeave refuse touch as Refuse does and, once armed, post
// with Ui::PostponeCall the deletion of themselves on their next mouse
// press (a double click included), of themselves on their next Leave, or
// of their top-level window on their next Leave. Scripted answers each
// TouchBegin from its script, in order, refusing once the script runs
// out, and takes every other touch event. Every mode takes the mouse
// events it is given, so none climbs on to the window.
enum class ProbeMode {
	Accept,
	Refuse,
	Plain,
	DestroyOnBegin,
	DestroyOnPress,
	DestroyOnLeave,
	DestroyWindowOnLeave,
	Scripted,
};

// One event as a probe's own event() or the fixture QWindow's object
// filter received it: the self-test's oracle, copied there, independently
// of the helper's application filter. |where| is the probe's object name
// or kWindowWhere; |accepted| is a Scripted probe's own answer to a
// TouchBegin.
struct JournalEvent {
	QString where;
	std::vector<TouchEventPoint> points;
	QPointer<const QInputDevice> device;
	QString deviceName;
	QPointF position;
	QEvent::Type type = QEvent::None;
	Qt::MouseEventSource source = Qt::MouseEventNotSynthesized;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	bool spontaneous = false;
	bool accepted = false;
};

// Shared by every probe, the window filter and the scroll button: the
// events in delivery order, the probes destroyed so far and the button's
// clicks. It owns no widget, so nothing holding it keeps one alive.
struct Journal {
	std::vector<JournalEvent> events;
	QStringList destroyed;
	int clicks = 0;
};

class TouchProbe final : public Ui::RpWidget {
public:
	TouchProbe(
		QWidget *parent,
		ProbeMode mode,
		std::shared_ptr<Journal> journal);
	~TouchProbe();

	void arm();
	[[nodiscard]] bool armed() const;
	void scriptBegins(std::vector<bool> answers);

protected:
	bool eventHook(QEvent *e) override;

private:
	void postDestruction(QEvent::Type type);
	[[nodiscard]] bool answerBegin();

	const ProbeMode _mode = ProbeMode::Accept;
	const std::shared_ptr<Journal> _journal;
	std::vector<bool> _answers;
	int _answered = 0;
	bool _armed = false;
	bool _destructionPosted = false;

};

// The input samples, all derived from st::defaultActiveButton.height:
// probe-local points carrying .25 / .75 fractions (inputs, never UI
// dimensions, which expose any integer rounding on the path), the pair's
// two points in the pair's own coordinates, one on each of its children,
// the tap point in the scroll viewport's coordinates, located on the
// button, and the swipe step from the live
// QApplication::startDragDistance().
struct Samples {
	std::array<QPointF, 4> direct;
	std::array<QPointF, 2> plain;
	std::array<QPointF, 2> refuse;
	std::array<QPointF, 2> unhandled;
	std::array<QPointF, 2> dies;
	std::array<QPointF, 2> first;
	std::array<QPointF, 2> second;
	std::array<QPointF, 2> pair;
	QPointF tap;
	int swipeStep = 0;
};

// One send() or cancel(): the helper's frame and what the journal received
// during that same synchronous call.
struct Delivery {
	TouchFrame frame;
	std::vector<JournalEvent> journal;
};

// One gesture as its stage's run left it; the stage's then judges it.
struct Leg {
	QString name;
	std::vector<Delivery> deliveries;
	TouchOwner owner;
	const QPointingDevice *device = nullptr;
	bool finished = false;
	bool stopped = false;
};

struct Comparison {
	QString text;
	double maxError = 0.;
	bool ok = false;
};

struct MouseCensus {
	QStringList windowRows;
	int widgetMouse = 0;
	int windowMouse = 0;
	int enter = 0;
	int journalProbeMouse = 0;
	int journalWindowMouse = 0;
	int journalEnter = 0;
	int left = 0;
};

struct RouteReading {
	QString text;
	bool ok = false;
};

struct DeviceCensus {
	QString text;
	int harness = 0;
	int screenEntries = 0;
	int padEntries = 0;
	bool roles = false;
	bool primaryIsHarness = false;
};

// The refusing-child leg right after its Begin and after its End.
struct TrapReading {
	TouchOwner atBegin;
	QString wrongWalk;
	QPointer<QWidget> wrongWidget;
	bool childMarked = false;
	bool parentMarked = false;
	bool childMarkedAfterEnd = false;
	bool parentMarkedAfterEnd = false;
};

struct ControlReading {
	Qt::MouseButtons buttons = Qt::NoButton;
	bool marked = false;
	bool underMouse = false;
};

struct CancelReading {
	DeviceCensus census;
	const QPointingDevice *before = nullptr;
	const QPointingDevice *after = nullptr;
};

// The destructive Begin as its delivery returned: the probe already gone,
// and the frame and owner still printable.
struct LifetimeReading {
	TouchOwner owner;
	QString details;
	bool alive = false;
	bool destroyed = false;
	bool stopped = false;
};

// How a stop leg's refusing probe costs its sequence the target or the
// window: armed to post its own deletion from Qt's synthesized press,
// deleted by the stage between the press and the release, or armed
// before the release to post its own or its window's deletion from the
// helper's Leave. HeldPress binds it to the held group's coordinate probe
// instead, armed before the release, whose deletion the button's Leave
// posts while it is down; HeldFinish runs that gesture unarmed, so nothing
// dies and the gesture only finishes with Qt's press still held.
enum class StopBy {
	ArmedPress,
	DeletedBetween,
	ArmedLeave,
	HeldPress,
	HeldFinish,
};

// A stop leg as its run left it: Qt's mouse buttons before the gesture,
// between the press and the release (after the deletion, for
// DeletedBetween) and after the leg's calls, beside the probe's and its
// window's survival read through the leg's own QPointers, and for the
// held legs the button's isDown() and isOver() before and after the
// release.
struct StopReading {
	Qt::MouseButtons before = Qt::NoButton;
	Qt::MouseButtons held = Qt::NoButton;
	Qt::MouseButtons after = Qt::NoButton;
	bool targetAlive = false;
	bool windowAlive = false;
	bool destroyed = false;
	bool stopped = false;
	bool finished = false;
	bool downHeld = false;
	bool overHeld = false;
	bool downAfter = false;
	bool overAfter = false;
};

// One stop leg: the probe and its sample point the fixture set up (and
// the held legs' second contact's point), the probe's name and QWindow its
// run took first, and what the run left.
struct StopLeg {
	QPointer<TouchProbe> probe;
	QPointer<QWindow> handle;
	QString name;
	QPointF point;
	QPointF second;
	Leg leg;
	StopReading reading;
};

// The literal outcome of a stop leg: what stopped it, the refusal and
// index of the call that did, the widget that refused the Begin and took
// Qt's press when that is not the probe, the releases Qt synthesized to
// it, whether the terminal cleanup ran and whether Qt held the press
// before that call, the window's survival and the gesture's end on
// return, and whether the probe survives, the gesture only finishing.
struct StopExpected {
	QString what;
	QString refusal;
	QString receiver;
	int call = 0;
	int releases = 0;
	bool cleanup = false;
	bool held = false;
	bool windowAlive = false;
	bool finished = false;
	bool survives = false;
};

// One two-group leg: the pair's script and its own mark right after the
// frame that carried both Begin groups.
struct PairReading {
	std::vector<bool> script;
	bool markedAfterBegin = false;
};

struct ScrollReading {
	int topBefore = 0;
	int topAfter = 0;
	int topMax = 0;
	int clicksBefore = 0;
	int clicksAfterBegin = 0;
	int clicksAfter = 0;
	int step = 0;
	bool contentOwns = false;
	bool over = false;
	bool underMouse = false;
	bool released = false;
};

struct State {
	base::unique_qptr<Ui::RpWidget> window;
	base::unique_qptr<Ui::RpWidget> secondWindow;
	base::unique_qptr<Ui::ScrollArea> scroll;
	QPointer<QWindow> handle;
	QPointer<QWindow> secondHandle;
	QPointer<TouchProbe> accept;
	QPointer<TouchProbe> plain;
	QPointer<TouchProbe> refuse;
	QPointer<TouchProbe> unhandled;
	QPointer<TouchProbe> dies;
	QPointer<TouchProbe> pair;
	QPointer<TouchProbe> pairLeft;
	QPointer<TouchProbe> pairRight;
	QPointer<Ui::AbstractButton> heldButton;
	QPointer<TouchProbe> heldAccept;
	QPointer<QWidget> content;
	QPointer<Ui::AbstractButton> button;
	std::shared_ptr<Journal> journal = std::make_shared<Journal>();
	Samples samples;
	QString gate;
	const QPointingDevice *screen = nullptr;
	const QPointingDevice *pad = nullptr;
	Leg direct;
	Leg climb;
	Leg trap;
	Leg control;
	Leg residue;
	Leg cancelled;
	Leg recovered;
	Leg multi;
	Leg touchpad;
	Leg destroyed;
	Leg follow;
	Leg afterStops;
	Leg pairRefuseFirst;
	Leg pairAcceptFirst;
	Leg tap;
	Leg swipe;
	StopLeg pressStop;
	StopLeg dropStop;
	StopLeg leaveTarget;
	StopLeg leaveSecond;
	StopLeg heldStop;
	StopLeg heldFinish;
	TrapReading trapReading;
	ControlReading controlReading;
	CancelReading cancelReading;
	LifetimeReading lifetime;
	PairReading pairRefuseFirstReading;
	PairReading pairAcceptFirstReading;
	ScrollReading tapReading;
	ScrollReading swipeReading;
	bool built = false;
};

[[nodiscard]] JournalEvent CopyJournalEvent(
		const QString &where,
		not_null<QEvent*> event) {
	auto result = JournalEvent();
	result.where = where;
	result.type = event->type();
	result.spontaneous = event->spontaneous();
	if (const auto input = dynamic_cast<QInputEvent*>(event.get())) {
		const auto device = input->device();
		result.device = device;
		result.deviceName = device ? device->name() : QString();
	}
	const auto touch = dynamic_cast<QTouchEvent*>(event.get());
	const auto single = dynamic_cast<QSinglePointEvent*>(event.get());
	if (touch) {
		for (const auto &point : touch->points()) {
			result.points.push_back({
				.id = point.id(),
				.state = point.state(),
				.position = point.position(),
				.globalPosition = point.globalPosition(),
			});
		}
	} else if (single) {
		result.position = single->position();
	}
	if (const auto mouse = dynamic_cast<QMouseEvent*>(event.get())) {
		result.source = mouse->source();
		result.button = mouse->button();
		result.buttons = mouse->buttons();
	}
	return result;
}

[[nodiscard]] bool RefusesTouch(ProbeMode mode) {
	constexpr auto kRefusing = std::array{
		ProbeMode::Refuse,
		ProbeMode::Plain,
		ProbeMode::DestroyOnPress,
		ProbeMode::DestroyOnLeave,
		ProbeMode::DestroyWindowOnLeave,
	};
	return ranges::contains(kRefusing, mode);
}

TouchProbe::TouchProbe(
	QWidget *parent,
	ProbeMode mode,
	std::shared_ptr<Journal> journal)
: RpWidget(parent)
, _mode(mode)
, _journal(std::move(journal)) {
	setAttribute(Qt::WA_AcceptTouchEvents, mode != ProbeMode::Plain);
}

TouchProbe::~TouchProbe() {
	_journal->destroyed.push_back(objectName());
}

void TouchProbe::arm() {
	_armed = true;
}

bool TouchProbe::armed() const {
	return _armed;
}

void TouchProbe::scriptBegins(std::vector<bool> answers) {
	_answers = std::move(answers);
	_answered = 0;
}

bool TouchProbe::eventHook(QEvent *e) {
	const auto type = e->type();
	const auto touch = IsTouchType(type);
	const auto mouse = IsMouseType(type);
	if (touch || mouse || (type == QEvent::Enter)) {
		_journal->events.push_back(CopyJournalEvent(objectName(), e));
	}
	if (_armed) {
		postDestruction(type);
	}
	if (mouse) {
		e->accept();
		return true;
	} else if ((_mode == ProbeMode::Scripted)
		&& (type == QEvent::TouchBegin)) {
		const auto accepted = answerBegin();
		_journal->events.back().accepted = accepted;
		e->setAccepted(accepted);
		return true;
	} else if (!touch || RefusesTouch(_mode)) {
		return RpWidget::eventHook(e);
	} else if ((_mode == ProbeMode::DestroyOnBegin)
		&& (type == QEvent::TouchBegin)
		&& !_destructionPosted) {
		_destructionPosted = true;
		Ui::PostponeCall(parentWidget(), [weak = QPointer<QWidget>(this)] {
			delete weak.data();
		});
	}
	e->accept();
	return true;
}

void TouchProbe::postDestruction(QEvent::Type type) {
	const auto press = (_mode == ProbeMode::DestroyOnPress)
		&& ((type == QEvent::MouseButtonPress)
			|| (type == QEvent::MouseButtonDblClick));
	const auto leave = ((_mode == ProbeMode::DestroyOnLeave)
			|| (_mode == ProbeMode::DestroyWindowOnLeave))
		&& (type == QEvent::Leave);
	if ((!press && !leave) || _destructionPosted) {
		return;
	}
	_destructionPosted = true;
	const auto victim = (_mode == ProbeMode::DestroyWindowOnLeave)
		? window()
		: static_cast<QWidget*>(this);
	Ui::PostponeCall(victim, [weak = QPointer<QWidget>(victim)] {
		delete weak.data();
	});
}

bool TouchProbe::answerBegin() {
	const auto index = _answered++;
	return (index < int(_answers.size())) && _answers[index];
}

[[nodiscard]] QString DeviceName(const QInputDevice *device) {
	return device ? device->name() : u"none"_q;
}

[[nodiscard]] QString DeviceTypeName(QInputDevice::DeviceType type) {
	switch (type) {
	case QInputDevice::DeviceType::Mouse: return u"Mouse"_q;
	case QInputDevice::DeviceType::TouchScreen: return u"TouchScreen"_q;
	case QInputDevice::DeviceType::TouchPad: return u"TouchPad"_q;
	default: return QString::number(int(type));
	}
}

[[nodiscard]] QString DeviceText(const QPointingDevice *device) {
	if (!device) {
		return u"none"_q;
	}
	const auto finger = (device->pointerType()
		== QPointingDevice::PointerType::Finger);
	return u"{name=%1 type=%2 pointer=%3 caps=0x%4 maxPoints=%5 "
		u"systemId=0x%6 mouseEmulation=%7}"_q.arg(
			device->name(),
			DeviceTypeName(device->type()),
			(finger
				? u"Finger"_q
				: QString::number(int(device->pointerType()))),
			QString::number(device->capabilities().toInt(), 16),
			QString::number(device->maximumPoints()),
			QString::number(device->systemId(), 16),
			FlagText(device->capabilities().testFlag(
				QInputDevice::Capability::MouseEmulation)));
}

// Every registered device is enumerated, so a second registration of an
// id, a replaced device or a harness device turned primary shows here.
[[nodiscard]] DeviceCensus ReadDevices(
		const QPointingDevice *screen,
		const QPointingDevice *pad) {
	using Capability = QInputDevice::Capability;
	auto result = DeviceCensus();
	const auto all = QInputDevice::devices();
	for (const auto device : all) {
		if (IsHarnessTouchDevice(device)) {
			++result.harness;
		}
		if (device->systemId() == kScreenSystemId) {
			++result.screenEntries;
		} else if (device->systemId() == kPadSystemId) {
			++result.padEntries;
		}
	}
	const auto finger = QPointingDevice::PointerType::Finger;
	const auto screenCaps = QInputDevice::Capabilities(Capability::Position)
		| Capability::Area;
	const auto padCaps = QInputDevice::Capabilities(Capability::Position)
		| Capability::NormalizedPosition
		| Capability::MouseEmulation;
	result.roles = screen
		&& pad
		&& (screen != pad)
		&& (screen->type() == QInputDevice::DeviceType::TouchScreen)
		&& (screen->pointerType() == finger)
		&& (screen->maximumPoints() == kMaxContacts)
		&& (screen->capabilities() == screenCaps)
		&& (screen->systemId() == kScreenSystemId)
		&& (pad->type() == QInputDevice::DeviceType::TouchPad)
		&& (pad->pointerType() == finger)
		&& (pad->maximumPoints() == kMaxContacts)
		&& (pad->capabilities() == padCaps)
		&& (pad->systemId() == kPadSystemId);
	const auto primary = QPointingDevice::primaryPointingDevice();
	result.primaryIsHarness = IsHarnessTouchDevice(primary);
	result.text = u"devices=%1 harness=%2 screenIdEntries=%3 "
		u"padIdEntries=%4 screen=%5 pad=%6 primary=%7"_q.arg(
			QString::number(all.size()),
			QString::number(result.harness),
			QString::number(result.screenEntries),
			QString::number(result.padEntries),
			DeviceText(screen),
			DeviceText(pad),
			DeviceText(primary));
	return result;
}

[[nodiscard]] QString PointsText(const std::vector<TouchEventPoint> &points) {
	auto list = QStringList();
	for (const auto &point : points) {
		list.push_back(u"%1:%2@%3"_q.arg(
			QString::number(point.id),
			StateName(point.state),
			CoordinateText(point.position)));
	}
	return list.join(u", "_q);
}

// |shape| prints only what a literal expectation carries.
[[nodiscard]] QString JournalText(const JournalEvent &event, bool shape) {
	const auto head = u"%1 %2"_q.arg(event.where, TypeName(event.type));
	if (shape) {
		return head + u" {%1}"_q.arg(PointsText(event.points));
	}
	auto result = head + u" spontaneous=%1 device=%2"_q.arg(
		FlagText(event.spontaneous),
		event.deviceName.isEmpty() ? u"none"_q : event.deviceName);
	if (IsTouchType(event.type)) {
		return result + u" {%1}"_q.arg(PointsText(event.points));
	}
	result += u" at=%1"_q.arg(CoordinateText(event.position));
	if (IsMouseType(event.type)) {
		result += u" source=%1 button=%2 buttons=%3"_q.arg(
			SourceName(event.source),
			QString::number(int(event.button)),
			QString::number(event.buttons.toInt()));
	}
	return result;
}

[[nodiscard]] QString ListText(
		const std::vector<JournalEvent> &events,
		bool shape) {
	auto list = QStringList();
	for (const auto &event : events) {
		list.push_back(JournalText(event, shape));
	}
	return list.join(u"; "_q);
}

[[nodiscard]] QString DprText(const State &state) {
	const auto handle = state.handle.data();
	const auto screen = handle ? handle->screen() : nullptr;
	return u"dpr=%1 screenDpr=%2 screen=%3"_q.arg(
		handle ? QString::number(handle->devicePixelRatio()) : u"none"_q,
		screen ? QString::number(screen->devicePixelRatio()) : u"none"_q,
		screen ? screen->name() : u"none"_q);
}

[[nodiscard]] QString OwnerStatusName(TouchOwnerStatus status) {
	switch (status) {
	case TouchOwnerStatus::None: return u"none"_q;
	case TouchOwnerStatus::Accepted: return u"accepted"_q;
	case TouchOwnerStatus::Refused: return u"refused"_q;
	case TouchOwnerStatus::Ambiguous: return u"ambiguous"_q;
	}
	return QString::number(int(status));
}

[[nodiscard]] QString OwnerText(const TouchOwner &owner) {
	return u"owner{frame=%1 status=%2 alive=%3 label=%4}"_q.arg(
		QString::number(owner.frame),
		OwnerStatusName(owner.status),
		FlagText(!owner.widget.isNull()),
		owner.label.isEmpty() ? u"none"_q : owner.label);
}

[[nodiscard]] bool OwnerIs(const TouchOwner &owner, QWidget *widget) {
	return widget
		&& (owner.frame == 0)
		&& (owner.status == TouchOwnerStatus::Accepted)
		&& (owner.widget.data() == widget)
		&& !owner.label.isEmpty();
}

[[nodiscard]] TouchContact ContactPressed(int id, QPointF position) {
	return { .id = id, .state = PointState::Pressed, .position = position };
}

[[nodiscard]] TouchContact ContactUpdated(int id, QPointF position) {
	return { .id = id, .state = PointState::Updated, .position = position };
}

[[nodiscard]] TouchContact ContactHeld(int id) {
	return { .id = id, .state = PointState::Stationary };
}

[[nodiscard]] TouchContact ContactReleased(int id) {
	return { .id = id, .state = PointState::Released };
}

// A literal expectation, written from the samples - never from a frame the
// helper built.
[[nodiscard]] TouchEventPoint At(int id, PointState state, QPointF position) {
	return { .id = id, .state = state, .position = position };
}

[[nodiscard]] JournalEvent Expect(
		const QString &where,
		QEvent::Type type,
		std::vector<TouchEventPoint> points) {
	auto result = JournalEvent();
	result.where = where;
	result.type = type;
	result.points = std::move(points);
	return result;
}

// One contact pressed at the first of |points|, moved to each of the
// others and released at the last - or, |cancelled|, cancelled with no
// point instead - as |where| receives it.
[[nodiscard]] std::vector<JournalEvent> ExpectStroke(
		const QString &where,
		const std::vector<QPointF> &points,
		bool cancelled = false) {
	auto result = std::vector<JournalEvent>();
	for (const auto &point : points) {
		const auto first = result.empty();
		const auto type = first ? QEvent::TouchBegin : QEvent::TouchUpdate;
		const auto state = first ? PointState::Pressed : PointState::Updated;
		result.push_back(Expect(where, type, { At(kFirstId, state, point) }));
	}
	result.push_back(cancelled
		? Expect(where, QEvent::TouchCancel, {})
		: Expect(where, QEvent::TouchEnd, {
			At(kFirstId, PointState::Released, points.back()),
		}));
	return result;
}

// Field by field: receiver, type, point count, ids and states in order,
// and each position within kTolerance.
[[nodiscard]] Comparison Compare(
		const std::vector<JournalEvent> &received,
		const std::vector<JournalEvent> &expected,
		const QString &receivedName = u"received"_q,
		const QString &expectedName = u"expected"_q) {
	auto result = Comparison();
	auto same = (received.size() == expected.size());
	const auto count = std::min(received.size(), expected.size());
	for (auto i = std::size_t(); i != count; ++i) {
		const auto &got = received[i];
		const auto &want = expected[i];
		if ((got.where != want.where)
			|| (got.type != want.type)
			|| (got.points.size() != want.points.size())) {
			same = false;
			continue;
		}
		for (auto j = std::size_t(); j != want.points.size(); ++j) {
			const auto &a = got.points[j];
			const auto &b = want.points[j];
			const auto delta = a.position - b.position;
			const auto error = std::hypot(delta.x(), delta.y());
			if ((a.id != b.id)
				|| (a.state != b.state)
				|| !(error <= kTolerance)) {
				same = false;
			}
			result.maxError = std::max(result.maxError, error);
		}
	}
	result.ok = same;
	result.text = u"%1=[%2] %3=[%4] maxError=%5"_q.arg(
		expectedName,
		ListText(expected, true),
		receivedName,
		ListText(received, false),
		QString::number(result.maxError, 'f', 4));
	return result;
}

[[nodiscard]] std::vector<JournalEvent> TouchesAt(
		const std::vector<JournalEvent> &events,
		const QString &where) {
	auto result = std::vector<JournalEvent>();
	for (const auto &event : events) {
		if (IsTouchType(event.type) && (event.where == where)) {
			result.push_back(event);
		}
	}
	return result;
}

[[nodiscard]] std::vector<JournalEvent> LegTouchesAt(
		const Leg &leg,
		const QString &where) {
	auto result = std::vector<JournalEvent>();
	for (const auto &delivery : leg.deliveries) {
		const auto part = TouchesAt(delivery.journal, where);
		result.insert(end(result), begin(part), end(part));
	}
	return result;
}

// The helper's own rows for |widget|'s touch events, in the journal's
// shape, so the two records compare field by field.
[[nodiscard]] std::vector<JournalEvent> LegRowsAt(
		const Leg &leg,
		not_null<QWidget*> widget) {
	auto result = std::vector<JournalEvent>();
	for (const auto &delivery : leg.deliveries) {
		for (const auto &row : delivery.frame.rows) {
			if (row.window
				|| !IsTouchType(row.type)
				|| (row.receiver.data() != widget.get())) {
				continue;
			}
			auto event = Expect(widget->objectName(), row.type, row.points);
			event.device = row.device;
			event.deviceName = row.deviceName;
			event.spontaneous = row.spontaneous;
			result.push_back(std::move(event));
		}
	}
	return result;
}

// Every mouse row of every source - the QWindow's included - and every
// Enter, from the helper's rows and from the journal alike.
[[nodiscard]] MouseCensus CountMouse(const Leg &leg) {
	auto result = MouseCensus();
	auto index = 0;
	for (const auto &delivery : leg.deliveries) {
		++index;
		for (const auto &row : delivery.frame.rows) {
			if (IsMouseType(row.type) && row.window) {
				++result.windowMouse;
				result.windowRows.push_back(u"call %1: %2"_q.arg(
					QString::number(index),
					RowText(row)));
			} else if (IsMouseType(row.type)) {
				++result.widgetMouse;
			} else if (row.type == QEvent::Enter) {
				++result.enter;
			}
		}
		for (const auto &event : delivery.journal) {
			if (IsMouseType(event.type) && (event.where == kWindowWhere)) {
				++result.journalWindowMouse;
			} else if (IsMouseType(event.type)) {
				++result.journalProbeMouse;
			} else if (event.type == QEvent::Enter) {
				++result.journalEnter;
			}
		}
		result.left += int(delivery.frame.left.size());
	}
	return result;
}

[[nodiscard]] bool IsClean(const MouseCensus &census) {
	return !census.widgetMouse
		&& !census.windowMouse
		&& !census.enter
		&& !census.journalProbeMouse
		&& !census.journalWindowMouse
		&& !census.journalEnter
		&& !census.left;
}

[[nodiscard]] QString MouseCensusText(const MouseCensus &census) {
	return u"widgetMouseRows=%1 windowMouseRows=%2 enterRows=%3 "
		u"journalProbeMouse=%4 journalWindowMouse=%5 journalEnter=%6 "
		u"left=%7 windowMouse=[%8]"_q.arg(
			QString::number(census.widgetMouse),
			QString::number(census.windowMouse),
			QString::number(census.enter),
			QString::number(census.journalProbeMouse),
			QString::number(census.journalWindowMouse),
			QString::number(census.journalEnter),
			QString::number(census.left),
			census.windowRows.join(u"; "_q));
}

// Each submitted frame reached the QWindow exactly once, spontaneously and
// from the leg's device, in the helper's rows and in the journal; a frame
// that was not submitted reached it not at all.
[[nodiscard]] RouteReading ReadWindowRoute(const Leg &leg) {
	auto ok = !leg.deliveries.empty();
	auto parts = QStringList();
	auto index = 0;
	for (const auto &delivery : leg.deliveries) {
		const auto &frame = delivery.frame;
		auto rows = 0;
		auto journal = 0;
		auto fields = true;
		for (const auto &row : frame.rows) {
			if (row.window && IsTouchType(row.type)) {
				++rows;
				fields = fields
					&& (row.device.data() == leg.device)
					&& row.spontaneous;
			}
		}
		for (const auto &event : delivery.journal) {
			if ((event.where == kWindowWhere) && IsTouchType(event.type)) {
				++journal;
				fields = fields
					&& (event.device.data() == leg.device)
					&& event.spontaneous;
			}
		}
		const auto expected = frame.submitted ? 1 : 0;
		ok = ok && fields && (rows == expected) && (journal == expected);
		parts.push_back(u"%1:submitted=%2 windowRows=%3 windowJournal=%4 "
			u"deviceAndSpontaneous=%5"_q.arg(
				QString::number(++index),
				FlagText(frame.submitted),
				QString::number(rows),
				QString::number(journal),
				FlagText(fields)));
	}
	return {
		.text = u"window route device=%1 [%2]"_q.arg(
			DeviceName(leg.device),
			parts.join(u", "_q)),
		.ok = ok,
	};
}

// The tempting rule the helper does not use: walk up from the first widget
// the Begin reached to the first one Qt marked
// Qt::WA_WState_AcceptedTouchBeginEvent.
[[nodiscard]] QWidget *FirstMarkedWalk(const TouchFrame &frame) {
	for (const auto &row : frame.rows) {
		if (row.window || (row.type != QEvent::TouchBegin)) {
			continue;
		}
		auto widget = qobject_cast<QWidget*>(row.receiver.data());
		for (; widget; widget = widget->parentWidget()) {
			if (widget->testAttribute(
					Qt::WA_WState_AcceptedTouchBeginEvent)) {
				return widget;
			}
		}
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] std::vector<const TouchEventRow*> WidgetBeginRows(
		const TouchFrame &frame) {
	auto result = std::vector<const TouchEventRow*>();
	for (const auto &row : frame.rows) {
		if (!row.window && (row.type == QEvent::TouchBegin)) {
			result.push_back(&row);
		}
	}
	return result;
}

[[nodiscard]] QString BeginRowsText(const TouchFrame &frame) {
	auto list = QStringList();
	for (const auto row : WidgetBeginRows(frame)) {
		list.push_back(RowText(*row));
	}
	return list.join(u"; "_q);
}

[[nodiscard]] QString LeftText(const Leg &leg) {
	auto list = QStringList();
	for (const auto &delivery : leg.deliveries) {
		list += delivery.frame.left;
	}
	return list.join(u"; "_q);
}

[[nodiscard]] bool LeftReached(const Leg &leg, const QString &name) {
	for (const auto &delivery : leg.deliveries) {
		for (const auto &label : delivery.frame.left) {
			if (label.startsWith(name + u' ')) {
				return true;
			}
		}
	}
	return false;
}

// Sends |changes|, or with |cancelled| cancels instead.
void Deliver(
		Leg &leg,
		Journal &journal,
		TouchSequence &sequence,
		const std::vector<TouchContact> &changes,
		bool cancelled = false) {
	const auto from = int(journal.events.size());
	auto frame = cancelled ? sequence.cancel() : sequence.send(changes);
	leg.deliveries.push_back({
		.frame = std::move(frame),
		.journal = { journal.events.begin() + from, journal.events.end() },
	});
}

void Close(Leg &leg, const TouchSequence &sequence) {
	leg.owner = sequence.owner();
	leg.finished = sequence.finished();
	leg.stopped = sequence.stopped();
}

// Names |leg|, a touchscreen leg, and hands out the journal every probe
// and the window filter share.
Journal &StartLeg(State &state, Leg &leg, const QString &name) {
	leg.name = name;
	leg.device = state.screen;
	return *state.journal;
}

// One touchscreen contact on |target|, pressed at the first of |points|,
// moved to each of the others and released at the last - or, |cancelled|,
// cancelled there instead.
void RunStroke(
		State &state,
		Leg &leg,
		const QString &name,
		not_null<QWidget*> target,
		const std::vector<QPointF> &points,
		bool cancelled = false) {
	auto &journal = StartLeg(state, leg, name);
	auto sequence = TouchSequence(target);
	Deliver(leg, journal, sequence, { ContactPressed(kFirstId, points[0]) });
	for (auto i = std::size_t(1); i != points.size(); ++i) {
		const auto updated = ContactUpdated(kFirstId, points[i]);
		Deliver(leg, journal, sequence, { updated });
	}
	if (cancelled) {
		Deliver(leg, journal, sequence, {}, true);
	} else {
		Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	}
	Close(leg, sequence);
}

void NoteFrames(const Leg &leg) {
	auto index = 0;
	for (const auto &delivery : leg.deliveries) {
		Note(u"TOUCH_FRAME: leg=%1 call=%2 %3 journal=[%4]"_q.arg(
			leg.name,
			QString::number(++index),
			TouchFrameDetails(delivery.frame),
			ListText(delivery.journal, false)));
	}
}

[[nodiscard]] QWidget *HitAt(
		not_null<QWidget*> window,
		not_null<QWidget*> origin,
		QPointF local) {
	return window->childAt(window->mapFromGlobal(origin->mapToGlobal(local)));
}

[[nodiscard]] base::unique_qptr<Ui::RpWidget> MakeWindow(
		const QString &name,
		const QString &title,
		QRect geometry) {
	auto result = base::make_unique_q<Ui::RpWidget>(nullptr);
	result->setObjectName(name);
	result->setWindowTitle(title);
	result->setAttribute(Qt::WA_ShowWithoutActivating);
	result->setAttribute(Qt::WA_QuitOnClose, false);
	result->setGeometry(geometry);
	return result;
}

// An ordinary parentless top level shown without activation, as
// MakeOrdinaryWindow in test_window_exposure.cpp: a touch frame needs the
// window's QWindow, and this window keeps Qt's synthesized mouse and Enter
// away from the product's widgets. Geometry is in rows of
// st::defaultActiveButton.height: the accepting probe with a Plain and a
// Refuse child in its upper half and free direct-hit space in its lower
// half, the unhandled and the destruction probes beside it, under them
// the held group - a Ui::AbstractButton refusing touch, which an object
// filter journals, and an accepting probe under a coordinate probe
// transparent for mouse events, which the button's Leave while it is
// down deletes once that probe is armed - the pair with its two refusing
// children and three stop probes in a third column,
// and below the accepting probe a Ui::ScrollArea whose tall content holds
// one Ui::AbstractButton at the viewport's bottom, so a swipe starting on
// it stays inside the viewport. A second small window right of it holds
// only the probe whose Leave destroys that window.
void BuildFixture(State &state) {
	state.screen = HarnessTouchDevice(TouchDeviceKind::Screen);
	state.pad = HarnessTouchDevice(TouchDeviceKind::Pad);
	const auto screen = QGuiApplication::primaryScreen();
	if (!screen) {
		return;
	}
	const auto row = st::defaultActiveButton.height;
	const auto skip = row / 2;
	const auto column = skip + 4 * row + skip;
	const auto third = column + 2 * row + skip;
	const auto pairWidth = row + skip + row;
	auto geometry = QRect(
		0,
		0,
		third + pairWidth + skip,
		skip + 3 * row + skip + 3 * row + skip);
	geometry.moveCenter(screen->availableGeometry().center());
	state.window = MakeWindow(
		u"touch.window"_q,
		u"Harness touch window"_q,
		geometry);
	const auto window = state.window.get();

	const auto journal = state.journal;
	const auto add = [&](
			QWidget *parent,
			ProbeMode mode,
			const QString &name,
			QRect rect) {
		const auto probe = Ui::CreateChild<TouchProbe>(
			parent,
			mode,
			journal);
		probe->setObjectName(name);
		probe->setGeometry(rect);
		probe->show();
		return QPointer<TouchProbe>(probe);
	};
	state.accept = add(
		window,
		ProbeMode::Accept,
		kAcceptName,
		QRect(skip, skip, 4 * row, 3 * row));
	state.plain = add(
		state.accept.data(),
		ProbeMode::Plain,
		kPlainName,
		QRect(skip, skip, row, row));
	state.refuse = add(
		state.accept.data(),
		ProbeMode::Refuse,
		kRefuseName,
		QRect(3 * row - skip, skip, row, row));
	state.unhandled = add(
		window,
		ProbeMode::Refuse,
		kUnhandledName,
		QRect(column, skip, 2 * row, row));
	state.dies = add(
		window,
		ProbeMode::DestroyOnBegin,
		kDiesName,
		QRect(column, skip + row + skip, 2 * row, row));
	state.pair = add(
		window,
		ProbeMode::Scripted,
		kPairName,
		QRect(third, skip, pairWidth, row));
	state.pairLeft = add(
		state.pair.data(),
		ProbeMode::Refuse,
		kPairLeftName,
		QRect(0, 0, row, row));
	state.pairRight = add(
		state.pair.data(),
		ProbeMode::Refuse,
		kPairRightName,
		QRect(row + skip, 0, row, row));
	const auto stop = [&](ProbeMode mode, const QString &name, int index) {
		const auto top = skip + index * (row + skip);
		return add(window, mode, name, QRect(third, top, 2 * row, row));
	};
	using Mode = ProbeMode;
	state.pressStop.probe = stop(Mode::DestroyOnPress, kPressDiesName, 1);
	state.dropStop.probe = stop(Mode::Refuse, kDroppedName, 2);
	state.leaveTarget.probe = stop(Mode::DestroyOnLeave, kLeaveDiesName, 3);

	const auto held = QRect(column, skip + 2 * (row + skip), 2 * row, row);
	const auto heldButton = Ui::CreateChild<Ui::AbstractButton>(window);
	heldButton->setObjectName(kHeldButtonName);
	heldButton->setAttribute(Qt::WA_AcceptTouchEvents);
	heldButton->setGeometry(held.x(), held.y(), row, row);
	heldButton->show();
	state.heldButton = heldButton;
	state.heldAccept = add(
		window,
		Mode::Accept,
		kHeldAcceptName,
		QRect(held.x() + row, held.y(), row, row));
	const auto coordinates = add(window, Mode::Plain, kHeldName, held);
	coordinates->setAttribute(Qt::WA_TransparentForMouseEvents);
	state.heldStop.probe = coordinates;
	base::install_event_filter(heldButton, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (IsTouchType(type)
			|| IsMouseType(type)
			|| (type == QEvent::Enter)
			|| (type == QEvent::Leave)) {
			journal->events.push_back(CopyJournalEvent(kHeldButtonName, e));
		}
		const auto armed = coordinates && coordinates->armed();
		if ((type == QEvent::Leave) && heldButton->isDown() && armed) {
			Ui::PostponeCall(coordinates.data(), [=] {
				delete coordinates.data();
			});
		}
		return base::EventFilterResult::Continue;
	});

	state.scroll = base::make_unique_q<Ui::ScrollArea>(
		window,
		st::defaultScrollArea);
	const auto scroll = state.scroll.get();
	scroll->setObjectName(u"touch.scroll"_q);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setGeometry(skip, skip + 3 * row + skip, 4 * row, 3 * row);
	auto inner = object_ptr<Ui::RpWidget>(scroll);
	inner->setObjectName(u"touch.scroll.content"_q);
	inner->resize(4 * row, 12 * row);
	const auto button = Ui::CreateChild<Ui::AbstractButton>(inner.data());
	button->setObjectName(kButtonName);
	button->setGeometry(0, 2 * row, 4 * row, row);
	button->setClickedCallback([=] {
		++journal->clicks;
	});
	button->show();
	state.content = scroll->setOwnedWidget(std::move(inner)).data();
	state.button = button;
	scroll->show();
	window->show();
	Ui::SendPendingMoveResizeEvents(window);
	state.handle = window->windowHandle();
	if (const auto handle = state.handle.data()) {
		base::install_event_filter(handle, [=](not_null<QEvent*> e) {
			if (IsTouchType(e->type()) || IsMouseType(e->type())) {
				journal->events.push_back(CopyJournalEvent(kWindowWhere, e));
			}
			return base::EventFilterResult::Continue;
		});
	}

	state.secondWindow = MakeWindow(
		u"touch.window.second"_q,
		u"Harness touch window 2"_q,
		QRect(
			geometry.x() + geometry.width() + 2 * row,
			geometry.y(),
			2 * row,
			row));
	const auto other = state.secondWindow.get();
	state.leaveSecond.probe = add(
		other,
		ProbeMode::DestroyWindowOnLeave,
		kLeaveWindowName,
		QRect(0, 0, 2 * row, row));
	other->show();
	Ui::SendPendingMoveResizeEvents(other);
	state.secondHandle = other->windowHandle();

	auto &samples = state.samples;
	const auto quarter = QPointF(0.25, 0.75);
	const auto threeQuarters = QPointF(0.75, 0.25);
	const auto direct = QPointF(row, 2 * row + row / 4) + quarter;
	samples.direct = {
		direct,
		direct + QPointF(row / 2, 0.5),
		direct + QPointF(row, row / 4),
		direct + QPointF(3 * row / 2 + 0.5, row / 2 - 0.5),
	};
	const auto second = QPointF(3 * row, 2 * row + row / 2) + threeQuarters;
	samples.first = { direct, direct + QPointF(row / 4, 0.5) };
	samples.second = { second, second + QPointF(-row / 4, -0.5) };
	const auto plain = QPointF(row / 4, row / 2) + quarter;
	samples.plain = { plain, plain + QPointF(row / 2, -row / 4) };
	const auto refuse = QPointF(row / 2, row / 4) + threeQuarters;
	samples.refuse = { refuse, refuse + QPointF(-row / 4, row / 2) };
	const auto unhandled = QPointF(row / 2, row / 4) + quarter;
	samples.unhandled = { unhandled, unhandled + QPointF(row, row / 4) };
	const auto dies = QPointF(row / 2, row / 2) + threeQuarters;
	samples.dies = { dies, dies + QPointF(row / 2, 0.) };
	samples.pair = {
		QPointF(row / 2, row / 2) + quarter,
		QPointF(row + skip + row / 2, row / 2) + threeQuarters,
	};
	state.pressStop.point = QPointF(row / 2, row / 2) + quarter;
	state.dropStop.point = QPointF(row, row / 2) + threeQuarters;
	state.leaveTarget.point = QPointF(row / 2, row / 4) + threeQuarters;
	state.leaveSecond.point = QPointF(row, row / 2) + quarter;
	state.heldStop.point = QPointF(row / 2, row / 2) + threeQuarters;
	state.heldStop.second = QPointF(row + row / 2, row / 2) + quarter;
	state.heldFinish = state.heldStop;
	samples.tap = button->mapTo(
		scroll->viewport(),
		QPointF(button->width() / 2, button->height() / 2) + quarter);
	samples.swipeStep = std::max(QApplication::startDragDistance(), 1);
}

// Idempotent, so the teardown stage and Runner::onFinish both run it. The
// scroll area goes first: its kinetic timer dies with it.
void ReleaseFixture(State &state) {
	state.scroll = nullptr;
	state.secondWindow = nullptr;
	state.window = nullptr;
}

void CheckFixture(State &state) {
	const auto window = state.window.get();
	const auto handle = state.handle.data();
	const auto shown = window && handle && !window->isHidden();
	Check(
		shown,
		u"fixture gate: touch self-test: the harness window is shown with "
		u"a native QWindow"_q,
		u"window=%1 handle=%2 %3"_q.arg(
			window ? WidgetDescription(window) : u"none"_q,
			FlagText(handle != nullptr),
			DprText(state)));
	const auto other = state.secondWindow.get();
	const auto otherHandle = state.secondHandle.data();
	const auto otherShown = other && otherHandle && !other->isHidden();
	Check(
		otherShown,
		u"fixture gate: touch self-test: the second harness window, the one "
		u"a Leave destroys, is shown with a native QWindow"_q,
		u"window=%1 handle=%2"_q.arg(
			other ? WidgetDescription(other) : u"none"_q,
			FlagText(otherHandle != nullptr)));

	auto hits = QStringList();
	auto hitsOk = shown && otherShown;
	const auto expectHit = [&](
			QWidget *origin,
			QPointF local,
			QWidget *expected) {
		const auto hit = (shown && otherShown && origin)
			? HitAt(origin->window(), origin, local)
			: nullptr;
		hitsOk = hitsOk && expected && (hit == expected);
		hits.push_back(u"%1@%2->%3"_q.arg(
			origin ? origin->objectName() : u"none"_q,
			CoordinateText(local),
			hit ? ObjectLabel(hit) : u"none"_q));
	};
	const auto expectOwn = [&](QWidget *probe, const auto &points) {
		for (const auto &point : points) {
			expectHit(probe, point, probe);
		}
	};
	const auto &samples = state.samples;
	const auto accept = state.accept.data();
	expectOwn(accept, samples.direct);
	expectOwn(accept, samples.first);
	expectOwn(accept, samples.second);
	expectOwn(state.plain.data(), samples.plain);
	expectOwn(state.refuse.data(), samples.refuse);
	expectOwn(state.unhandled.data(), samples.unhandled);
	expectOwn(state.dies.data(), samples.dies);
	const auto pair = state.pair.data();
	expectHit(pair, samples.pair[0], state.pairLeft.data());
	expectHit(pair, samples.pair[1], state.pairRight.data());
	const auto stops = {
		&state.pressStop,
		&state.dropStop,
		&state.leaveTarget,
		&state.leaveSecond,
	};
	for (const auto stop : stops) {
		expectHit(stop->probe.data(), stop->point, stop->probe.data());
	}
	const auto &held = state.heldStop;
	expectHit(held.probe.data(), held.point, state.heldButton.data());
	expectHit(held.probe.data(), held.second, state.heldAccept.data());
	const auto scroll = state.scroll.get();
	const auto viewport = scroll ? scroll->viewport() : nullptr;
	expectHit(viewport, samples.tap, state.button.data());
	Check(
		hitsOk,
		u"fixture gate: touch self-test: every sample point hits the "
		u"widget it is meant for"_q,
		hits.join(u"; "_q));

	const auto max = scroll ? scroll->scrollTopMax() : 0;
	const auto travel = kSwipeUpdates * samples.swipeStep;
	const auto scrollOk = scroll
		&& (scroll->scrollTop() == 0)
		&& (max >= travel);
	Check(
		scrollOk,
		u"fixture gate: touch self-test: the scroll area starts at the top "
		u"with a range longer than the swipe"_q,
		u"scrollTop=%1 scrollTopMax=%2 swipeStep=%3 startDragDistance=%4 "
		u"swipeTravel=%5 tap=%6 viewport=%7"_q.arg(
			scroll ? QString::number(scroll->scrollTop()) : u"none"_q,
			QString::number(max),
			QString::number(samples.swipeStep),
			QString::number(QApplication::startDragDistance()),
			QString::number(travel),
			CoordinateText(samples.tap),
			viewport ? WidgetDescription(viewport) : u"none"_q));

	const auto popup = QApplication::activePopupWidget();
	const auto modal = QGuiApplication::modalWindow();
	const auto synthesize = QCoreApplication::testAttribute(
		Qt::AA_SynthesizeMouseForUnhandledTouchEvents);
	const auto routeOk = !popup && !modal && synthesize;
	Check(
		routeOk,
		u"fixture gate: touch self-test: no popup or modal window is up and "
		u"Qt synthesizes mouse from unhandled touch"_q,
		u"activePopupWidget=%1 modalWindow=%2 "
		u"AA_SynthesizeMouseForUnhandledTouchEvents=%3 "
		u"AA_SynthesizeTouchForUnhandledMouseEvents=%4"_q.arg(
			popup ? WidgetDescription(popup) : u"none"_q,
			modal ? modal->objectName() : u"none"_q,
			FlagText(synthesize),
			FlagText(QCoreApplication::testAttribute(
				Qt::AA_SynthesizeTouchForUnhandledMouseEvents))));

	const auto supported = state.screen && state.pad;
	const auto census = ReadDevices(state.screen, state.pad);
	Check(
		supported
			&& census.roles
			&& (census.harness == 2)
			&& (census.screenEntries == 1)
			&& (census.padEntries == 1),
		u"touch self-test: exactly the two harness devices are registered, "
		u"with their roles and capabilities, and only the pad emulates "
		u"a mouse"_q,
		census.text);
	Check(
		supported && !census.primaryIsHarness,
		u"touch self-test: the primary pointing device is not a harness "
		u"device"_q,
		census.text);

	const auto gates = std::array{
		std::pair(shown, u"window"_q),
		std::pair(otherShown, u"second window"_q),
		std::pair(hitsOk, u"hit targets"_q),
		std::pair(scrollOk, u"scroll range"_q),
		std::pair(routeOk, u"popup, modal or synthesis attribute"_q),
		std::pair(supported, u"no harness touch devices"_q),
	};
	auto failed = QStringList();
	for (const auto &[ok, name] : gates) {
		if (!ok) {
			failed.push_back(name);
		}
	}
	state.gate = failed.join(u", "_q);
	state.built = failed.isEmpty();
}

void CheckRowsAndRoute(
		const Leg &leg,
		const std::vector<QWidget*> &widgets) {
	auto ok = true;
	auto texts = QStringList();
	for (const auto widget : widgets) {
		const auto rows = Compare(
			LegRowsAt(leg, widget),
			LegTouchesAt(leg, widget->objectName()),
			u"rows"_q,
			u"journal"_q);
		ok = ok && rows.ok;
		texts.push_back(rows.text);
	}
	const auto route = ReadWindowRoute(leg);
	texts.push_back(route.text);
	Check(
		ok && route.ok,
		u"touch self-test: %1: the helper's rows equal the probes' own "
		u"record, and every frame reached the QWindow once, spontaneously, "
		u"from %2"_q.arg(leg.name, DeviceName(leg.device)),
		texts.join(u"; "_q));
}

void CheckClean(const Leg &leg) {
	const auto census = CountMouse(leg);
	Check(
		IsClean(census),
		u"touch self-test: %1: the accepted sequence gave no widget and no "
		u"window a mouse event and no widget an Enter, and left no "
		u"hover"_q.arg(leg.name),
		MouseCensusText(census));
}

void RunAccepting(State &state, Leg &leg, const QString &name) {
	const auto &p = state.samples.direct;
	const auto accept = state.accept.data();
	RunStroke(state, leg, name, accept, { p[0], p[1], p[2], p[3] });
}

void CheckAccepting(const State &state, const Leg &leg) {
	NoteFrames(leg);
	const auto accept = state.accept.data();
	const auto &p = state.samples.direct;
	const auto received = LegTouchesAt(leg, kAcceptName);
	const auto mapping = Compare(
		received,
		ExpectStroke(kAcceptName, { p[0], p[1], p[2], p[3] }));
	const auto spontaneous = !received.empty()
		&& received.front().spontaneous;
	Check(
		mapping.ok && spontaneous,
		u"touch self-test: %1: one contact reaches the accepting probe as "
		u"Begin, three Updates and End, each within 0.01 px of the "
		u"requested local position, the directly hit Begin "
		u"spontaneous"_q.arg(leg.name),
		u"%1 beginSpontaneous=%2 %3"_q.arg(
			mapping.text,
			FlagText(spontaneous),
			DprText(state)));
	CheckRowsAndRoute(leg, { accept });
	CheckClean(leg);
	Check(
		OwnerIs(leg.owner, accept) && leg.finished && !leg.stopped,
		u"touch self-test: %1: the owner reading names the directly hit "
		u"probe and the gesture finished"_q.arg(leg.name),
		u"%1 finished=%2 stopped=%3"_q.arg(
			OwnerText(leg.owner),
			FlagText(leg.finished),
			FlagText(leg.stopped)));
}

void RunClimb(State &state) {
	const auto &c = state.samples.plain;
	const auto plain = state.plain.data();
	RunStroke(state, state.climb, u"climb"_q, plain, { c[0], c[1] });
}

void CheckClimb(const State &state) {
	const auto &leg = state.climb;
	NoteFrames(leg);
	const auto accept = state.accept.data();
	const auto plain = state.plain.data();
	const auto offset = QPointF(plain->pos());
	const auto &c = state.samples.plain;
	const auto received = LegTouchesAt(leg, kAcceptName);
	const auto mapping = Compare(
		received,
		ExpectStroke(kAcceptName, { c[0] + offset, c[1] + offset }));
	const auto plainJournal = LegTouchesAt(leg, kPlainName);
	const auto plainRows = LegRowsAt(leg, plain);
	const auto spontaneous = !received.empty()
		&& received.front().spontaneous;
	Check(
		mapping.ok && plainJournal.empty() && plainRows.empty() && !spontaneous,
		u"touch self-test: climb: a sequence started on a child without "
		u"Qt::WA_AcceptTouchEvents reaches the accepting ancestor at "
		u"ancestor-local positions, the child never offered and the "
		u"climbed Begin not spontaneous"_q,
		u"%1 childOffset=%2 childRows=%3 childJournal=%4 "
		u"beginSpontaneous=%5 %6"_q.arg(
			mapping.text,
			CoordinateText(offset),
			QString::number(plainRows.size()),
			QString::number(plainJournal.size()),
			FlagText(spontaneous),
			DprText(state)));
	CheckRowsAndRoute(leg, { accept });
	CheckClean(leg);
	Check(
		OwnerIs(leg.owner, accept) && leg.finished,
		u"touch self-test: climb: the owner reading names the accepting "
		u"ancestor"_q,
		OwnerText(leg.owner));
}

void RunTrap(State &state) {
	auto &leg = state.trap;
	auto &reading = state.trapReading;
	auto &journal = StartLeg(state, leg, u"refusing child"_q);
	const auto &r = state.samples.refuse;
	const auto refuse = state.refuse.data();
	const auto accept = state.accept.data();
	const auto marked = Qt::WA_WState_AcceptedTouchBeginEvent;
	auto sequence = TouchSequence(refuse);
	Deliver(leg, journal, sequence, { ContactPressed(kFirstId, r[0]) });
	reading.atBegin = sequence.owner();
	reading.childMarked = refuse->testAttribute(marked);
	reading.parentMarked = accept->testAttribute(marked);
	reading.wrongWidget = FirstMarkedWalk(leg.deliveries.front().frame);
	reading.wrongWalk = reading.wrongWidget
		? ObjectLabel(reading.wrongWidget.data())
		: u"none"_q;
	Deliver(leg, journal, sequence, { ContactUpdated(kFirstId, r[1]) });
	Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	reading.childMarkedAfterEnd = refuse->testAttribute(marked);
	reading.parentMarkedAfterEnd = accept->testAttribute(marked);
	Close(leg, sequence);
}

void CheckTrap(const State &state) {
	const auto &leg = state.trap;
	const auto &reading = state.trapReading;
	NoteFrames(leg);
	const auto accept = state.accept.data();
	const auto refuse = state.refuse.data();
	const auto &begin = leg.deliveries.front().frame;
	const auto offers = WidgetBeginRows(begin);
	const auto offered = (offers.size() == 2)
		&& (offers[0]->receiver.data() == refuse)
		&& (offers[0]->climb == 1)
		&& offers[0]->spontaneous
		&& (offers[1]->receiver.data() == accept)
		&& (offers[1]->climb == 1)
		&& !offers[1]->spontaneous;
	Check(
		offered,
		u"touch self-test: refusing child: the Begin is offered to the "
		u"refusing child, spontaneously, and then in the same climb to the "
		u"accepting probe, not spontaneously"_q,
		u"beginRows=[%1]"_q.arg(BeginRowsText(begin)));
	Check(
		reading.childMarked && reading.parentMarked,
		u"touch self-test: refusing child: right after the Begin both the "
		u"refusing child and the accepting probe carry "
		u"Qt::WA_WState_AcceptedTouchBeginEvent"_q,
		u"childMarked=%1 parentMarked=%2"_q.arg(
			FlagText(reading.childMarked),
			FlagText(reading.parentMarked)));
	Check(
		OwnerIs(reading.atBegin, accept)
			&& (reading.wrongWidget.data() == refuse),
		u"touch self-test: refusing child: the owner reading names the "
		u"accepting probe, while a walk up from the first receiver to the "
		u"first marked widget names the refusing child"_q,
		u"%1 firstMarkedWalk=%2"_q.arg(
			OwnerText(reading.atBegin),
			reading.wrongWalk));
	const auto offset = QPointF(refuse->pos());
	const auto &r = state.samples.refuse;
	const auto tail = Compare(
		LegTouchesAt(leg, kAcceptName),
		ExpectStroke(kAcceptName, { r[0] + offset, r[1] + offset }));
	const auto child = Compare(LegTouchesAt(leg, kRefuseName), {
		Expect(kRefuseName, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, r[0]),
		}),
	});
	const auto cached = OwnerIs(leg.owner, accept)
		&& (leg.owner.label == reading.atBegin.label)
		&& !reading.parentMarkedAfterEnd;
	Check(
		tail.ok && child.ok && cached && leg.finished,
		u"touch self-test: refusing child: the Update and End reach only "
		u"the accepting probe, and after End cleared its attribute the "
		u"cached owner still names it"_q,
		u"probe: %1; child: %2; %3 parentMarkedAfterEnd=%4 "
		u"childMarkedAfterEnd=%5"_q.arg(
			tail.text,
			child.text,
			OwnerText(leg.owner),
			FlagText(reading.parentMarkedAfterEnd),
			FlagText(reading.childMarkedAfterEnd)));
	CheckRowsAndRoute(leg, { refuse, accept });
	CheckClean(leg);
}

void RunControl(State &state) {
	auto &reading = state.controlReading;
	const auto &u = state.samples.unhandled;
	const auto unhandled = state.unhandled.data();
	const auto name = u"unhandled control"_q;
	RunStroke(state, state.control, name, unhandled, { u[0], u[1] });
	reading.buttons = QGuiApplication::mouseButtons();
	reading.marked = unhandled->testAttribute(
		Qt::WA_WState_AcceptedTouchBeginEvent);
	reading.underMouse = unhandled->underMouse();
}

void CheckControl(const State &state) {
	const auto &leg = state.control;
	const auto &reading = state.controlReading;
	NoteFrames(leg);
	const auto unhandled = state.unhandled.data();
	auto rows = 0;
	auto rowsFromScreen = true;
	auto rowRelease = false;
	for (const auto &delivery : leg.deliveries) {
		for (const auto &row : delivery.frame.rows) {
			if (!IsMouseType(row.type)) {
				continue;
			}
			++rows;
			rowsFromScreen = rowsFromScreen
				&& (row.source == Qt::MouseEventSynthesizedByQt)
				&& (row.device.data() == state.screen);
			if (!row.window
				&& (row.type == QEvent::MouseButtonRelease)
				&& (row.receiver.data() == unhandled)) {
				rowRelease = true;
			}
		}
	}
	auto mice = std::vector<JournalEvent>();
	auto miceFromScreen = true;
	auto miceRelease = false;
	for (const auto &delivery : leg.deliveries) {
		for (const auto &event : delivery.journal) {
			if (!IsMouseType(event.type) || (event.where != kUnhandledName)) {
				continue;
			}
			mice.push_back(event);
			miceFromScreen = miceFromScreen
				&& (event.source == Qt::MouseEventSynthesizedByQt)
				&& (event.device.data() == state.screen);
			if (event.type == QEvent::MouseButtonRelease) {
				miceRelease = true;
			}
		}
	}
	const auto touches = Compare(LegTouchesAt(leg, kUnhandledName), {
		Expect(kUnhandledName, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, state.samples.unhandled[0]),
		}),
	});
	const auto census = CountMouse(leg);
	Check(
		(rows > 0)
			&& rowsFromScreen
			&& rowRelease
			&& !mice.empty()
			&& miceFromScreen
			&& miceRelease
			&& touches.ok,
		u"touch self-test: unhandled control: a screen sequence no widget "
		u"accepts makes Qt synthesize mouse input from the harness "
		u"touchscreen, its release included, after the one refused "
		u"Begin"_q,
		u"mouseRows=%1 rowsQtFromScreen=%2 rowReleaseOnProbe=%3 "
		u"probeMouse=[%4] probeMouseQtFromScreen=%5 probeRelease=%6 "
		u"touches: %7; %8"_q.arg(
			QString::number(rows),
			FlagText(rowsFromScreen),
			FlagText(rowRelease),
			ListText(mice, false),
			FlagText(miceFromScreen),
			FlagText(miceRelease),
			touches.text,
			MouseCensusText(census)));
	Check(
		(leg.owner.frame == 0)
			&& (leg.owner.status == TouchOwnerStatus::Refused)
			&& leg.owner.label.isEmpty()
			&& leg.owner.widget.isNull()
			&& !reading.marked,
		u"touch self-test: unhandled control: the unhandled sequence has "
		u"no owner"_q,
		u"%1 probeMarked=%2"_q.arg(
			OwnerText(leg.owner),
			FlagText(reading.marked)));
	Check(
		(reading.buttons == Qt::NoButton)
			&& !reading.underMouse
			&& LeftReached(leg, kUnhandledName),
		u"touch self-test: unhandled control: Qt's mouse buttons are "
		u"released afterwards and the helper's Leave left the unhandled "
		u"probe without hover"_q,
		u"mouseButtons=%1 underMouse=%2 enterRows=%3 left=[%4]"_q.arg(
			QString::number(reading.buttons.toInt()),
			FlagText(reading.underMouse),
			QString::number(census.enter),
			LeftText(leg)));
	CheckRowsAndRoute(leg, { unhandled });
}

void RunCancel(State &state) {
	auto &reading = state.cancelReading;
	const auto &p = state.samples.direct;
	const auto accept = state.accept.data();
	const auto points = std::vector{ p[0], p[1] };
	reading.before = HarnessTouchDevice(TouchDeviceKind::Screen);
	RunStroke(state, state.cancelled, u"cancel"_q, accept, points, true);
	RunStroke(state, state.recovered, u"recovery"_q, accept, points);
	reading.after = HarnessTouchDevice(TouchDeviceKind::Screen);
	reading.census = ReadDevices(state.screen, state.pad);
}

void CheckCancel(const State &state) {
	const auto &cancelled = state.cancelled;
	const auto &recovered = state.recovered;
	const auto &reading = state.cancelReading;
	NoteFrames(cancelled);
	NoteFrames(recovered);
	const auto accept = state.accept.data();
	const auto &p = state.samples.direct;
	const auto before = Compare(
		LegTouchesAt(cancelled, kAcceptName),
		ExpectStroke(kAcceptName, { p[0], p[1] }, true));
	const auto &cancel = cancelled.deliveries.back();
	auto cancelRows = QStringList();
	auto pointFree = true;
	auto windowRows = 0;
	auto probeRows = 0;
	for (const auto &row : cancel.frame.rows) {
		if (!IsTouchType(row.type)) {
			continue;
		}
		cancelRows.push_back(RowText(row));
		pointFree = pointFree
			&& (row.type == QEvent::TouchCancel)
			&& row.points.empty();
		if (row.window) {
			++windowRows;
		} else if (row.receiver.data() == accept) {
			++probeRows;
		}
	}
	const auto windowJournal = TouchesAt(cancel.journal, kWindowWhere);
	const auto windowPointFree = (windowJournal.size() == 1)
		&& (windowJournal.front().type == QEvent::TouchCancel)
		&& windowJournal.front().points.empty();
	Check(
		before.ok
			&& cancel.frame.cancel
			&& cancel.frame.submitted
			&& cancel.frame.refusal.isEmpty()
			&& pointFree
			&& (windowRows == 1)
			&& (probeRows == 1)
			&& (cancelRows.size() == 2)
			&& windowPointFree
			&& cancelled.finished,
		u"touch self-test: cancel: a point-free TouchCancel reaches the "
		u"QWindow and the accepting probe"_q,
		u"%1 cancelRows=[%2] windowJournal=[%3] finished=%4"_q.arg(
			before.text,
			cancelRows.join(u"; "_q),
			ListText(windowJournal, false),
			FlagText(cancelled.finished)));
	const auto after = Compare(
		LegTouchesAt(recovered, kAcceptName),
		ExpectStroke(kAcceptName, { p[0], p[1] }));
	Check(
		after.ok && OwnerIs(recovered.owner, accept) && recovered.finished,
		u"touch self-test: cancel: a fresh sequence of the same device and "
		u"id on the same probe is delivered as Begin, Update and End"_q,
		u"%1 %2"_q.arg(after.text, OwnerText(recovered.owner)));
	Check(
		(reading.before == state.screen)
			&& (reading.after == state.screen)
			&& (reading.census.harness == 2)
			&& (reading.census.screenEntries == 1)
			&& (reading.census.padEntries == 1),
		u"touch self-test: cancel: the touchscreen device was neither "
		u"replaced nor registered again"_q,
		u"before=%1 after=%2 fixture=%3 %4"_q.arg(
			DeviceText(reading.before),
			DeviceText(reading.after),
			DeviceText(state.screen),
			reading.census.text));
	CheckRowsAndRoute(cancelled, { accept });
	CheckRowsAndRoute(recovered, { accept });
	CheckClean(cancelled);
	CheckClean(recovered);
}

void RunMulti(State &state) {
	auto &leg = state.multi;
	auto &journal = StartLeg(state, leg, u"multi-contact"_q);
	const auto &a = state.samples.first;
	const auto &b = state.samples.second;
	auto sequence = TouchSequence(state.accept.data());
	Deliver(leg, journal, sequence, { ContactPressed(kFirstId, a[0]) });
	Deliver(leg, journal, sequence, { ContactPressed(kSecondId, b[0]) });
	Deliver(leg, journal, sequence, { ContactUpdated(kFirstId, a[1]) });
	Deliver(leg, journal, sequence, {
		ContactUpdated(kSecondId, b[1]),
		ContactHeld(kFirstId),
	});
	Deliver(leg, journal, sequence, {
		ContactHeld(kFirstId),
		ContactHeld(kSecondId),
	});
	Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	Deliver(leg, journal, sequence, { ContactReleased(kSecondId) });
	Close(leg, sequence);
}

void CheckMulti(const State &state) {
	const auto &leg = state.multi;
	NoteFrames(leg);
	const auto accept = state.accept.data();
	const auto &a = state.samples.first;
	const auto &b = state.samples.second;
	const auto one = kFirstId;
	const auto two = kSecondId;
	const auto expected = std::vector<std::vector<JournalEvent>>{
		{ Expect(kAcceptName, QEvent::TouchBegin, {
			At(one, PointState::Pressed, a[0]),
		}) },
		{ Expect(kAcceptName, QEvent::TouchUpdate, {
			At(two, PointState::Pressed, b[0]),
			At(one, PointState::Stationary, a[0]),
		}) },
		{ Expect(kAcceptName, QEvent::TouchUpdate, {
			At(one, PointState::Updated, a[1]),
			At(two, PointState::Stationary, b[0]),
		}) },
		{ Expect(kAcceptName, QEvent::TouchUpdate, {
			At(two, PointState::Updated, b[1]),
			At(one, PointState::Stationary, a[1]),
		}) },
		{},
		{ Expect(kAcceptName, QEvent::TouchUpdate, {
			At(one, PointState::Released, a[1]),
			At(two, PointState::Stationary, b[1]),
		}) },
		{ Expect(kAcceptName, QEvent::TouchEnd, {
			At(two, PointState::Released, b[1]),
		}) },
	};
	const auto windowTypes = std::vector<QEvent::Type>{
		QEvent::TouchBegin,
		QEvent::TouchUpdate,
		QEvent::TouchUpdate,
		QEvent::TouchUpdate,
		QEvent::TouchUpdate,
		QEvent::TouchUpdate,
		QEvent::TouchEnd,
	};
	auto framesOk = (leg.deliveries.size() == expected.size());
	auto windowOk = framesOk;
	auto texts = QStringList();
	auto windowTexts = QStringList();
	for (auto i = std::size_t(); i != leg.deliveries.size(); ++i) {
		const auto &delivery = leg.deliveries[i];
		if (i < expected.size()) {
			const auto frame = Compare(
				TouchesAt(delivery.journal, kAcceptName),
				expected[i]);
			framesOk = framesOk && frame.ok;
			texts.push_back(u"call %1: %2"_q.arg(
				QString::number(i + 1),
				frame.text));
			const auto window = TouchesAt(delivery.journal, kWindowWhere);
			windowOk = windowOk
				&& (window.size() == 1)
				&& (window.front().type == windowTypes[i]);
			windowTexts.push_back(u"call %1: [%2]"_q.arg(
				QString::number(i + 1),
				ListText(window, false)));
		}
	}
	Check(
		framesOk,
		u"touch self-test: multi-contact: the frames P1, P2, M1, M2+S1, "
		u"S1+S2, R1, R2 reach the probe as exactly Begin{P1}, "
		u"Update{P2,S1}, Update{M1,S2}, Update{M2,S1}, nothing, "
		u"Update{R1,S2}, End{R2}, ids, order, states and positions "
		u"kept"_q,
		texts.join(u"; "_q));

	const auto stationary = (leg.deliveries.size() > 4)
		? &leg.deliveries[4]
		: nullptr;
	auto stationaryRows = 0;
	auto stationaryWidgetRows = 0;
	auto stationaryShape = false;
	if (stationary) {
		for (const auto &row : stationary->frame.rows) {
			if (!IsTouchType(row.type)) {
				continue;
			} else if (!row.window) {
				++stationaryWidgetRows;
				continue;
			}
			++stationaryRows;
			stationaryShape = (row.type == QEvent::TouchUpdate)
				&& (row.points.size() == 2)
				&& ranges::all_of(row.points, [](const auto &point) {
					return point.state == PointState::Stationary;
				});
		}
	}
	const auto stationaryJournal = stationary
		? TouchesAt(stationary->journal, kWindowWhere)
		: std::vector<JournalEvent>();
	const auto stationaryProbe = stationary
		? TouchesAt(stationary->journal, kAcceptName)
		: std::vector<JournalEvent>();
	Check(
		windowOk
			&& stationary
			&& stationary->frame.submitted
			&& (stationaryRows == 1)
			&& stationaryShape
			&& !stationaryWidgetRows
			&& (stationaryJournal.size() == 1)
			&& stationaryProbe.empty(),
		u"touch self-test: multi-contact: every frame reached the QWindow, "
		u"and the all-Stationary frame reached it as a TouchUpdate and no "
		u"widget"_q,
		u"window=[%1] stationaryCall: windowRows=%2 shape=%3 "
		u"widgetRows=%4 probeJournal=%5"_q.arg(
			windowTexts.join(u"; "_q),
			QString::number(stationaryRows),
			FlagText(stationaryShape),
			QString::number(stationaryWidgetRows),
			QString::number(stationaryProbe.size())));

	const auto census = CountMouse(leg);
	auto qtMove = false;
	if (stationary) {
		for (const auto &row : stationary->frame.rows) {
			if (row.window && IsMouseType(row.type)) {
				qtMove = (row.type == QEvent::MouseMove)
					&& (row.source == Qt::MouseEventSynthesizedByQt)
					&& (row.device.data() == state.screen)
					&& (row.buttons == Qt::LeftButton)
					&& (row.button == Qt::NoButton);
			}
		}
	}
	Check(
		!census.widgetMouse
			&& !census.enter
			&& !census.journalProbeMouse
			&& !census.journalEnter
			&& !census.left
			&& (census.windowMouse == 1)
			&& (census.journalWindowMouse == 1)
			&& qtMove,
		u"touch self-test: multi-contact: no widget got a mouse event or "
		u"an Enter; the one mouse event is the MouseMove Qt synthesized at "
		u"the QWindow from the unaccepted all-Stationary frame, with the "
		u"left button held, which no widget received"_q,
		u"%1 stationaryQtMove=%2"_q.arg(
			MouseCensusText(census),
			FlagText(qtMove)));
	CheckRowsAndRoute(leg, { accept });
	Check(
		OwnerIs(leg.owner, accept) && leg.finished,
		u"touch self-test: multi-contact: the owner reading names the "
		u"accepting probe"_q,
		OwnerText(leg.owner));
}

void RunPad(State &state) {
	auto &leg = state.touchpad;
	auto &journal = StartLeg(state, leg, u"touchpad"_q);
	leg.device = state.pad;
	const auto &a = state.samples.first;
	const auto &b = state.samples.second;
	auto sequence = TouchSequence(
		state.accept.data(),
		TouchDeviceKind::Pad);
	Deliver(leg, journal, sequence, {
		ContactPressed(kFirstId, a[0]),
		ContactPressed(kSecondId, b[0]),
	});
	Deliver(leg, journal, sequence, {
		ContactUpdated(kFirstId, a[1]),
		ContactUpdated(kSecondId, b[1]),
	});
	Deliver(leg, journal, sequence, {
		ContactReleased(kFirstId),
		ContactReleased(kSecondId),
	});
	Close(leg, sequence);
}

void CheckPad(const State &state) {
	const auto &leg = state.touchpad;
	NoteFrames(leg);
	const auto accept = state.accept.data();
	const auto &a = state.samples.first;
	const auto &b = state.samples.second;
	const auto received = LegTouchesAt(leg, kAcceptName);
	const auto mapping = Compare(received, {
		Expect(kAcceptName, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, a[0]),
			At(kSecondId, PointState::Pressed, b[0]),
		}),
		Expect(kAcceptName, QEvent::TouchUpdate, {
			At(kFirstId, PointState::Updated, a[1]),
			At(kSecondId, PointState::Updated, b[1]),
		}),
		Expect(kAcceptName, QEvent::TouchEnd, {
			At(kFirstId, PointState::Released, a[1]),
			At(kSecondId, PointState::Released, b[1]),
		}),
	});
	const auto fromPad = !received.empty()
		&& ranges::all_of(received, [&](const JournalEvent &event) {
			return (event.device.data() == state.pad);
		});
	Check(
		mapping.ok && fromPad,
		u"touch self-test: touchpad: two contacts reach the accepting probe "
		u"together as Begin, Update and End from the harness touchpad"_q,
		u"%1 allFromPad=%2"_q.arg(mapping.text, FlagText(fromPad)));
	CheckRowsAndRoute(leg, { accept });
	CheckClean(leg);
	Check(
		OwnerIs(leg.owner, accept) && leg.finished,
		u"touch self-test: touchpad: the owner reading names the accepting "
		u"probe"_q,
		OwnerText(leg.owner));
}

void RunLifetime(State &state) {
	auto &reading = state.lifetime;
	{
		auto &leg = state.destroyed;
		auto &journal = StartLeg(state, leg, u"destroyed target"_q);
		const auto &d = state.samples.dies;
		auto sequence = TouchSequence(state.dies.data());
		Deliver(leg, journal, sequence, { ContactPressed(kFirstId, d[0]) });
		reading.alive = !state.dies.isNull();
		reading.destroyed = journal.destroyed.contains(kDiesName);
		reading.stopped = sequence.stopped();
		reading.owner = sequence.owner();
		reading.details = TouchFrameDetails(leg.deliveries.back().frame);
		Deliver(leg, journal, sequence, { ContactUpdated(kFirstId, d[1]) });
		Close(leg, sequence);
	}
	const auto &p = state.samples.direct;
	const auto name = u"after the destroyed target"_q;
	RunStroke(state, state.follow, name, state.accept.data(), { p[0], p[1] });
}

void CheckLifetime(const State &state) {
	const auto &leg = state.destroyed;
	const auto &reading = state.lifetime;
	NoteFrames(leg);
	const auto &first = leg.deliveries.front();
	const auto begin = Compare(TouchesAt(first.journal, kDiesName), {
		Expect(kDiesName, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, state.samples.dies[0]),
		}),
	});
	const auto route = ReadWindowRoute(leg);
	Check(
		!reading.alive
			&& reading.destroyed
			&& reading.stopped
			&& first.frame.submitted
			&& !first.frame.targetAlive
			&& begin.ok
			&& (reading.owner.frame == 0)
			&& !reading.owner.label.isEmpty()
			&& reading.owner.widget.isNull()
			&& route.ok,
		u"touch self-test: destroyed target: a probe that posts its own "
		u"deletion with Ui::PostponeCall from its Begin is gone when the "
		u"delivery returns, the sequence is stopped, and its frame and "
		u"owner still print"_q,
		u"aliveOnReturn=%1 destroyedOnReturn=%2 stopped=%3 %4 %5; %6; "
		u"frame after the death: %7"_q.arg(
			FlagText(reading.alive),
			FlagText(reading.destroyed),
			FlagText(reading.stopped),
			OwnerText(reading.owner),
			begin.text,
			route.text,
			reading.details));
	const auto &second = leg.deliveries.back();
	Check(
		(leg.deliveries.size() == 2)
			&& (second.frame.refusal == u"stopped"_q)
			&& !second.frame.submitted
			&& second.frame.sent.empty()
			&& second.frame.rows.empty()
			&& second.journal.empty()
			&& leg.stopped,
		u"touch self-test: destroyed target: a later send through the "
		u"stopped sequence sends nothing"_q,
		u"%1 journal=%2"_q.arg(
			TouchFrameDetails(second.frame),
			QString::number(second.journal.size())));

	const auto &follow = state.follow;
	NoteFrames(follow);
	const auto accept = state.accept.data();
	const auto &p = state.samples.direct;
	const auto fresh = Compare(
		LegTouchesAt(follow, kAcceptName),
		ExpectStroke(kAcceptName, { p[0], p[1] }));
	Check(
		fresh.ok && OwnerIs(follow.owner, accept) && follow.finished,
		u"touch self-test: destroyed target: a fresh sequence of the same "
		u"device and id on a surviving probe is delivered as Begin, Update "
		u"and End"_q,
		u"%1 %2"_q.arg(fresh.text, OwnerText(follow.owner)));
	CheckRowsAndRoute(follow, { accept });
	CheckClean(leg);
	CheckClean(follow);
}

// The refusing probe gets a Begin, from which Qt synthesizes a press the
// probe takes, then a release, then one call more; the held legs press
// their second contact before releasing both together. The probe's name and
// QWindow are taken while it lives; the readings come from Qt and from the
// leg's own QPointers and the journal.
void RunStop(State &state, StopLeg &stop, const QString &name, StopBy by) {
	auto &journal = *state.journal;
	auto &leg = stop.leg;
	auto &reading = stop.reading;
	const auto held = (by == StopBy::HeldPress) || (by == StopBy::HeldFinish);
	const auto button = state.heldButton.data();
	stop.name = stop.probe->objectName();
	stop.handle = stop.probe->window()->windowHandle();
	StartLeg(state, leg, name);
	reading.before = QGuiApplication::mouseButtons();
	if (by == StopBy::ArmedPress) {
		stop.probe->arm();
	}
	auto sequence = TouchSequence(stop.probe.data());
	const auto pressed = ContactPressed(kFirstId, stop.point);
	Deliver(leg, journal, sequence, { pressed });
	if (by == StopBy::DeletedBetween) {
		delete stop.probe.data();
	} else if (held) {
		const auto second = ContactPressed(kSecondId, stop.second);
		Deliver(leg, journal, sequence, { second });
		reading.downHeld = button->isDown();
		reading.overHeld = button->isOver();
	}
	reading.held = QGuiApplication::mouseButtons();
	if ((by == StopBy::ArmedLeave) || (by == StopBy::HeldPress)) {
		stop.probe->arm();
	}
	Deliver(leg, journal, sequence, held
		? std::vector{ ContactReleased(kFirstId), ContactReleased(kSecondId) }
		: std::vector{ ContactReleased(kFirstId) });
	reading.after = QGuiApplication::mouseButtons();
	reading.targetAlive = !stop.probe.isNull();
	reading.windowAlive = !stop.handle.isNull();
	reading.destroyed = journal.destroyed.contains(stop.name);
	reading.stopped = sequence.stopped();
	reading.finished = sequence.finished();
	reading.downAfter = held && button->isDown();
	reading.overAfter = held && button->isOver();
	Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	Close(leg, sequence);
}

// The two stops and the held finish that would strand Qt's synthesized
// press, then the accepting leg again as the fresh gesture after them.
void RunStops(State &state) {
	RunStop(
		state,
		state.pressStop,
		u"press destroys the target"_q,
		StopBy::ArmedPress);
	RunStop(
		state,
		state.dropStop,
		u"target deleted between calls"_q,
		StopBy::DeletedBetween);
	RunStop(
		state,
		state.heldFinish,
		u"a held press ends with its gesture"_q,
		StopBy::HeldFinish);
	RunAccepting(state, state.afterStops, u"after the stops"_q);
}

// The three stops a cleanup Leave causes once the gesture finished.
void RunLeaveStops(State &state) {
	RunStop(
		state,
		state.leaveTarget,
		u"Leave destroys the target"_q,
		StopBy::ArmedLeave);
	RunStop(
		state,
		state.leaveSecond,
		u"Leave destroys the window"_q,
		StopBy::ArmedLeave);
	RunStop(
		state,
		state.heldStop,
		u"Leave destroys the target of a held press"_q,
		StopBy::HeldPress);
}

// The mouse Qt synthesized from |device| that reached the probe |name|:
// presses - a quick second press reaches it as a double click instead -
// and releases.
struct ProbeMouse {
	int presses = 0;
	int releases = 0;
};

[[nodiscard]] ProbeMouse CountProbeMouse(
		const Leg &leg,
		const QString &name,
		const QPointingDevice *device) {
	auto result = ProbeMouse();
	for (const auto &delivery : leg.deliveries) {
		for (const auto &event : delivery.journal) {
			if ((event.where != name)
				|| (event.source != Qt::MouseEventSynthesizedByQt)
				|| (event.device.data() != device)) {
				continue;
			} else if ((event.type == QEvent::MouseButtonPress)
				|| (event.type == QEvent::MouseButtonDblClick)) {
				++result.presses;
			} else if (event.type == QEvent::MouseButtonRelease) {
				++result.releases;
			}
		}
	}
	return result;
}

// The terminal cleanup as the helper's cleanup rows and, independently, the
// QWindow's own journal saw it: the one release Qt sends for its
// synthesized press - not synthesized, from |device|, the left button
// released and none held - at the QWindow and, handed on, at |receiver|
// alone of all widgets (at none when it is empty), beside one point-free
// TouchCancel at the QWindow while a contact is |down|, and none once all
// were released: Qt cancels only the points it still holds.
[[nodiscard]] RouteReading ReadCleanup(
		const Delivery &delivery,
		const QPointingDevice *device,
		const QString &receiver,
		bool down) {
	const auto cancel = [&](const auto &event) {
		return (event.type == QEvent::TouchCancel)
			&& event.points.empty()
			&& (event.device.data() == device);
	};
	const auto release = [&](const auto &event) {
		return (event.type == QEvent::MouseButtonRelease)
			&& (event.source == Qt::MouseEventNotSynthesized)
			&& (event.device.data() == device)
			&& (event.button == Qt::LeftButton)
			&& (event.buttons == Qt::NoButton);
	};
	const auto &rows = delivery.frame.cleanupRows;
	auto texts = QStringList();
	for (const auto &row : rows) {
		texts.push_back(RowText(row));
	}
	auto journalCancels = 0;
	auto journalReleases = 0;
	for (const auto &event : delivery.journal) {
		if (event.where == kWindowWhere) {
			journalCancels += cancel(event) ? 1 : 0;
			journalReleases += release(event) ? 1 : 0;
		}
	}
	const auto widgetRows = int(
		ranges::count(rows, false, &TouchEventRow::window));
	const auto receiverRows = int(ranges::count_if(rows, [&](const auto &row) {
		return !row.window
			&& release(row)
			&& row.label.startsWith(receiver + u' ');
	}));
	const auto cancelRows = int(ranges::count_if(rows, cancel));
	const auto releaseRows = int(ranges::count_if(rows, release));
	const auto cancels = down ? 1 : 0;
	const auto widgets = receiver.isEmpty() ? 0 : 1;
	return {
		.text = u"cleanupCancel=%1 cleanupRows=[%2] cancelRows=%3 "
			u"releaseRows=%4 widgetRows=%5 journalWindowCancels=%6 "
			u"journalWindowReleases=%7"_q.arg(
				FlagText(delivery.frame.cleanupCancel),
				texts.join(u"; "_q),
				QString::number(cancelRows),
				QString::number(releaseRows),
				QString::number(widgetRows),
				QString::number(journalCancels),
				QString::number(journalReleases)),
		.ok = delivery.frame.cleanupCancel
			&& (cancelRows == cancels)
			&& (releaseRows == 1 + widgets)
			&& (widgetRows == widgets)
			&& (receiverRows == widgets)
			&& (journalCancels == cancels)
			&& (journalReleases == 1),
	};
}

[[nodiscard]] QString ButtonsText(Qt::MouseButtons buttons) {
	return QString::number(buttons.toInt());
}

// The call after a stop, or after a gesture that |survives| only
// finished: refused "stopped" or "finished", with nothing sent, nothing
// delivered and no cleanup again.
void CheckNothingAfterStop(const Leg &leg, bool survives) {
	const auto &last = leg.deliveries.back();
	const auto refusal = survives ? u"finished"_q : u"stopped"_q;
	Check(
		(leg.deliveries.size() > 1)
			&& (last.frame.refusal == refusal)
			&& !last.frame.submitted
			&& !last.frame.cleanupCancel
			&& last.frame.sent.empty()
			&& last.frame.rows.empty()
			&& last.frame.cleanupRows.empty()
			&& last.frame.left.isEmpty()
			&& last.journal.empty()
			&& (leg.stopped != survives),
		u"touch self-test: %1: the next call through the %2 sequence "
		u"is refused \"%2\" and sends nothing"_q.arg(leg.name, refusal),
		u"%1 journal=%2"_q.arg(
			TouchFrameDetails(last.frame),
			QString::number(last.journal.size())));
}

// A stop leg against its literal outcome: the own record of the probe, or
// of the receiver that is not the probe, of the Begin it refused and of
// Qt's synthesized press and releases, Qt's mouse buttons before, between
// the press and the stopping call and on return, the fixture's survival
// readings, then the stopping (for a surviving probe, finishing) call's
// own readings and its terminal cleanup, and the call after it.
void CheckStop(
		const State &state,
		const StopLeg &stop,
		const StopExpected &expected) {
	const auto &leg = stop.leg;
	const auto &reading = stop.reading;
	const auto survives = expected.survives;
	const auto &name = expected.receiver.isEmpty()
		? stop.name
		: expected.receiver;
	NoteFrames(leg);
	const auto touches = Compare(LegTouchesAt(leg, name), {
		Expect(name, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, stop.point),
		}),
	});
	const auto mouse = CountProbeMouse(leg, name, state.screen);
	const auto held = expected.held ? Qt::LeftButton : Qt::NoButton;
	Check(
		touches.ok
			&& (mouse.presses == 1)
			&& (mouse.releases == expected.releases)
			&& (reading.before == Qt::NoButton)
			&& (reading.held == held)
			&& (reading.after == Qt::NoButton)
			&& (reading.destroyed != survives)
			&& (reading.targetAlive == survives)
			&& (reading.windowAlive == expected.windowAlive)
			&& (reading.stopped != survives)
			&& (reading.finished == expected.finished),
		u"touch self-test: %1: %2; on return the probe is %3 and Qt's mouse "
		u"buttons Qt::NoButton"_q.arg(
			leg.name,
			expected.what,
			(survives
				? u"alive, the sequence finished and not stopped"_q
				: u"destroyed, the sequence stopped"_q)),
		u"%1 probePresses=%2 probeReleases=%3 mouseButtons before=%4 "
		u"held=%5 onReturn=%6 targetAlive=%7 windowAlive=%8 %9"_q.arg(
			touches.text,
			QString::number(mouse.presses),
			QString::number(mouse.releases),
			ButtonsText(reading.before),
			ButtonsText(reading.held),
			ButtonsText(reading.after),
			FlagText(reading.targetAlive),
			FlagText(reading.windowAlive),
			u"destroyed=%1 stopped=%2 finished=%3"_q.arg(
				FlagText(reading.destroyed),
				FlagText(reading.stopped),
				FlagText(reading.finished))));
	const auto &stopping = leg.deliveries[expected.call];
	const auto &frame = stopping.frame;
	const auto cleanup = ReadCleanup(
		stopping,
		state.screen,
		expected.receiver,
		!expected.finished);
	const auto cleaned = expected.cleanup
		? cleanup.ok
		: (!frame.cleanupCancel && frame.cleanupRows.empty());
	Check(
		(frame.refusal == expected.refusal)
			&& (frame.submitted == expected.refusal.isEmpty())
			&& (frame.targetAlive == survives)
			&& (frame.windowAlive == expected.windowAlive)
			&& cleaned,
		u"touch self-test: %1: the %2 call, %3, reads targetAlive=%4 "
		u"windowAlive=%5, and %6"_q.arg(
			leg.name,
			survives ? u"finishing"_q : u"stopping"_q,
			(expected.refusal.isEmpty()
				? u"submitted"_q
				: u"refused \"%1\""_q.arg(expected.refusal)),
			FlagText(survives),
			FlagText(expected.windowAlive),
			(!expected.cleanup
				? u"no press was left for a stop cleanup"_q
				: expected.receiver.isEmpty()
				? u"its stop cleanup's one point-free TouchCancel made Qt "
					u"send the QWindow the release for the held press and "
					u"no widget anything"_q
				: u"its %1 cleanup's one point-free cancel, every contact "
					u"already released, made Qt send no TouchCancel and the "
					u"release for the held press to the QWindow and through "
					u"it to %2"_q.arg(
						survives ? u"terminal"_q : u"stop"_q,
						expected.receiver))),
		u"call %1: %2; %3"_q.arg(
			QString::number(expected.call + 1),
			TouchFrameDetails(frame),
			cleanup.text));
	CheckNothingAfterStop(leg, survives);
}

// A held leg past its end, stopped or, when it |survives|, only finished:
// the accepting probe's own Begin and End of contact 2 are why Qt
// synthesized no release; the button's own record of the last submitted
// call reads the helper's Leave, the terminal cleanup's release and the
// Leave once more - a Ui::AbstractButton keeps isOver() through a Leave
// while it is down - so on return it is neither down nor over, and |left|
// names it once for each Leave pass.
void CheckHeldHover(const State &state, const StopLeg &stop, bool survives) {
	const auto &leg = stop.leg;
	const auto &reading = stop.reading;
	const auto button = state.heldButton.data();
	const auto accept = state.heldAccept.data();
	const auto at = stop.second - QPointF(accept->pos() - button->pos());
	const auto probe = Compare(LegTouchesAt(leg, kHeldAcceptName), {
		Expect(kHeldAcceptName, QEvent::TouchBegin, {
			At(kSecondId, PointState::Pressed, at),
		}),
		Expect(kHeldAcceptName, QEvent::TouchEnd, {
			At(kSecondId, PointState::Released, at),
		}),
	});
	const auto &stopping = leg.deliveries[2];
	const auto record = stopping.journal
		| ranges::views::filter([](const JournalEvent &event) {
			return (event.where == kHeldButtonName);
		})
		| ranges::to_vector;
	const auto ordered = (record.size() == 3)
		&& (record[0].type == QEvent::Leave)
		&& (record[1].type == QEvent::MouseButtonRelease)
		&& (record[1].source == Qt::MouseEventNotSynthesized)
		&& (record[1].device.data() == state.screen)
		&& (record[2].type == QEvent::Leave);
	const auto passes = stopping.frame.left.filter(kHeldButtonName + u' ');
	Check(
		probe.ok
			&& ordered
			&& (passes.size() == 2)
			&& reading.downHeld
			&& reading.overHeld
			&& !reading.downAfter
			&& !reading.overAfter,
		u"touch self-test: %1: the accepting probe took contact 2's Begin "
		u"and End, so the button stayed down and over through the helper's "
		u"Leave; the %2 cleanup's release reached it, the Leave followed "
		u"once more, and on return it is neither down nor over"_q.arg(
			leg.name,
			survives ? u"terminal"_q : u"stop"_q),
		u"probe: %1; button: [%2] buttonLeavePasses=%3 downAndOverBefore="
		u"%4,%5 onReturn=%6,%7"_q.arg(
			probe.text,
			ListText(record, false),
			QString::number(passes.size()),
			FlagText(reading.downHeld),
			FlagText(reading.overHeld),
			FlagText(reading.downAfter),
			FlagText(reading.overAfter)));
	CheckRowsAndRoute(leg, { button, accept });
}

void CheckStops(const State &state) {
	CheckStop(state, state.pressStop, {
		.what = u"a probe that refused the Begin posted its own deletion "
			u"from the press Qt synthesized from it"_q,
		.cleanup = true,
		.windowAlive = true,
	});
	CheckStop(state, state.dropStop, {
		.what = u"a probe that refused the Begin and took Qt's synthesized "
			u"press was deleted between two calls, Qt::LeftButton still "
			u"held after its deletion"_q,
		.refusal = u"target-gone"_q,
		.call = 1,
		.cleanup = true,
		.held = true,
		.windowAlive = true,
	});
	CheckStop(state, state.heldFinish, {
		.what = u"contact 1 on a button that refused the Begin took Qt's "
			u"press, and contact 2 on an accepting probe kept Qt from "
			u"synthesizing its release, every widget left alive"_q,
		.receiver = kHeldButtonName,
		.call = 2,
		.cleanup = true,
		.held = true,
		.windowAlive = true,
		.finished = true,
		.survives = true,
	});
	CheckHeldHover(state, state.heldFinish, true);
	CheckAccepting(state, state.afterStops);
}

void CheckLeaveStops(const State &state) {
	CheckStop(state, state.leaveTarget, {
		.what = u"a refusing probe, armed before the release, posted its "
			u"own deletion from the Leave the helper sent as that release "
			u"ended the gesture"_q,
		.call = 1,
		.releases = 1,
		.held = true,
		.windowAlive = true,
		.finished = true,
	});
	CheckStop(state, state.leaveSecond, {
		.what = u"a refusing probe, armed before the release, posted its "
			u"window's deletion from the Leave the helper sent as that "
			u"release ended the gesture"_q,
		.call = 1,
		.releases = 1,
		.held = true,
		.finished = true,
	});
	CheckStop(state, state.heldStop, {
		.what = u"contact 1 on a button that refused the Begin took Qt's "
			u"press, contact 2 on an accepting probe kept Qt from "
			u"synthesizing its release, and the button's Leave, while it "
			u"was down, posted the deletion of the coordinate probe the "
			u"gesture was bound to"_q,
		.receiver = kHeldButtonName,
		.call = 2,
		.cleanup = true,
		.held = true,
		.windowAlive = true,
		.finished = true,
	});
	CheckHeldHover(state, state.heldStop, false);
}

// Two contacts pressed together on the pair's two refusing children make
// two Begin groups, each climbing on to the pair, which answers them from
// |script| in turn; then both are released together.
void RunPair(
		State &state,
		Leg &leg,
		PairReading &reading,
		std::vector<bool> script) {
	auto &journal = *state.journal;
	const auto pair = state.pair.data();
	const auto &p = state.samples.pair;
	leg.device = state.screen;
	reading.script = script;
	pair->scriptBegins(std::move(script));
	auto sequence = TouchSequence(pair);
	Deliver(leg, journal, sequence, {
		ContactPressed(kFirstId, p[0]),
		ContactPressed(kSecondId, p[1]),
	});
	reading.markedAfterBegin = pair->testAttribute(
		Qt::WA_WState_AcceptedTouchBeginEvent);
	Deliver(leg, journal, sequence, {
		ContactReleased(kFirstId),
		ContactReleased(kSecondId),
	});
	Close(leg, sequence);
}

void RunPairs(State &state) {
	state.pairRefuseFirst.name = u"independent groups, refuse then accept"_q;
	RunPair(
		state,
		state.pairRefuseFirst,
		state.pairRefuseFirstReading,
		{ false, true });
	state.pairAcceptFirst.name = u"independent groups, accept then refuse"_q;
	RunPair(
		state,
		state.pairAcceptFirst,
		state.pairAcceptFirstReading,
		{ true, false });
}

[[nodiscard]] QString AnswersText(const std::vector<bool> &answers) {
	auto list = QStringList();
	for (const auto answer : answers) {
		list.push_back(answer ? u"accept"_q : u"refuse"_q);
	}
	return list.join(u", "_q);
}

// The handlers' own record is the oracle: the order the children got their
// Begin in, which is the order of the groups, and the pair's answers. Qt
// sends an End only to a widget still marked, so after the pair refused
// the second group it never gets the End of the first one it accepted,
// and Qt synthesizes a release, which reaches the QWindow only.
void CheckPair(
		const State &state,
		const Leg &leg,
		const PairReading &reading) {
	NoteFrames(leg);
	const auto pair = state.pair.data();
	const auto left = state.pairLeft.data();
	const auto right = state.pairRight.data();
	const auto &p = state.samples.pair;
	const auto &begin = leg.deliveries.front();
	auto children = QStringList();
	auto answers = std::vector<bool>();
	for (const auto &event : begin.journal) {
		if (event.type != QEvent::TouchBegin) {
			continue;
		} else if (event.where == kPairName) {
			answers.push_back(event.accepted);
		} else if ((event.where == kPairLeftName)
			|| (event.where == kPairRightName)) {
			children.push_back(event.where);
		}
	}
	const auto leftFirst = !children.isEmpty()
		&& (children.front() == kPairLeftName);
	const auto leftPoint = At(kFirstId, PointState::Pressed, p[0]);
	const auto rightPoint = At(kSecondId, PointState::Pressed, p[1]);
	const auto firstPoint = leftFirst ? leftPoint : rightPoint;
	const auto secondPoint = leftFirst ? rightPoint : leftPoint;
	const auto marked = reading.script.back();
	auto expected = std::vector<JournalEvent>{
		Expect(kPairName, QEvent::TouchBegin, { firstPoint }),
		Expect(kPairName, QEvent::TouchBegin, { secondPoint }),
	};
	if (marked) {
		auto ended = secondPoint;
		ended.state = PointState::Released;
		expected.push_back(Expect(kPairName, QEvent::TouchEnd, { ended }));
	}
	const auto pairTouches = Compare(LegTouchesAt(leg, kPairName), expected);
	const auto leftTouches = Compare(LegTouchesAt(leg, kPairLeftName), {
		Expect(kPairLeftName, QEvent::TouchBegin, {
			At(kFirstId, PointState::Pressed, p[0] - QPointF(left->pos())),
		}),
	});
	const auto rightTouches = Compare(LegTouchesAt(leg, kPairRightName), {
		Expect(kPairRightName, QEvent::TouchBegin, {
			At(kSecondId, PointState::Pressed, p[1] - QPointF(right->pos())),
		}),
	});
	Check(
		pairTouches.ok
			&& leftTouches.ok
			&& rightTouches.ok
			&& (children.size() == 2)
			&& (answers == reading.script)
			&& (reading.markedAfterBegin == marked)
			&& (reading.markedAfterBegin != reading.script.front()),
		u"touch self-test: %1: the two contacts reach the two refusing "
		u"children and then their common ancestor as two Begin groups, "
		u"which it answers %2 in turn, so its "
		u"Qt::WA_WState_AcceptedTouchBeginEvent after the frame is the "
		u"second group's answer, not the initial group's, and it gets the "
		u"End %3"_q.arg(
			leg.name,
			AnswersText(reading.script),
			(marked
				? u"of the group it accepted"_q
				: u"of neither group"_q)),
		u"pair: %1; left: %2; right: %3; childOrder=[%4] answers=[%5] "
		u"markedAfterBegin=%6"_q.arg(
			pairTouches.text,
			leftTouches.text,
			rightTouches.text,
			children.join(u", "_q),
			AnswersText(answers),
			FlagText(reading.markedAfterBegin)));

	const auto offers = WidgetBeginRows(begin.frame);
	const auto firstChild = leftFirst ? left : right;
	const auto secondChild = leftFirst ? right : left;
	const auto offered = [&](std::size_t index, QWidget *widget, int climb) {
		return (offers[index]->receiver.data() == widget)
			&& (offers[index]->climb == climb);
	};
	const auto climbs = (offers.size() == 4)
		&& offered(0, firstChild, 1)
		&& offered(1, pair, 1)
		&& offered(2, secondChild, 2)
		&& offered(3, pair, 2);
	const auto &owner = leg.owner;
	const auto ambiguous = (owner.status == TouchOwnerStatus::Ambiguous)
		&& owner.widget.isNull()
		&& (owner.frame == 0)
		&& owner.label.startsWith(kPairName + u' ');
	Check(
		climbs && ambiguous && leg.finished && !leg.stopped,
		u"touch self-test: %1: the helper's rows hold climbs 1 and 2, each "
		u"a child and then the ancestor, and owner() is Ambiguous with no "
		u"widget - not the Accepted or Refused reading the ancestor's last "
		u"mark would give the initial climb"_q.arg(leg.name),
		u"%1 beginRows=[%2]"_q.arg(
			OwnerText(owner),
			BeginRowsText(begin.frame)));
	CheckRowsAndRoute(leg, { left, right, pair });

	const auto census = CountMouse(leg);
	auto releases = 0;
	for (const auto &event : leg.deliveries.back().journal) {
		releases += ((event.where == kWindowWhere)
			&& (event.type == QEvent::MouseButtonRelease)
			&& (event.source == Qt::MouseEventSynthesizedByQt)
			&& (event.device.data() == state.screen)
			&& (event.buttons == Qt::NoButton)) ? 1 : 0;
	}
	const auto windowOk = marked
		? (!census.windowMouse && !census.journalWindowMouse)
		: (releases == 1);
	Check(
		windowOk
			&& !census.widgetMouse
			&& !census.enter
			&& !census.journalProbeMouse
			&& !census.journalEnter
			&& !census.left,
		u"touch self-test: %1: no widget got a mouse event or an Enter, "
		u"and %2"_q.arg(
			leg.name,
			(marked
				? u"no window a mouse event"_q
				: u"the release Qt synthesized from the End no widget took "
					u"reached the QWindow only"_q)),
		u"%1 windowQtReleases=%2"_q.arg(
			MouseCensusText(census),
			QString::number(releases)));
}

void CheckPairs(const State &state) {
	CheckPair(state, state.pairRefuseFirst, state.pairRefuseFirstReading);
	CheckPair(state, state.pairAcceptFirst, state.pairAcceptFirstReading);
}

void CheckDevices(const State &state) {
	const auto census = ReadDevices(state.screen, state.pad);
	const auto screen = HarnessTouchDevice(TouchDeviceKind::Screen);
	const auto pad = HarnessTouchDevice(TouchDeviceKind::Pad);
	Check(
		(screen == state.screen)
			&& (pad == state.pad)
			&& census.roles
			&& (census.harness == 2)
			&& (census.screenEntries == 1)
			&& (census.padEntries == 1)
			&& !census.primaryIsHarness,
		u"touch self-test: after every probe leg the same two harness "
		u"devices are registered and the primary pointing device is still "
		u"none of them"_q,
		u"screenNow=%1 padNow=%2 %3"_q.arg(
			DeviceText(screen),
			DeviceText(pad),
			census.text));
}

void RunTap(State &state) {
	auto &leg = state.tap;
	auto &reading = state.tapReading;
	auto &journal = StartLeg(state, leg, u"scroll tap"_q);
	const auto scroll = state.scroll.get();
	const auto button = state.button.data();
	reading.topBefore = scroll->scrollTop();
	reading.topMax = scroll->scrollTopMax();
	reading.clicksBefore = journal.clicks;
	auto sequence = TouchSequence(scroll->viewport());
	const auto tap = state.samples.tap;
	Deliver(leg, journal, sequence, { ContactPressed(kFirstId, tap) });
	reading.clicksAfterBegin = journal.clicks;
	Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	reading.clicksAfter = journal.clicks;
	reading.topAfter = scroll->scrollTop();
	reading.over = button->isOver();
	reading.underMouse = button->underMouse();
	reading.contentOwns = state.content
		&& (sequence.owner().widget.data() == state.content.data());
	Close(leg, sequence);
}

void CheckTap(const State &state) {
	const auto &leg = state.tap;
	const auto &reading = state.tapReading;
	NoteFrames(leg);
	const auto button = state.button.data();
	const auto primary = QPointingDevice::primaryPointingDevice();
	auto qtRows = 0;
	auto appRows = 0;
	auto appFromPrimary = true;
	auto press = false;
	auto release = false;
	auto buttonRows = QStringList();
	for (const auto &delivery : leg.deliveries) {
		for (const auto &row : delivery.frame.rows) {
			if (!IsMouseType(row.type)) {
				continue;
			} else if (row.source == Qt::MouseEventSynthesizedByQt) {
				++qtRows;
			} else if (row.source == Qt::MouseEventSynthesizedByApplication) {
				++appRows;
				appFromPrimary = appFromPrimary
					&& (row.device.data() == primary);
			}
			if (row.window || (row.receiver.data() != button)) {
				continue;
			}
			buttonRows.push_back(RowText(row));
			const auto app = (row.source
				== Qt::MouseEventSynthesizedByApplication);
			press = press || (app && (row.type == QEvent::MouseButtonPress));
			release = release
				|| (app && (row.type == QEvent::MouseButtonRelease));
		}
	}
	const auto census = CountMouse(leg);
	Check(
		(reading.clicksAfterBegin == reading.clicksBefore)
			&& (reading.clicksAfter == reading.clicksBefore + 1)
			&& press
			&& release
			&& !qtRows
			&& (appRows > 0)
			&& appFromPrimary
			&& (reading.topAfter == reading.topBefore),
		u"touch self-test: scroll tap: a tap on the button inside the "
		u"Ui::ScrollArea clicks it exactly once, through the press and "
		u"release the scroll area synthesizes itself, with no "
		u"Qt-synthesized mouse"_q,
		u"clicks before=%1 afterBegin=%2 afterEnd=%3 scrollTop=%4->%5 "
		u"max=%6 qtRows=%7 applicationRows=%8 applicationFromPrimary=%9 "
		u"primary=%10 buttonRows=[%11] %12"_q.arg(
			QString::number(reading.clicksBefore),
			QString::number(reading.clicksAfterBegin),
			QString::number(reading.clicksAfter),
			QString::number(reading.topBefore),
			QString::number(reading.topAfter),
			QString::number(reading.topMax),
			QString::number(qtRows),
			QString::number(appRows),
			FlagText(appFromPrimary)).arg(
			DeviceText(primary),
			buttonRows.join(u"; "_q),
			MouseCensusText(census)));
	Check(
		reading.contentOwns,
		u"touch self-test: scroll tap: the scroll area's content widget "
		u"owns the tap"_q,
		u"%1 beginRows=[%2]"_q.arg(
			OwnerText(leg.owner),
			leg.deliveries.empty()
				? QString()
				: BeginRowsText(leg.deliveries.front().frame)));
	Check(
		!reading.over
			&& !reading.underMouse
			&& LeftReached(leg, kButtonName)
			&& leg.finished,
		u"touch self-test: scroll tap: after the helper's Leave the button "
		u"is not hovered"_q,
		u"isOver=%1 underMouse=%2 left=[%3]"_q.arg(
			FlagText(reading.over),
			FlagText(reading.underMouse),
			LeftText(leg)));
	const auto route = ReadWindowRoute(leg);
	Check(
		route.ok,
		u"touch self-test: scroll tap: every frame reached the QWindow "
		u"once, spontaneously, from the harness touchscreen"_q,
		route.text);
}

void RunSwipe(State &state) {
	auto &leg = state.swipe;
	auto &reading = state.swipeReading;
	auto &journal = StartLeg(state, leg, u"scroll swipe"_q);
	const auto scroll = state.scroll.get();
	const auto button = state.button.data();
	const auto start = state.samples.tap;
	reading.step = state.samples.swipeStep;
	reading.topBefore = scroll->scrollTop();
	reading.topMax = scroll->scrollTopMax();
	reading.clicksBefore = journal.clicks;
	auto sequence = TouchSequence(scroll->viewport());
	Deliver(leg, journal, sequence, { ContactPressed(kFirstId, start) });
	for (auto k = 1; k <= kSwipeUpdates; ++k) {
		const auto point = start - QPointF(0, k * reading.step);
		Deliver(leg, journal, sequence, { ContactUpdated(kFirstId, point) });
	}
	Deliver(leg, journal, sequence, { ContactReleased(kFirstId) });
	reading.topAfter = scroll->scrollTop();
	reading.clicksAfter = journal.clicks;
	reading.over = button->isOver();
	reading.underMouse = button->underMouse();
	reading.contentOwns = state.content
		&& (sequence.owner().widget.data() == state.content.data());
	Close(leg, sequence);

	// No kinetic scroll is judged: the area goes before its timer runs.
	state.scroll = nullptr;
	reading.released = !state.button && !state.content;
}

void CheckSwipe(const State &state) {
	const auto &leg = state.swipe;
	const auto &reading = state.swipeReading;
	NoteFrames(leg);
	auto qtRows = 0;
	auto appRows = 0;
	for (const auto &delivery : leg.deliveries) {
		for (const auto &row : delivery.frame.rows) {
			if (!IsMouseType(row.type)) {
				continue;
			} else if (row.source == Qt::MouseEventSynthesizedByQt) {
				++qtRows;
			} else if (row.source == Qt::MouseEventSynthesizedByApplication) {
				++appRows;
			}
		}
	}
	Check(
		(reading.topAfter > reading.topBefore)
			&& (reading.clicksAfter == reading.clicksBefore)
			&& !qtRows,
		u"touch self-test: scroll swipe: a vertical swipe starting on the "
		u"button scrolls the area and clicks nothing, with no "
		u"Qt-synthesized mouse"_q,
		u"scrollTop=%1->%2 max=%3 step=%4 updates=%5 "
		u"predictedScroll=%6 startDragDistance=%7 clicks=%8->%9 "
		u"qtRows=%10 applicationRows=%11 isOver=%12 underMouse=%13"_q.arg(
			QString::number(reading.topBefore),
			QString::number(reading.topAfter),
			QString::number(reading.topMax),
			QString::number(reading.step),
			QString::number(kSwipeUpdates),
			QString::number((kSwipeUpdates - 1) * reading.step),
			QString::number(QApplication::startDragDistance()),
			QString::number(reading.clicksBefore),
			QString::number(reading.clicksAfter)).arg(
			QString::number(qtRows),
			QString::number(appRows),
			FlagText(reading.over),
			FlagText(reading.underMouse)));
	const auto route = ReadWindowRoute(leg);
	Check(
		route.ok && reading.contentOwns && leg.finished && reading.released,
		u"touch self-test: scroll swipe: the content widget owned the "
		u"swipe, every frame reached the QWindow once, and the scroll area "
		u"was released right after it"_q,
		u"%1 contentOwned=%2 released=%3 %4"_q.arg(
			OwnerText(leg.owner),
			FlagText(reading.contentOwns),
			FlagText(reading.released),
			route.text));
}

void CheckTeardown(const State &state) {
	Check(
		!state.window
			&& !state.handle
			&& !state.secondWindow
			&& !state.secondHandle,
		u"touch self-test: teardown destroyed the harness windows and "
		u"their QWindows"_q,
		u"window=%1 handle=%2 secondWindow=%3 secondHandle=%4 "
		u"destroyedProbes=[%5]"_q.arg(
			FlagText(state.window.get() != nullptr),
			FlagText(!state.handle.isNull()),
			FlagText(state.secondWindow.get() != nullptr),
			FlagText(!state.secondHandle.isNull()),
			state.journal->destroyed.join(u", "_q)));
}

} // namespace

void AppendTouchSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();

	// The release point, registered when the self-test is appended: a
	// stage timeout, the watchdog and skip-to-end skip the teardown stage,
	// and a harness window left alive would float over the console.
	runner->onFinish([=] {
		ReleaseFixture(*state);
	});
	const auto skip = Fn<QString()>([=] {
		return state->built
			? QString()
			: u"the touch self-test fixture gate failed: %1"_q.arg(
				state->gate);
	});
	// Each stage runs one step on the shared fixture and judges it in its
	// then; all but the fixture and the teardown skip after a failed gate.
	const auto add = [=](
			const QString &name,
			Fn<void(State&)> run,
			Fn<void(State&)> then,
			bool gated = true) {
		runner->add({
			.name = u"touch self-test: "_q + name,
			.skipReason = gated ? skip : nullptr,
			.run = [=] { run(*state); },
			.then = [=] { then(*state); },
		});
	};
	add(u"fixture and devices"_q, BuildFixture, CheckFixture, false);
	add(u"accepting probe"_q, [](State &fixture) {
		RunAccepting(fixture, fixture.direct, u"accepting probe"_q);
	}, [](State &fixture) {
		CheckAccepting(fixture, fixture.direct);
	});
	add(u"climb from a non-accepting child"_q, RunClimb, CheckClimb);
	add(u"owner under a refusing child"_q, RunTrap, CheckTrap);
	add(u"unhandled control"_q, RunControl, CheckControl);
	add(u"accepting probe after the control"_q, [](State &fixture) {
		RunAccepting(fixture, fixture.residue, u"after the control"_q);
	}, [](State &fixture) {
		CheckAccepting(fixture, fixture.residue);
	});
	add(u"cancel and recovery"_q, RunCancel, CheckCancel);
	add(u"multi-contact frames"_q, RunMulti, CheckMulti);
	add(u"touchpad contacts"_q, RunPad, CheckPad);
	add(u"postponed destruction"_q, RunLifetime, CheckLifetime);
	add(u"terminal cleanup after a synthesized press"_q, RunStops, CheckStops);
	add(
		u"Leave cleanup destroying the target or its window"_q,
		RunLeaveStops,
		CheckLeaveStops);
	add(u"owner across independent Begin groups"_q, RunPairs, CheckPairs);
	add(u"devices after every probe leg"_q, [](State&) {}, CheckDevices);
	add(u"scroll area tap"_q, RunTap, CheckTap);
	add(u"scroll area swipe"_q, RunSwipe, CheckSwipe);
	add(u"teardown"_q, ReleaseFixture, CheckTeardown, false);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
