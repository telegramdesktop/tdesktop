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
#include "ui/chat/chat_style.h"
#include "ui/controls/swipe_handler.h"
#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/ui_utility.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "window/themes/window_theme.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "base/timer.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_info.h"

namespace AdminLog {

class FixedBar final : public Ui::RpWidget {
public:
	FixedBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel);

	[[nodiscard]] rpl::producer<> showFilterRequests() const;
	[[nodiscard]] rpl::producer<> searchCancelRequests() const;
	[[nodiscard]] rpl::producer<QString> searchRequests() const;

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
	object_ptr<Ui::InputField> _field;
	object_ptr<Profile::BackButton> _backButton;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::CrossButton> _cancel;
	object_ptr<Ui::RoundButton> _filter;

	Ui::Animations::Simple _searchShownAnimation;
	bool _searchShown = false;
	bool _animatingMode = false;
	base::Timer _searchTimer;

	rpl::event_stream<> _searchCancelRequests;
	rpl::event_stream<QString> _searchRequests;

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
	return result;
}

FixedBar::FixedBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel)
: RpWidget(parent)
, _controller(controller)
, _channel(channel)
, _field(this, st::defaultMultiSelectSearchField, tr::lng_dlg_filter())
, _backButton(
	this,
	&controller->session(),
	tr::lng_admin_log_title_all(tr::now),
	controller->adaptive().oneColumnValue())
, _search(this, st::topBarSearch)
, _cancel(this, st::historyAdminLogCancelSearch)
, _filter(this, tr::lng_admin_log_filter(), st::topBarButton) {
	_backButton->moveToLeft(0, 0);
	_backButton->setClickedCallback([=] { goBack(); });
	_search->setClickedCallback([=] { showSearch(); });
	_cancel->setClickedCallback([=] { cancelSearch(); });
	_field->hide();
	_filter->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	_field->cancelled(
	) | rpl::start_with_next([=] {
		cancelSearch();
	}, _field->lifetime());
	_field->changes(
	) | rpl::start_with_next([=] {
		searchUpdated();
	}, _field->lifetime());
	_field->submits(
	) | rpl::start_with_next([=] { applySearch(); }, _field->lifetime());
	_searchTimer.setCallback([=] { applySearch(); });

	_cancel->hide(anim::type::instant);
}

void FixedBar::applyFilter(const FilterValue &value) {
	auto hasFilter = value.flags || value.admins;
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
		_searchCancelRequests.fire({});
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
			_field->clear();
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
	_searchRequests.fire_copy(_field->getLastText());
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

rpl::producer<> FixedBar::showFilterRequests() const {
	return _filter->clicks() | rpl::to_empty;
}

rpl::producer<> FixedBar::searchCancelRequests() const {
	return _searchCancelRequests.events();
}

rpl::producer<QString> FixedBar::searchRequests() const {
	return _searchRequests.events();
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
		auto p = QPainter(this);
		p.fillRect(e->rect(), st::topBarBg);
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		goBack();
	} else {
		RpWidget::mousePressEvent(e);
	}
}

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel)
: Window::SectionWidget(parent, controller, rpl::single<PeerData*>(channel))
, _scroll(this, st::historyScroll, false)
, _fixedBar(this, controller, channel)
, _fixedBarShadow(this)
, _whatIsThis(
		this,
		tr::lng_admin_log_about(tr::now),
		st::historyComposeButton) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->showFilterRequests(
	) | rpl::start_with_next([=] {
		showFilter();
	}, lifetime());
	_fixedBar->searchCancelRequests(
	) | rpl::start_with_next([=] {
		setInnerFocus();
	}, lifetime());
	_fixedBar->searchRequests(
	) | rpl::start_with_next([=](const QString &query) {
		_inner->applySearch(query);
	}, lifetime());
	_fixedBar->show();

	_fixedBarShadow->raise();

	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

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
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		onScroll();
	}, lifetime());

	_whatIsThis->setClickedCallback([=] {
		controller->show(Ui::MakeInformBox(channel->isMegagroup()
			? tr::lng_admin_log_about_text()
			: tr::lng_admin_log_about_text_channel()));
	});

	setupShortcuts();
	setupSwipeReply();
}

void Widget::showFilter() {
	_inner->showFilter([this](FilterValue &&filter) {
		applyFilter(std::move(filter));
		controller()->hideLayer();
	});
}

void Widget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn()
			? 0
			: st::lineWidth,
		_fixedBar->height());
}

not_null<ChannelData*> Widget::channel() const {
	return _inner->channel();
}

Dialogs::RowDescriptor Widget::activeChat() const {
	return {
		channel()->owner().history(channel()),
		FullMsgId(channel()->id, ShowAtUnreadMsgId)
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
		return Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& isActiveWindow();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 2) && request->handle([=] {
			_fixedBar->showSearch();
			return true;
		});
	}, lifetime());
}

void Widget::setupSwipeReply() {
	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation > 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						auto context = _inner->preparePaintContext({});
						return {
							context.st->msgServiceBg()->c,
							context.st->msgServiceFg()->c,
						};
					});
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};

	auto init = [=](int, Qt::LayoutDirection direction) {
		if (direction == Qt::RightToLeft) {
			return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				controller()->showBackFromStack();
			});
		}
		return Ui::Controls::SwipeHandlerFinishData();
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = _inner.data(),
		.scroll = _scroll.data(),
		.update = std::move(update),
		.init = std::move(init),
	});
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_shared<SectionMemento>(channel());
	saveState(result.get());
	return result;
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
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	const auto clip = e->rect();
	SectionWidget::PaintBackground(controller(), _inner->theme(), this, clip);
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

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

void Widget::applyFilter(FilterValue &&value) {
	_fixedBar->applyFilter(value);
	_inner->applyFilter(std::move(value));
}

} // namespace AdminLog

