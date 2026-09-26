/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_zoom_controls.h"

#include "base/qt/qt_key_modifiers.h"
#include "iv/iv_delegate.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/tooltip.h"
#include "ui/rect.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <QtCore/QEvent>
#include <QtGui/QCursor>
#include <QtGui/QPainter>
#include <QAction>

namespace Iv {
namespace {

constexpr auto kZoomStep = int(10);
constexpr auto kZoomSmallStep = int(5);
constexpr auto kZoomTinyStep = int(1);

class ZoomMenuAction final
	: public Ui::Menu::Action
	, public Ui::AbstractTooltipShower {
public:
	ZoomMenuAction(
		not_null<Ui::Menu::Menu*> menu,
		not_null<Delegate*> delegate,
		const style::Menu &st);

	void init();

	void paintEvent(QPaintEvent *event) override;

	QString tooltipText() const override;

	QPoint tooltipPos() const override;

	bool tooltipWindowActive() const override;

private:
	const not_null<Delegate*> _delegate;
	const style::Menu &_st;
	Ui::Text::String _text;

};

ZoomMenuAction::ZoomMenuAction(
	not_null<Ui::Menu::Menu*> menu,
	not_null<Delegate*> delegate,
	const style::Menu &st)
: Ui::Menu::Action(
	menu,
	st,
	Ui::CreateChild<QAction>(menu.get()),
	nullptr,
	nullptr)
, _delegate(delegate)
, _st(st) {
	init();
}

void ZoomMenuAction::init() {
	enableMouseSelecting();

	AbstractButton::setDisabled(true);

	const auto processTooltip = [=](not_null<Ui::RpWidget*> w) {
		w->events() | rpl::on_next([=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Enter) {
				Ui::Tooltip::Show(1000, this);
			} else if (e->type() == QEvent::Leave) {
				Ui::Tooltip::Hide();
			}
		}, w->lifetime());
	};

	const auto reset = Ui::CreateChild<Ui::RoundButton>(
		this,
		rpl::single<QString>(QString()),
		st::ivResetZoom);
	processTooltip(reset);
	const auto resetLabel = Ui::CreateChild<Ui::FlatLabel>(
		reset,
		tr::lng_background_reset_default(),
		st::ivResetZoomLabel);
	resetLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	reset->setClickedCallback([this] {
		_delegate->ivSetZoom(0);
	});
	reset->show();
	const auto plus = Ui::CreateSimpleCircleButton(
		this,
		st::defaultRippleAnimationBgOver);
	plus->resize(Size(st::ivZoomButtonsSize));
	plus->paintRequest() | rpl::on_next([=, fg = _st.itemFg] {
		auto p = QPainter(plus);
		p.setPen(fg);
		p.setFont(st::normalFont);
		p.drawText(plus->rect(), QChar('+'), style::al_center);
	}, plus->lifetime());
	processTooltip(plus);
	const auto step = [] {
		return base::IsAltPressed()
			? kZoomTinyStep
			: base::IsCtrlPressed()
			? kZoomSmallStep
			: kZoomStep;
	};
	plus->setClickedCallback([this, step] {
		_delegate->ivSetZoom(_delegate->ivZoom() + step());
	});
	plus->show();
	const auto minus = Ui::CreateSimpleCircleButton(
		this,
		st::defaultRippleAnimationBgOver);
	minus->resize(Size(st::ivZoomButtonsSize));
	minus->paintRequest() | rpl::on_next([=, fg = _st.itemFg] {
		auto p = QPainter(minus);
		const auto r = minus->rect();
		p.setPen(fg);
		p.setFont(st::normalFont);
		p.drawText(
			QRectF(r).translated(0, style::ConvertFloatScale(-1)),
			QChar(0x2013),
			style::al_center);
	}, minus->lifetime());
	processTooltip(minus);
	minus->setClickedCallback([this, step] {
		_delegate->ivSetZoom(_delegate->ivZoom() - step());
	});
	minus->show();

	{
		const auto maxWidthText = u"000%"_q;
		_text.setText(_st.itemStyle, maxWidthText);
		Ui::Menu::ItemBase::setMinWidth(
			_text.maxWidth()
				+ st::ivResetZoomInnerPadding
				+ resetLabel->width()
				+ plus->width()
				+ minus->width()
				+ _st.itemPadding.right() * 2);
	}

	_delegate->ivZoomValue(
	) | rpl::on_next([this](int value) {
		_text.setText(_st.itemStyle, QString::number(value) + '%');
		update();
	}, lifetime());

	rpl::combine(
		sizeValue(),
		reset->sizeValue()
	) | rpl::on_next([=](const QSize &size, const QSize &) {
		reset->setFullWidth(0
			+ resetLabel->width()
			+ st::ivResetZoomInnerPadding);
		resetLabel->moveToLeft(
			(reset->width() - resetLabel->width()) / 2,
			(reset->height() - resetLabel->height()) / 2);
		reset->moveToRight(
			_st.itemPadding.right(),
			(size.height() - reset->height()) / 2);
		plus->moveToRight(
			_st.itemPadding.right() + reset->width(),
			(size.height() - plus->height()) / 2);
		minus->moveToRight(
			_st.itemPadding.right() + plus->width() + reset->width(),
			(size.height() - minus->height()) / 2);
	}, lifetime());
}

void ZoomMenuAction::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	p.setPen(_st.itemFg);
	_text.draw(p, {
		.position = QPoint(
			_st.itemIconPosition.x(),
			(height() - _text.minHeight()) / 2),
		.outerWidth = width(),
		.availableWidth = width(),
	});
}

QString ZoomMenuAction::tooltipText() const {
#ifdef Q_OS_MAC
	return tr::lng_iv_zoom_tooltip_cmd(tr::now);
#else
	return tr::lng_iv_zoom_tooltip_ctrl(tr::now);
#endif
}

QPoint ZoomMenuAction::tooltipPos() const {
	return QCursor::pos();
}

bool ZoomMenuAction::tooltipWindowActive() const {
	return true;
}

} // namespace

base::unique_qptr<Ui::Menu::ItemBase> CreateZoomMenuAction(
		not_null<Ui::Menu::Menu*> menu,
		not_null<Delegate*> delegate) {
	return base::make_unique_q<ZoomMenuAction>(menu, delegate, menu->st());
}

} // namespace Iv
