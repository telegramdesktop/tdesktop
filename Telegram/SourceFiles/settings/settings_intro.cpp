/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_intro.h"

#include "settings/settings_advanced.h"
#include "settings/settings_main.h"
#include "settings/settings_chat.h"
#include "settings/settings_codes.h"
#include "ui/basic_click_handlers.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/cached_round_corners.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "boxes/abstract_box.h"
#include "window/window_controller.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Settings {
namespace {

class TopBar : public Ui::RpWidget {
public:
	TopBar(QWidget *parent, const style::InfoTopBar &st);

	void setTitle(rpl::producer<QString> &&title);

	template <typename ButtonWidget>
	ButtonWidget *addButton(base::unique_qptr<ButtonWidget> button) {
		auto result = button.get();
		pushButton(std::move(button));
		return result;
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateControlsGeometry(int newWidth);
	Ui::RpWidget *pushButton(base::unique_qptr<Ui::RpWidget> button);

	const style::InfoTopBar &_st;
	std::vector<base::unique_qptr<Ui::RpWidget>> _buttons;
	QPointer<Ui::FlatLabel> _title;

};

object_ptr<Ui::RpWidget> CreateIntroSettings(
		QWidget *parent,
		not_null<Window::Controller*> window) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);

	Ui::AddDivider(result);
	Ui::AddSkip(result);
	SetupLanguageButton(window, result);
	SetupConnectionType(window, &window->account(), result);
	Ui::AddSkip(result);
	if (HasUpdate()) {
		Ui::AddDivider(result);
		Ui::AddSkip(result);
		SetupUpdate(result);
		Ui::AddSkip(result);
	}
	{
		auto wrap = object_ptr<Ui::VerticalLayout>(result);
		SetupSystemIntegrationContent(
			window->sessionController(),
			wrap.data());
		SetupWindowTitleContent(
			window->sessionController(),
			wrap.data());
		if (wrap->count() > 0) {
			Ui::AddDivider(result);
			Ui::AddSkip(result);
			result->add(object_ptr<Ui::OverrideMargins>(
				result,
				std::move(wrap)));
			Ui::AddSkip(result);
		}
	}
	Ui::AddDivider(result);
	Ui::AddSkip(result);
	SetupInterfaceScale(window, result, false);
	SetupDefaultThemes(window, result);
	Ui::AddSkip(result);

	if (anim::Disabled()) {
		Ui::AddDivider(result);
		Ui::AddSkip(result);
		SetupAnimations(window, result);
		Ui::AddSkip(result);
	}

	Ui::AddDivider(result);
	Ui::AddSkip(result);

	AddButtonWithIcon(
		result,
		tr::lng_settings_faq(),
		st::settingsButtonNoIcon
	)->addClickHandler([] {
		OpenFaq(nullptr);
	});

	return result;
}

TopBar::TopBar(QWidget *parent, const style::InfoTopBar &st)
: RpWidget(parent)
, _st(st) {
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	if (_title) {
		delete _title;
	}
	_title = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(title),
		_st.title);
	updateControlsGeometry(width());
}

Ui::RpWidget *TopBar::pushButton(base::unique_qptr<Ui::RpWidget> button) {
	auto wrapped = std::move(button);
	auto weak = wrapped.get();
	_buttons.push_back(std::move(wrapped));
	weak->widthValue(
	) | rpl::start_with_next([this] {
		updateControlsGeometry(width());
	}, lifetime());
	return weak;
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) continue;
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_title) {
		_title->moveToLeft(
			_st.titlePosition.x(),
			_st.titlePosition.y(),
			newWidth);
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	const auto radius = st::boxRadius;
	QPainter(this).fillRect(
		e->rect().intersected({ 0, radius, width(), height() - radius }),
		_st.bg);
}

} // namespace

class IntroWidget : public Ui::RpWidget {
public:
	IntroWidget(
		QWidget *parent,
		not_null<Window::Controller*> window);

	void forceContentRepaint();

	rpl::producer<int> desiredHeightValue() const override;

	void updateGeometry(QRect newGeometry, int additionalScroll);
	int scrollTillBottom(int forHeight) const;
	rpl::producer<int> scrollTillBottomChanges() const;

	void setInnerFocus();

	~IntroWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void updateControlsGeometry();
	QRect contentGeometry() const;
	void setInnerWidget(object_ptr<Ui::RpWidget> content);
	void showContent(not_null<Window::Controller*> window);
	rpl::producer<bool> topShadowToggledValue() const;
	void createTopBar(not_null<Window::Controller*> window);
	void applyAdditionalScroll(int additionalScroll);

	rpl::variable<int> _scrollTopSkip = -1;
	rpl::event_stream<int> _scrollTillBottomChanges;
	object_ptr<Ui::RpWidget> _wrap;
	not_null<Ui::ScrollArea*> _scroll;
	Ui::PaddingWrap<Ui::RpWidget> *_innerWrap = nullptr;
	int _innerDesiredHeight = 0;

