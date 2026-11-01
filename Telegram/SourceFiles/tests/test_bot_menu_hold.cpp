/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_main.h"

#include "base/invoke_queued.h"
#include "history/view/controls/history_view_bot_menu_hold.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/rp_window.h"

#include <crl/crl_time.h>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QtGui/QtEvents>

namespace Test {
namespace {

using HistoryView::Controls::kBotMenuHoldDuration;
using HistoryView::Controls::BotMenuHoldTracker;

class PressDragPanel final : public Ui::RpWidget {
public:
	PressDragPanel(QWidget *parent) : Ui::RpWidget(parent) {
		setMouseTracking(true);
	}

	static constexpr auto kRows = 3;
	static constexpr auto kRowHeight = 40;
	static constexpr auto kRowsTop = 20;

	void startPressDragSelect() {
		if (_pressDragSelect
			|| !isVisible()
			|| (mouseGrabber() && mouseGrabber() != this)) {
			return;
		}
		_pressDragSelect = true;
		updateHover(QCursor::pos());
		grabMouse();
	}

	[[nodiscard]] int hover() const {
		return _hover;
	}

	[[nodiscard]] bool gestureActive() const {
		return _pressDragSelect;
	}

	[[nodiscard]] rpl::producer<int> selections() const {
		return _selections.events();
	}

	[[nodiscard]] rpl::producer<> cancels() const {
		return _cancels.events();
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		_pressDragSelect = false;
		updateHover(e->globalPos());
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		updateHover(e->globalPos());
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		updateHover(e->globalPos());
		if (_pressDragSelect) {
			_pressDragSelect = false;
			if (mouseGrabber() == this) {
				releaseMouse();
			}
			if (_hover >= 0) {
				_selections.fire_copy(_hover);
			} else {
				_cancels.fire({});
			}
			return;
		}
	}

	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		p.fillRect(rect(), QColor(255, 255, 255));
		for (auto i = 0; i != kRows; ++i) {
			p.fillRect(
				rowRect(i),
				(_hover == i) ? QColor(180, 210, 255) : QColor(235, 235, 235));
		}
	}

private:
	[[nodiscard]] QRect rowRect(int index) const {
		return QRect(
			0,
			kRowsTop + index * kRowHeight,
			width(),
			kRowHeight);
	}

	void updateHover(QPoint global) {
		const auto local = mapFromGlobal(global);
		_hover = -1;
		for (auto i = 0; i != kRows; ++i) {
			if (rowRect(i).contains(local)) {
				_hover = i;
			}
		}
		update();
	}

	bool _pressDragSelect = false;
	int _hover = -1;
	rpl::event_stream<int> _selections;
	rpl::event_stream<> _cancels;

};

void runFor(crl::time ms) {
	const auto until = crl::now() + ms;
	while (crl::now() < until) {
		QCoreApplication::processEvents(
			QEventLoop::AllEvents,
			10);
	}
}

void sendPress(
		QWidget *w,
		QPoint local,
		Qt::MouseButton button = Qt::LeftButton) {
	auto e = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		w->mapToGlobal(local),
		button,
		button,
		Qt::NoModifier);
	QCoreApplication::sendEvent(w, &e);
}

void sendRelease(
		QWidget *w,
		QPoint local,
		Qt::MouseButton button = Qt::LeftButton) {
	auto e = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		w->mapToGlobal(local),
		button,
		Qt::NoButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(w, &e);
}

