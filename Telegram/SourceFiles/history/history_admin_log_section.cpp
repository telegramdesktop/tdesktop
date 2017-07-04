/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_admin_log_section.h"

#include "history/history_admin_log_inner.h"
#include "history/history_admin_log_filter.h"
#include "profile/profile_back_button.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "window/themes/window_theme.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"

namespace AdminLog {

// If we require to support more admins we'll have to rewrite this anyway.
constexpr auto kMaxChannelAdmins = 200;

class FixedBar final : public TWidget, private base::Subscriber {
public:
	FixedBar(QWidget *parent, gsl::not_null<ChannelData*> channel, base::lambda<void()> showFilterCallback);

	// When animating mode is enabled the content is hidden and the
	// whole fixed bar acts like a back button.
	void setAnimatingMode(bool enabled);

	void applyFilter(const FilterValue &value);
	void goBack();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	gsl::not_null<ChannelData*> _channel;
	object_ptr<Profile::BackButton> _backButton;
	object_ptr<Ui::RoundButton> _filter;

	bool _animatingMode = false;

};

object_ptr<Window::SectionWidget> SectionMemento::createWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller, const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _channel);
	result->setInternalState(geometry, this);
	return std::move(result);
}

FixedBar::FixedBar(QWidget *parent, gsl::not_null<ChannelData*> channel, base::lambda<void()> showFilterCallback) : TWidget(parent)
, _channel(channel)
, _backButton(this, lang(lng_admin_log_title_all))
, _filter(this, langFactory(lng_admin_log_filter), st::topBarButton) {
	_backButton->moveToLeft(0, 0);
	_backButton->setClickedCallback([this] { goBack(); });
	_filter->setClickedCallback([this, showFilterCallback] { showFilterCallback(); });
}

void FixedBar::applyFilter(const FilterValue &value) {
	auto hasFilter = (value.flags != 0) || !value.allUsers;
	_backButton->setText(lang(hasFilter ? lng_admin_log_title_selected : lng_admin_log_title_all));
}

void FixedBar::goBack() {
	App::main()->showBackFromStack();
}

int FixedBar::resizeGetHeight(int newWidth) {
	auto newHeight = 0;

	auto buttonLeft = newWidth;
	buttonLeft -= _filter->width(); _filter->moveToLeft(buttonLeft, 0);
	_backButton->resizeToWidth(buttonLeft);
	_backButton->moveToLeft(0, 0);
	newHeight += _backButton->height();

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

Widget::Widget(QWidget *parent, gsl::not_null<Window::Controller*> controller, gsl::not_null<ChannelData*> channel) : Window::SectionWidget(parent, controller)
, _scroll(this, st::historyScroll, false)
, _fixedBar(this, channel, [this] { showFilter(); })
, _fixedBarShadow(this, st::shadowFg)
, _whatIsThis(this, lang(lng_admin_log_about).toUpper(), st::historyComposeButton) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, controller, channel, [this](int top) { _scroll->scrollToY(top); }));
	_scroll->move(0, _fixedBar->height());
	_scroll->show();

	connect(_scroll, &Ui::ScrollArea::scrolled, this, [this] { onScroll(); });
	_inner->setCancelledCallback([this] { _fixedBar->goBack(); });

	_whatIsThis->setClickedCallback([this] { Ui::show(Box<InformBox>(lang(lng_admin_log_about_text))); });
}