	int _additionalScroll = 0;
	object_ptr<TopBar> _topBar = { nullptr };

	object_ptr<Ui::FadeShadow> _topShadow;

};

IntroWidget::IntroWidget(
	QWidget *parent,
	not_null<Window::Controller*> window)
: RpWidget(parent)
, _wrap(this)
, _scroll(Ui::CreateChild<Ui::ScrollArea>(_wrap.data()))
, _topShadow(this) {
	_wrap->setAttribute(Qt::WA_OpaquePaintEvent);
	_wrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_wrap.data());
		p.fillRect(clip, st::boxBg);
	}, _wrap->lifetime());

	_scrollTopSkip.changes(
	) | rpl::start_with_next([this] {
		updateControlsGeometry();
	}, lifetime());

	createTopBar(window);
	showContent(window);
	_topShadow->toggleOn(
		topShadowToggledValue(
		) | rpl::filter([](bool shown) {
			return true;
		}));
}

void IntroWidget::updateControlsGeometry() {
	if (!_innerWrap) {
		return;
	}

	_topBar->resizeToWidth(width());
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, _topBar->height());
	_wrap->setGeometry(contentGeometry());

	auto scrollGeometry = _wrap->rect().marginsRemoved(
		QMargins(0, _scrollTopSkip.current(), 0, 0));
	if (_scroll->geometry() != scrollGeometry) {
		_scroll->setGeometry(scrollGeometry);
		_innerWrap->resizeToWidth(_scroll->width());
	}

	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_innerWrap->setVisibleTopBottom(
			scrollTop,
			scrollTop + _scroll->height());
	}
}

void IntroWidget::forceContentRepaint() {
	// WA_OpaquePaintEvent on TopBar creates render glitches when
	// animating the LayerWidget's height :( Fixing by repainting.

	if (_topBar) {
		_topBar->update();
	}
	_scroll->update();
	if (_innerWrap) {
		_innerWrap->update();
	}
}

void IntroWidget::createTopBar(not_null<Window::Controller*> window) {
	_topBar.create(this, st::infoLayerTopBar);
	_topBar->setTitle(tr::lng_menu_settings());
	auto close = _topBar->addButton(
		base::make_unique_q<Ui::IconButton>(
			_topBar,
			st::infoLayerTopBarClose));
	close->addClickHandler([=] {
		window->hideSettingsAndLayer();
	});

	_topBar->lower();
	_topBar->resizeToWidth(width());
	_topBar->show();
}

void IntroWidget::setInnerWidget(object_ptr<Ui::RpWidget> content) {
	_innerWrap = _scroll->setOwnedWidget(
		object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
			this,
			std::move(content),
			_innerWrap ? _innerWrap->padding() : style::margins()));
	_innerWrap->move(0, 0);

	// MSVC BUG + REGRESSION rpl::mappers::tuple :(
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_innerWrap->entity()->desiredHeightValue()
	) | rpl::start_with_next([this](
			int top,
			int height,
			int desired) {
		const auto bottom = top + height;
		_innerDesiredHeight = desired;
		_innerWrap->setVisibleTopBottom(top, bottom);
		_scrollTillBottomChanges.fire_copy(std::max(desired - bottom, 0));
	}, _innerWrap->lifetime());
}

rpl::producer<bool> IntroWidget::topShadowToggledValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollTopSkip.value()
	) | rpl::map((_1 > 0) || (_2 > 0));
}

void IntroWidget::showContent(not_null<Window::Controller*> window) {
	setInnerWidget(CreateIntroSettings(_scroll, window));

	_additionalScroll = 0;
	updateControlsGeometry();
	_topShadow->raise();
	_topShadow->finishAnimating();
}

void IntroWidget::setInnerFocus() {
	setFocus();
}

rpl::producer<int> IntroWidget::desiredHeightValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_topBar->heightValue(),
		_innerWrap->entity()->desiredHeightValue(),
		_scrollTopSkip.value()
	) | rpl::map(_1 + _2 + _3);
}

QRect IntroWidget::contentGeometry() const {
	return rect().marginsRemoved({ 0, _topBar->height(), 0, 0 });
}

void IntroWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void IntroWidget::keyPressEvent(QKeyEvent *e) {
	crl::on_main(this, [text = e->text()]{
		CodesFeedString(nullptr, text);
	});
	return RpWidget::keyPressEvent(e);
}

void IntroWidget::applyAdditionalScroll(int additionalScroll) {
	if (_innerWrap) {
		_innerWrap->setPadding({ 0, 0, 0, additionalScroll });
	}
}

void IntroWidget::updateGeometry(QRect newGeometry, int additionalScroll) {
	auto scrollChanged = (_additionalScroll != additionalScroll);
	auto geometryChanged = (geometry() != newGeometry);
	auto shrinkingContent = (additionalScroll < _additionalScroll);
	_additionalScroll = additionalScroll;

	if (geometryChanged) {
		if (shrinkingContent) {
			setGeometry(newGeometry);
		}
		if (scrollChanged) {
			applyAdditionalScroll(additionalScroll);
		}
		if (!shrinkingContent) {
			setGeometry(newGeometry);
		}
	} else if (scrollChanged) {
		applyAdditionalScroll(additionalScroll);
	}
}