void sendMove(QWidget *w, QPoint local) {
	auto e = QMouseEvent(
		QEvent::MouseMove,
		local,
		w->mapToGlobal(local),
		Qt::NoButton,
		Qt::LeftButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(w, &e);
}

void runScenario(not_null<Ui::RpWidget*> body) {
	auto checks = 0;
	auto failures = 0;
	const auto check = [&](
			bool value,
			const QString &what) {
		++checks;
		if (!value) {
			++failures;
		}
		qDebug() << (value ? "PASS:" : "FAIL:") << what;
	};

	auto button = Ui::CreateChild<Ui::AbstractButton>(body);
	button->setGeometry(QRect(380, 540, 40, 40));
	button->show();

	auto panel = Ui::CreateChild<PressDragPanel>(body);
	panel->setGeometry(QRect(0, 320, 800, 200));
	panel->hide();

	auto clicks = 0;
	button->clicks(
	) | rpl::on_next([&](Qt::MouseButton mouseButton) {
		++clicks;
	}, button->lifetime());

	auto activations = 0;
	button->lifetime().make_state<BotMenuHoldTracker>(
		button,
		kBotMenuHoldDuration,
		[&] {
			++activations;
			panel->show();
			panel->startPressDragSelect();
		});

	auto selections = -1;
	auto cancels = 0;
	panel->selections(
	) | rpl::on_next([&](int index) {
		selections = index;
	}, panel->lifetime());
	panel->cancels(
	) | rpl::on_next([&] {
		++cancels;
		panel->hide();
	}, panel->lifetime());

	const auto quick = kBotMenuHoldDuration / 5;
	const auto held = kBotMenuHoldDuration + 200;
	const auto buttonCenter = QPoint(20, 20);

	sendPress(button, buttonCenter);
	runFor(quick);
	sendRelease(button, buttonCenter);
	runFor(quick);
	check(clicks == 1, "quick click fires the normal click");
	check(activations == 0, "quick click does not activate the gesture");

	sendPress(button, buttonCenter);
	runFor(quick);
	check(activations == 0, "short hold is still below the threshold");
	runFor(held);
	check(activations == 1, "hold past the duration activates once");
	check(clicks == 1, "activation does not click the button");
	check(button->isDown() == false, "activation clears the button press");
	check(panel->gestureActive(), "panel entered press-drag selection");
	check(
		QWidget::mouseGrabber() == panel,
		"panel grabbed the mouse");

	const auto rowCenter = QPoint(400, PressDragPanel::kRowsTop
		+ PressDragPanel::kRowHeight + PressDragPanel::kRowHeight / 2);
	sendMove(panel, rowCenter);
	check(panel->hover() == 1, "drag selects the row under the cursor");
	sendRelease(panel, rowCenter);
	runFor(quick);
	check(selections == 1, "release over a row selects the command");
	check(cancels == 0, "release over a row is not a cancel");
	check(
		QWidget::mouseGrabber() != panel,
		"panel released the mouse after selection");
	check(clicks == 1, "button still was not clicked by the gesture");

	sendPress(button, buttonCenter);
	runFor(held);
	check(activations == 2, "second hold activates once more");
	check(panel->gestureActive(), "panel gesture is active again");
	sendMove(panel, QPoint(400, 195));
	check(panel->hover() == -1, "empty space is not a selection");
	sendRelease(panel, QPoint(400, 195));
	runFor(quick);
	check(cancels == 1, "release outside rows cancels");
	check(selections == 1, "cancel does not select a command");
	check(panel->isHidden(), "cancel wiring hides the panel");

	sendPress(button, buttonCenter, Qt::RightButton);
	check(activations == 3, "right button press activates at once");
	check(panel->gestureActive(), "right button gesture is active");
	check(clicks == 1, "right button press does not click");
	const auto firstRowCenter = QPoint(400, PressDragPanel::kRowsTop
		+ PressDragPanel::kRowHeight / 2);
	sendMove(panel, firstRowCenter);
	check(panel->hover() == 0, "right drag selects the row under the cursor");
	sendRelease(panel, firstRowCenter);
	runFor(quick);
	check(selections == 0, "right release over a row selects the command");
	check(cancels == 1, "right release over a row is not a cancel");
	check(
		QWidget::mouseGrabber() != panel,
		"panel released the mouse after right selection");
	check(clicks == 1, "right button still did not click");

	sendPress(button, buttonCenter);
	runFor(quick);
	sendRelease(button, buttonCenter);
	runFor(quick);
	check(clicks == 2, "normal click keeps working after gestures");
	check(activations == 3, "quick click does not activate");

	qDebug() << "test_bot_menu_hold:"
		<< checks << "checks," << failures << "failures";
	QCoreApplication::exit(failures ? 1 : 0);
}

} // namespace

QString name() {
	return u"test_bot_menu_hold"_q;
}

void test(not_null<Ui::RpWindow*>, not_null<Ui::RpWidget*> body) {
	InvokeQueued(body, [=] {
		runScenario(body);
	});
}

} // namespace Test
