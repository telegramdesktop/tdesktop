/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_section.h"

#include "history/admin_log/history_admin_log_inner.h"
#include "history/admin_log/history_admin_log_filter.h"
#include "profile/profile_back_button.h"
#include "core/shortcuts.h"
#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "boxes/confirm_box.h"
#include "base/timer.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "styles/style_info.h"

namespace AdminLog {

class FixedBar final : public TWidget, private base::Subscriber {
public:
	FixedBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel);

	base::Observable<void> showFilterSignal;
	base::Observable<void> searchCancelledSignal;
	base::Observable<QString> searchSignal;

	// When animating mode is enabled the content is hidden and the
	// whole fixed bar acts like a back button.
	void setAnimatingMode(bool enabled);

	void applyFilter(const FilterValue &value);
	void goBack();
	void showSearch();
	bool setSearchFocus() {
		if (_searchShown) {
			_field->setFocus();
			return true;
		}
		return false;
	}

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void toggleSearch();
	void cancelSearch();
	void searchUpdated();
	void applySearch();
	void searchAnimationCallback();

	not_null<Window::SessionController*> _controller;
	not_null<ChannelData*> _channel;
	object_ptr<Ui::FlatInput> _field;
	object_ptr<Profile::BackButton> _backButton;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::CrossButton> _cancel;
	object_ptr<Ui::RoundButton> _filter;

	Ui::Animations::Simple _searchShownAnimation;
	bool _searchShown = false;
	bool _animatingMode = false;
	base::Timer _searchTimer;

};

object_ptr<Window::SectionWidget> SectionMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, _channel);
	result->setInternalState(geometry, this);
	return std::move(result);
}

FixedBar::FixedBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel) : TWidget(parent)
, _controller(controller)
, _channel(channel)
, _field(this, st::historyAdminLogSearchField, tr::lng_dlg_filter())
, _backButton(this, tr::lng_admin_log_title_all(tr::now))
, _search(this, st::topBarSearch)
, _cancel(this, st::historyAdminLogCancelSearch)
, _filter(this, tr::lng_admin_log_filter(), st::topBarButton) {
	_backButton->moveToLeft(0, 0);
	_backButton->setClickedCallback([=] { goBack(); });
	_filter->setClickedCallback([=] { showFilterSignal.notify(); });
	_search->setClickedCallback([=] { showSearch(); });
	_cancel->setClickedCallback([=] { cancelSearch(); });
	_field->hide();
	connect(_field, &Ui::FlatInput::cancelled, [=] { cancelSearch(); });
	connect(_field, &Ui::FlatInput::changed, [=] { searchUpdated(); });
	connect(_field, &Ui::FlatInput::submitted, [=] { applySearch(); });
	_searchTimer.setCallback([=] { applySearch(); });

	_cancel->hide(anim::type::instant);
}

void FixedBar::applyFilter(const FilterValue &value) {
	auto hasFilter = (value.flags != 0) || !value.allUsers;
	_backButton->setText(hasFilter
		? tr::lng_admin_log_title_selected(tr::now)
		: tr::lng_admin_log_title_all(tr::now));
}

void FixedBar::goBack() {
	_controller->showBackFromStack();
}

void FixedBar::showSearch() {
	if (!_searchShown) {
		toggleSearch();
	}
}

void FixedBar::toggleSearch() {
	_searchShown = !_searchShown;
	_cancel->toggle(_searchShown, anim::type::normal);
	_searchShownAnimation.start(
		[=] { searchAnimationCallback(); },
		_searchShown ? 0. : 1.,
		_searchShown ? 1. : 0.,
		st::historyAdminLogSearchSlideDuration);
	_search->setDisabled(_searchShown);
	if (_searchShown) {
		_field->show();
		_field->setFocus();
	} else {
		searchCancelledSignal.notify(true);
	}
}

void FixedBar::searchAnimationCallback() {
	if (!_searchShownAnimation.animating()) {
		_field->setVisible(_searchShown);
		_search->setIconOverride(
			_searchShown ? &st::topBarSearch.icon : nullptr,
			_searchShown ? &st::topBarSearch.icon : nullptr);
		_search->setRippleColorOverride(
			_searchShown ? &st::topBarBg : nullptr);
		_search->setCursor(
			_searchShown ? style::cur_default : style::cur_pointer);
	}
	resizeToWidth(width());
}

void FixedBar::cancelSearch() {
	if (_searchShown) {
		if (!_field->getLastText().isEmpty()) {
			_field->setText(QString());
			_field->updatePlaceholder();
			_field->setFocus();
			applySearch();
		} else {
			toggleSearch();
		}
	}
}

void FixedBar::searchUpdated() {
	if (_field->getLastText().isEmpty()) {
		applySearch();
	} else {
		_searchTimer.callOnce(AutoSearchTimeout);
	}
}

void FixedBar::applySearch() {
	searchSignal.notify(_field->getLastText());
}