int IntroWidget::scrollTillBottom(int forHeight) const {
	auto scrollHeight = forHeight
		- _scrollTopSkip.current()
		- _topBar->height();
	auto scrollBottom = _scroll->scrollTop() + scrollHeight;
	auto desired = _innerDesiredHeight;
	return std::max(desired - scrollBottom, 0);
}

rpl::producer<int> IntroWidget::scrollTillBottomChanges() const {
	return _scrollTillBottomChanges.events();
}

IntroWidget::~IntroWidget() = default;

LayerWidget::LayerWidget(QWidget*, not_null<Window::Controller*> window)
: _content(this, window) {
	setupHeightConsumers();
}

void LayerWidget::setupHeightConsumers() {
	_content->scrollTillBottomChanges(
	) | rpl::filter([this] {
		return !_inResize;
	}) | rpl::start_with_next([this] {
		resizeToWidth(width());
	}, lifetime());
	_content->desiredHeightValue(
	) | rpl::start_with_next([this](int height) {
		accumulate_max(_desiredHeight, height);
		if (_content && !_inResize) {
			resizeToWidth(width());
		}
	}, lifetime());
}

void LayerWidget::showFinished() {
}

void LayerWidget::parentResized() {
	const auto parentSize = parentWidget()->size();
	const auto parentWidth = parentSize.width();
	const auto newWidth = (parentWidth < MinimalSupportedWidth())
		? parentWidth
		: qMin(
			parentWidth - 2 * st::infoMinimalLayerMargin,
			st::infoDesiredWidth);
	resizeToWidth(newWidth);
}

int LayerWidget::MinimalSupportedWidth() {
	auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_content) {
		return 0;
	}
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto newLeft = (windowWidth - newWidth) / 2;
	if (!newLeft) {
		_content->updateGeometry({ 0, 0, windowWidth, windowHeight }, 0);
		auto newGeometry = QRect(0, 0, windowWidth, windowHeight);
		if (newGeometry != geometry()) {
			_content->forceContentRepaint();
		}
		if (newGeometry.topLeft() != geometry().topLeft()) {
			move(newGeometry.topLeft());
		}
		_tillTop = _tillBottom = true;
		return windowHeight;
	}
	auto newTop = std::clamp(
		windowHeight / 24,
		st::infoLayerTopMinimal,
		st::infoLayerTopMaximal);
	auto newBottom = newTop;
	auto desiredHeight = _desiredHeight + st::boxRadius;
	accumulate_min(desiredHeight, windowHeight - newTop - newBottom);

	// First resize content to new width and get the new desired height.
	auto contentLeft = 0;
	auto contentTop = 0;
	auto contentBottom = st::boxRadius;
	auto contentWidth = newWidth;
	auto contentHeight = desiredHeight - contentTop - contentBottom;
	auto scrollTillBottom = _content->scrollTillBottom(contentHeight);
	auto additionalScroll = std::min(scrollTillBottom, newBottom);

	desiredHeight += additionalScroll;
	contentHeight += additionalScroll;
	_tillTop = false;
	_tillBottom = (newTop + desiredHeight >= windowHeight);
	if (_tillBottom) {
		contentHeight += contentBottom;
		additionalScroll += contentBottom;
	}
	_content->updateGeometry({
		contentLeft,
		contentTop,
		contentWidth,
		contentHeight }, additionalScroll);

	auto newGeometry = QRect(newLeft, newTop, newWidth, desiredHeight);
	if (newGeometry != geometry()) {
		_content->forceContentRepaint();
	}
	if (newGeometry.topLeft() != geometry().topLeft()) {
		move(newGeometry.topLeft());
	}

	return desiredHeight;
}

void LayerWidget::doSetInnerFocus() {
	_content->setInnerFocus();
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto clip = e->rect();
	auto r = st::boxRadius;
	const auto &pixmaps = Ui::CachedCornerPixmaps(Ui::BoxCorners);
	if (!_tillTop && clip.intersects({ 0, 0, width(), r })) {
		Ui::FillRoundRect(p, 0, 0, width(), r, st::boxBg, {
			.p = { pixmaps.p[0], pixmaps.p[1], QPixmap(), QPixmap() },
		});
	}
	if (!_tillBottom && clip.intersects({ 0, height() - r, width(), r })) {
		Ui::FillRoundRect(p, 0, height() - r, width(), r, st::boxBg, {
			.p = { QPixmap(), QPixmap(), pixmaps.p[2], pixmaps.p[3] },
		});
	}
	if (_tillTop) {
		p.fillRect(0, 0, width(), r, st::boxBg);
	}
}

} // namespace Settings