void Widget::showFilter() {
	if (_admins.empty()) {
		request(MTPchannels_GetParticipants(_inner->channel()->inputChannel, MTP_channelParticipantsAdmins(), MTP_int(0), MTP_int(kMaxChannelAdmins))).done([this](const MTPchannels_ChannelParticipants &result) {
			Expects(result.type() == mtpc_channels_channelParticipants);
			auto &participants = result.c_channels_channelParticipants();
			App::feedUsers(participants.vusers);
			for (auto &participant : participants.vparticipants.v) {
				auto getUserId = [&participant] {
					switch (participant.type()) {
					case mtpc_channelParticipant: return participant.c_channelParticipant().vuser_id.v;
					case mtpc_channelParticipantSelf: return participant.c_channelParticipantSelf().vuser_id.v;
					case mtpc_channelParticipantAdmin: return participant.c_channelParticipantAdmin().vuser_id.v;
					case mtpc_channelParticipantCreator: return participant.c_channelParticipantCreator().vuser_id.v;
					case mtpc_channelParticipantBanned: return participant.c_channelParticipantBanned().vuser_id.v;
					default: Unexpected("Type in AdminLog::Widget::showFilter()");
					}
				};
				if (auto user = App::userLoaded(getUserId())) {
					_admins.push_back(user);
				}
			}
			if (_admins.empty()) {
				_admins.push_back(App::self());
			}
			showFilter();
		}).send();
	} else {
		Ui::show(Box<FilterBox>(_inner->channel(), _admins, _inner->filter(), [this](FilterValue &&filter) {
			applyFilter(std::move(filter));
			Ui::hideLayer();
		}));
	}
}

void Widget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(Adaptive::OneColumn() ? 0 : st::lineWidth, _fixedBar->height());
}

gsl::not_null<ChannelData*> Widget::channel() const {
	return _inner->channel();
}

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _fixedBarShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _fixedBarShadow->show();
	return result;
}

void Widget::doSetInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(gsl::not_null<Window::SectionMemento*> memento) {
	if (auto logMemento = dynamic_cast<SectionMemento*>(memento.get())) {
		if (logMemento->getChannel() == channel()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, gsl::not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_unique<SectionMemento>(channel());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(gsl::not_null<SectionMemento*> memento) {
	memento->setScrollTop(_scroll->scrollTop());
	memento->setAdmins(std::move(_admins));
	_inner->saveState(memento);
}

void Widget::restoreState(gsl::not_null<SectionMemento*> memento) {
	_inner->restoreState(memento);
	_admins = memento->takeAdmins();
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

	Painter p(this);
	auto clip = e->rect();
	auto ms = getms();
	//_historyDownShown.step(ms);

	auto fill = QRect(0, 0, width(), App::main()->height());
	auto fromy = App::main()->backgroundFromY();
	auto x = 0, y = 0;
	auto cached = App::main()->cachedBackground(fill, x, y);
	if (cached.isNull()) {
		if (Window::Theme::Background()->tile()) {
			auto &pix = Window::Theme::Background()->pixmapForTiled();
			auto left = clip.left();
			auto top = clip.top();
			auto right = clip.left() + clip.width();
			auto bottom = clip.top() + clip.height();
			auto w = pix.width() / cRetinaFactor();
			auto h = pix.height() / cRetinaFactor();
			auto sx = qFloor(left / w);
			auto sy = qFloor((top - fromy) / h);
			auto cx = qCeil(right / w);
			auto cy = qCeil((bottom - fromy) / h);
			for (auto i = sx; i < cx; ++i) {
				for (auto j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
				}
			}
		} else {
			PainterHighQualityEnabler hq(p);

			auto &pix = Window::Theme::Background()->pixmap();
			QRect to, from;
			Window::Theme::ComputeBackgroundRects(fill, pix.size(), to, from);
			to.moveTop(to.top() + fromy);
			p.drawPixmap(to, pix, from);
		}
	} else {
		p.drawPixmap(x, fromy + y, cached);
	}
}

void Widget::onScroll() {
	int scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void Widget::showAnimatedHook() {
	_fixedBar->setAnimatingMode(true);
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
}

bool Widget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _scroll->viewportEvent(e);
}

QRect Widget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return mapToGlobal(_scroll->geometry());
}

void Widget::applyFilter(FilterValue &&value) {
	_fixedBar->applyFilter(value);
	_inner->applyFilter(std::move(value));
}

} // namespace AdminLog