int FixedBar::resizeGetHeight(int newWidth) {
	auto filterLeft = newWidth - _filter->width();
	_filter->moveToLeft(filterLeft, 0);

	auto cancelLeft = filterLeft - _cancel->width();
	_cancel->moveToLeft(cancelLeft, 0);

	auto searchShownLeft = st::topBarArrowPadding.left();
	auto searchHiddenLeft = filterLeft - _search->width();
	auto searchShown = _searchShownAnimation.value(_searchShown ? 1. : 0.);
	auto searchCurrentLeft = anim::interpolate(searchHiddenLeft, searchShownLeft, searchShown);
	_search->moveToLeft(searchCurrentLeft, 0);
	_backButton->resizeToWidth(searchCurrentLeft);
	_backButton->moveToLeft(0, 0);

	auto newHeight = _backButton->height();
	auto fieldLeft = searchShownLeft + _search->width();
	_field->setGeometryToLeft(fieldLeft, st::historyAdminLogSearchTop, cancelLeft - fieldLeft, _field->height());

	return newHeight;
}

void FixedBar::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
		if (_animatingMode) {
			setAttribute(Qt::WA_OpaquePaintEvent, false);
			hideChildren();
		} else {
			setAttribute(Qt::WA_OpaquePaintEvent);
			showChildren();
			_field->hide();
			_cancel->setVisible(false);
		}
		show();
	}
}

void FixedBar::paintEvent(QPaintEvent *e) {
	if (!_animatingMode) {
		Painter p(this);
		p.fillRect(e->rect(), st::topBarBg);
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		goBack();
	} else {
		TWidget::mousePressEvent(e);
	}
}

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel)
: Window::SectionWidget(parent, controller)
, _scroll(this, st::historyScroll, false)
, _fixedBar(this, controller, channel)
, _fixedBarShadow(this)
, _whatIsThis(this, tr::lng_admin_log_about(tr::now).toUpper(), st::historyComposeButton) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	subscribe(_fixedBar->showFilterSignal, [this] { showFilter(); });
	subscribe(_fixedBar->searchCancelledSignal, [this] { setInnerFocus(); });
	subscribe(_fixedBar->searchSignal, [this](const QString &query) { _inner->applySearch(query); });
	_fixedBar->show();

	_fixedBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, controller, channel));
	_inner->showSearchSignal(
	) | rpl::start_with_next([=] {
		_fixedBar->showSearch();
	}, lifetime());
	_inner->cancelSignal(
	) | rpl::start_with_next([=] {
		_fixedBar->goBack();
	}, lifetime());
	_inner->scrollToSignal(
	) | rpl::start_with_next([=](int top) {
		_scroll->scrollToY(top);
	}, lifetime());

	_scroll->move(0, _fixedBar->height());
	_scroll->show();

	connect(_scroll, &Ui::ScrollArea::scrolled, this, [this] { onScroll(); });

	_whatIsThis->setClickedCallback([=] { Ui::show(Box<InformBox>(tr::lng_admin_log_about_text(tr::now))); });

	setupShortcuts();
}

void Widget::showFilter() {
	_inner->showFilter([this](FilterValue &&filter) {
		applyFilter(std::move(filter));
		Ui::hideLayer();
	});
}

void Widget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(Adaptive::OneColumn() ? 0 : st::lineWidth, _fixedBar->height());
}

not_null<ChannelData*> Widget::channel() const {
	return _inner->channel();
}

Dialogs::RowDescriptor Widget::activeChat() const {
	return {
		channel()->owner().history(channel()),
		FullMsgId(channel()->bareId(), ShowAtUnreadMsgId)
	};
}

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _fixedBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _fixedBarShadow->show();
	return result;
}

void Widget::doSetInnerFocus() {
	if (!_fixedBar->setSearchFocus()) {
		_inner->setFocus();
	}
}

bool Widget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<SectionMemento*>(memento.get())) {
		if (logMemento->getChannel() == channel()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void Widget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow() && !Ui::isLayerShown() && inFocusChain();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 2) && request->handle([=] {
			_fixedBar->showSearch();
			return true;
		});
	}, lifetime());
}

std::unique_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_unique<SectionMemento>(channel());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<SectionMemento*> memento) {
	memento->setScrollTop(_scroll->scrollTop());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<SectionMemento*> memento) {
	_inner->restoreState(memento);
	auto scrollTop = memento->getScrollTop();
	_scroll->scrollToY(scrollTop);
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}

	auto contentWidth = width();

	auto newScrollTop = _scroll->scrollTop() + topDelta();
	_fixedBar->resizeToWidth(contentWidth);
	_fixedBarShadow->resize(contentWidth, st::lineWidth);

	auto bottom = height();
	auto scrollHeight = bottom - _fixedBar->height() - _whatIsThis->height();
	auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_inner->restoreScrollPosition();
	}

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
	auto fullWidthButtonRect = myrtlrect(0, bottom - _whatIsThis->height(), contentWidth, _whatIsThis->height());
	_whatIsThis->setGeometry(fullWidthButtonRect);
}

void Widget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	SectionWidget::PaintBackground(this, e->rect());
}

void Widget::onScroll() {
	int scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void Widget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_fixedBar->setAnimatingMode(true);
	if (params.withTopBarShadow) _fixedBarShadow->show();
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
}

bool Widget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

void Widget::applyFilter(FilterValue &&value) {
	_fixedBar->applyFilter(value);
	_inner->applyFilter(std::move(value));
}

} // namespace AdminLog

