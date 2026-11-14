/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "base/timer.h"
#include "lottie/lottie_icon.h"
#include "menu/menu_send_details.h"
#include "settings/settings_key_navigation.h"
#include "ui/effects/animations.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_entity.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_info.h"
#include "mokagram/settings/mokagram_settings_box.h"
#include "ui/text/text_custom_emoji.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QAction>

namespace Settings {
namespace {

[[nodiscard]] HighlightArgs MaybeFillFromRipple(
		not_null<QWidget*> target,
		HighlightArgs args) {
	if (!args.rippleShape) {
		return args;
	}
	const auto widget = target.get();
	if (const auto icon = dynamic_cast<Ui::IconButton*>(widget)) {
		const auto &st = icon->st();
		args.shape = HighlightShape::Ellipse;
		args.margin = QMargins(
			st.rippleAreaPosition.x(),
			st.rippleAreaPosition.y(),
			st.width - st.rippleAreaPosition.x() - st.rippleAreaSize,
			st.height - st.rippleAreaPosition.y() - st.rippleAreaSize);
	} else if (const auto round = dynamic_cast<Ui::RoundButton*>(widget)) {
		const auto &st = round->st();
		args.shape = HighlightShape::Rect;
		args.radius = st.radius ? st.radius : st::buttonRadius;
	}
	return args;
}

class HighlightOverlay final : public Ui::RpWidget {
public:
	HighlightOverlay(not_null<QWidget*> target, HighlightArgs &&args);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	enum class Phase {
		Wait,
		FadeIn,
		Shown,
		FadeOut,
	};

	void updateGeometryFromTarget();
	void updateZOrder();
	void startFadeIn();
	void startFadeOut();
	void nextPhase();
	void nextPhaseIn(crl::time delay);
	void finish();

	const QPointer<QWidget> _target;
	const HighlightArgs _args;
	const style::color &_color;

	Ui::Animations::Simple _animation;
	base::Timer _phaseTimer;
	Phase _phase = Phase::Wait;
	bool _finishing = false;

};

HighlightOverlay::HighlightOverlay(
	not_null<QWidget*> target,
	HighlightArgs &&args)
: RpWidget(target->parentWidget())
, _target(target.get())
, _args(MaybeFillFromRipple(target, std::move(args)))
, _color(_args.color ? *_args.color : st::windowBgActive)
, _phaseTimer([=] { nextPhase(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	target->installEventFilter(this);

	QObject::connect(
		target.get(),
		&QObject::destroyed,
		this,
		[this] { finish(); });

	updateGeometryFromTarget();
	updateZOrder();

	paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(this);

		const auto progress = _animation.value(
			(_phase == Phase::Wait || _phase == Phase::FadeOut) ? 0. : 1.);
		const auto alpha = _args.opacity * progress;

		auto color = _color->c;
		color.setAlphaF(color.alphaF() * alpha);

		p.setPen(Qt::NoPen);
		p.setBrush(color);

		const auto r = rect();
		switch (_args.shape) {
		case HighlightShape::Rect:
			if (_args.radius > 0) {
				PainterHighQualityEnabler hq(p);
				p.drawRoundedRect(r, _args.radius, _args.radius);
			} else {
				p.drawRect(r);
			}
			break;
		case HighlightShape::Ellipse:
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(r);
			break;
		}
	}, lifetime());

	hide();
	nextPhaseIn(_args.showDelay);
}

bool HighlightOverlay::eventFilter(QObject *o, QEvent *e) {
	if (o != _target.data()) {
		return false;
	}
	switch (e->type()) {
	case QEvent::Move:
	case QEvent::Resize:
		updateGeometryFromTarget();
		break;
	case QEvent::ZOrderChange:
		updateZOrder();
		break;
	case QEvent::Show:
	case QEvent::Hide:
		setVisible(!_target->isHidden());
		break;
	default:
		break;
	}
	return false;
}

void HighlightOverlay::updateGeometryFromTarget() {
	if (_target) {
		setGeometry(_target->geometry().marginsRemoved(_args.margin));
	}
}

void HighlightOverlay::updateZOrder() {
	if (!_target) {
		return;
	}
	if (_args.below) {
		stackUnder(_target);
	} else {
		const auto parent = _target->parentWidget();
		const auto siblings = parent ? parent->children() : QObjectList();
		const auto it = ranges::find(siblings, _target.data());
		const auto next = (it != siblings.end()) ? (it + 1) : it;
		if (next != siblings.end()) {
			if (const auto widget = qobject_cast<QWidget*>(*next)) {
				stackUnder(widget);
				return;
			}
		}
		raise();
	}
}

void HighlightOverlay::startFadeIn() {
	show();

	_phase = Phase::FadeIn;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			_phase = Phase::Shown;
			nextPhaseIn(_args.shownDuration);
		}
	}, 0., 1., _args.showDuration, anim::easeOutCubic);
}

void HighlightOverlay::startFadeOut() {
	_phase = Phase::FadeOut;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			finish();
		}
	}, 1., 0., _args.hideDuration, anim::easeInCubic);
}

void HighlightOverlay::nextPhase() {
	switch (_phase) {
	case Phase::Wait:
		startFadeIn();
		break;
	case Phase::Shown:
		startFadeOut();
		break;
	default:
		Unexpected("Next phase called during Fade phase.");
	}
}

void HighlightOverlay::nextPhaseIn(crl::time delay) {
	if (!delay) {
		_phaseTimer.cancel();
		nextPhase();
	} else {
		_phaseTimer.callOnce(delay);
	}
}

void HighlightOverlay::finish() {
	if (_finishing) {
		return;
	}
	_finishing = true;
	_phaseTimer.cancel();
	_animation.stop();
	deleteLater();
}

} // namespace

void HighlightWidget(QWidget *target, HighlightArgs &&args) {
	if (!target) {
		return;
	}
	if (args.scroll) {
		ScrollToWidget(target);
	}
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto section = dynamic_cast<AbstractSection*>(parent)) {
			section->setNavigationAnchor(target);
			break;
		}
	}
	new HighlightOverlay(target, std::move(args));
}

void ScrollToWidget(not_null<QWidget*> target) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPosition = target->mapToGlobal(QPoint(0, 0));
			const auto localPosition = inner->mapFromGlobal(globalPosition);
			const auto localTop = localPosition.y();
			const auto targetHeight = target->height();
			const auto scrollHeight = scroll->height();
			const auto centered = localTop - (scrollHeight - targetHeight) / 2;
			const auto top = std::clamp(centered, 0, scroll->scrollTopMax());
			scroll->scrollToY(top);
		}
	};
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

void RevealWidget(not_null<QWidget*> target, int margin) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPosition = target->mapToGlobal(QPoint(0, 0));
			const auto localTop = inner->mapFromGlobal(globalPosition).y();
			scroll->scrollToY(
				localTop - margin,
				localTop + target->height() + margin);
		}
	};
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

HighlightArgs SubsectionTitleHighlight() {
	const auto radius = st::roundRadiusSmall;
	return { .margin = { -radius, 0, -radius, 0 }, .radius = radius };
}

AbstractSection::AbstractSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

AbstractSection::~AbstractSection() = default;

SendMenu::Details AbstractSection::sendMenuDetails() const {
	return {};
}

bool AbstractSection::processChosenSticker(ChatHelpers::FileChosen &&) {
	return false;
}

void AbstractSection::build(
		not_null<Ui::VerticalLayout*> container,
		SectionBuildMethod method) {
	method(
		container,
		_controller,
		showOtherMethod(),
		_showFinished.events());
}

void AbstractSection::keyPressEvent(QKeyEvent *e) {
	if (!_keyNavigation) {
		_keyNavigation = std::make_unique<KeyNavigation>(this);
	}
	if (!_keyNavigation->handle(e)) {
		RpWidget::keyPressEvent(e);
	}
}

void AbstractSection::setNavigationAnchor(not_null<QWidget*> widget) {
	if (!_keyNavigation) {
		_keyNavigation = std::make_unique<KeyNavigation>(this);
	}
	_keyNavigation->anchorTo(widget);
}

Icon::Icon(IconDescriptor descriptor) : _icon(descriptor.icon) {
	const auto background = [&]() -> const style::color* {
		if (descriptor.type == IconType::Simple) {
			return nullptr;
		}
		return descriptor.background;
	}();
	if (background) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_background.emplace(radius, *background);
	} else if (const auto brush = descriptor.backgroundBrush) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_backgroundBrush.emplace(radius, std::move(*brush));
	}
}

void Icon::paint(QPainter &p, QPoint position) const {
	paint(p, position.x(), position.y());
}

void Icon::paint(QPainter &p, int x, int y) const {
	if (_background) {
		_background->paint(p, { { x, y }, _icon->size() });
	} else if (_backgroundBrush) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_backgroundBrush->second);
		p.drawRoundedRect(
			QRect(QPoint(x, y), _icon->size()),
			_backgroundBrush->first,
			_backgroundBrush->first);
	}
	_icon->paint(p, { x, y }, 2 * x + _icon->width());
}

int Icon::width() const {
	return _icon->width();
}

int Icon::height() const {
	return _icon->height();
}

QSize Icon::size() const {
	return _icon->size();
}

// Mokagram: логотип (контур 500x500, с границами примерно 90..415) для значка «Настройки mokaGram».
[[nodiscard]] const QPainterPath &MokaLogoPath() {
	static const auto result = [] {
		auto path = QPainterPath();
		const auto data = QString::fromLatin1(R"SVG(M375.0,89.5 C376.6,89.5 382.8,89.8 384.5,90.0 C386.1,90.1 384.0,90.2 385.0,90.5 C385.9,90.7 387.6,90.5 390.0,91.5 C392.3,92.5 396.5,94.6 399.0,96.5 C401.5,98.3 403.1,100.0 405.0,102.5 C406.8,105.0 409.0,109.1 410.0,111.5 C411.0,113.8 410.7,115.5 411.0,116.5 C411.2,117.4 411.4,116.3 411.5,117.0 C411.5,117.6 411.4,119.8 411.5,120.5 C411.5,121.1 412.0,119.3 412.0,121.0 C412.0,122.6 411.6,128.8 411.5,130.5 C411.3,132.1 411.3,129.5 411.0,131.0 C410.6,132.5 410.1,136.8 409.5,139.5 C408.8,142.1 407.8,144.5 407.0,147.0 C406.1,149.5 404.9,153.0 404.5,154.5 C404.0,156.0 404.8,154.9 404.5,156.0 C404.1,157.0 402.8,159.9 402.5,161.0 C402.1,162.0 402.8,161.4 402.5,162.5 C402.1,163.5 400.8,166.4 400.5,167.5 C400.1,168.5 400.9,167.6 400.5,169.0 C400.0,170.3 398.4,174.1 398.0,175.5 C397.5,176.8 398.3,175.9 398.0,177.0 C397.6,178.0 396.3,180.9 396.0,182.0 C395.6,183.0 396.3,182.0 396.0,183.5 C395.6,184.9 394.4,188.1 393.6,190.5 C392.8,192.8 392.1,195.1 391.3,197.5 C390.5,199.8 389.3,203.0 389.0,204.5 C388.6,205.9 389.5,204.1 389.0,206.0 C388.4,207.8 386.8,212.5 385.7,215.7 C384.6,219.0 383.0,223.6 382.5,225.5 C381.9,227.3 382.8,225.9 382.5,227.0 C382.1,228.0 380.8,230.9 380.5,232.0 C380.1,233.0 380.9,232.1 380.5,233.5 C380.0,234.8 378.4,238.6 378.0,240.0 C377.5,241.3 378.3,240.4 378.0,241.5 C377.6,242.5 376.3,245.4 376.0,246.5 C375.6,247.5 376.4,246.6 376.0,248.0 C375.5,249.3 373.9,253.1 373.5,254.5 C373.0,255.8 373.8,254.9 373.5,256.0 C373.1,257.0 371.8,259.9 371.5,261.0 C371.1,262.0 371.9,261.1 371.5,262.5 C371.0,263.8 369.4,267.6 369.0,269.0 C368.5,270.3 369.5,268.6 369.0,270.5 C368.4,272.3 366.8,277.0 365.7,280.2 C364.6,283.5 363.0,288.1 362.5,290.0 C361.9,291.8 362.8,290.4 362.5,291.5 C362.1,292.5 360.8,295.4 360.5,296.5 C360.1,297.5 360.9,296.6 360.5,298.0 C360.0,299.3 358.4,303.1 358.0,304.5 C357.5,305.8 358.3,304.9 358.0,306.0 C357.6,307.0 356.3,309.9 356.0,311.0 C355.6,312.0 356.4,311.1 356.0,312.5 C355.5,313.8 353.9,317.6 353.5,319.0 C353.0,320.3 354.0,318.6 353.5,320.5 C352.9,322.3 351.3,327.0 350.2,330.2 C349.1,333.5 347.5,338.1 347.0,340.0 C346.4,341.8 347.3,340.4 347.0,341.5 C346.6,342.5 345.3,345.4 345.0,346.5 C344.6,347.5 345.4,346.6 345.0,348.0 C344.5,349.3 342.9,353.1 342.5,354.5 C342.0,355.8 342.8,354.9 342.5,356.0 C342.1,357.0 340.8,359.9 340.5,361.0 C340.1,362.0 340.9,360.9 340.5,362.5 C340.0,364.0 338.6,367.6 337.7,370.2 C336.8,372.8 335.8,375.8 335.0,378.0 C334.1,380.1 333.8,380.9 332.5,383.0 C331.1,385.0 328.5,388.6 327.0,390.5 C325.4,392.3 324.4,392.9 323.0,394.0 C321.5,395.0 320.1,396.0 318.5,397.0 C316.8,397.9 315.1,398.7 313.0,399.5 C310.8,400.2 308.1,401.0 305.7,401.7 C303.3,402.5 301.7,402.7 298.5,404.0 C295.2,405.2 289.2,408.0 286.5,409.0 C283.7,410.0 283.0,409.6 282.0,410.0 C280.9,410.3 281.0,410.7 280.0,411.0 C278.9,411.2 276.3,411.3 275.5,411.5 C274.6,411.6 275.6,411.9 275.0,412.0 C274.3,412.0 272.1,411.9 271.5,412.0 C270.8,412.0 272.6,412.4 271.0,412.5 C269.3,412.5 263.1,412.5 261.5,412.5 C259.8,412.4 262.0,412.1 261.0,412.0 C259.9,411.8 257.5,412.0 255.0,411.5 C252.4,410.9 248.2,409.5 245.5,408.5 C242.7,407.4 240.6,406.2 238.5,405.0 C236.3,403.7 234.0,402.0 232.5,401.0 C231.0,399.9 231.1,400.1 229.5,398.5 C227.8,396.8 224.3,393.2 222.5,391.0 C220.6,388.7 219.6,387.0 218.5,385.0 C217.3,383.0 216.5,381.6 215.5,379.0 C214.4,376.3 212.7,371.4 212.0,369.0 C211.2,366.5 211.5,366.0 211.0,364.5 C210.4,362.9 209.4,360.9 208.5,359.5 C207.5,358.0 206.4,356.9 205.5,356.0 C204.5,355.0 204.0,354.6 203.0,354.0 C201.9,353.3 200.8,352.7 199.0,352.0 C197.1,351.2 194.1,350.5 191.7,349.7 C189.3,349.0 186.8,348.4 184.5,347.5 C182.1,346.5 179.6,345.2 177.5,344.0 C175.3,342.7 173.7,341.8 171.5,340.0 C169.2,338.1 166.3,335.6 164.0,333.0 C161.6,330.3 159.1,326.6 157.5,324.0 C155.8,321.3 154.9,319.3 154.0,317.0 C153.0,314.6 152.5,312.1 151.7,309.7 C151.0,307.3 150.2,304.3 149.5,302.5 C148.7,300.6 148.4,299.8 147.5,298.5 C146.5,297.1 145.7,295.8 144.0,294.5 C142.2,293.1 140.1,291.7 137.0,290.5 C133.8,289.2 128.4,288.2 125.0,287.0 C121.5,285.7 119.4,284.7 116.5,283.0 C113.5,281.2 109.7,278.3 107.5,276.5 C105.2,274.6 104.7,274.0 103.0,272.0 C101.2,269.9 98.6,266.6 97.0,264.0 C95.3,261.3 94.1,258.9 93.0,256.0 C91.8,253.0 90.5,249.1 90.0,246.5 C89.4,243.8 89.6,241.1 89.5,240.0 C89.3,238.8 89.0,241.0 89.0,239.5 C88.9,237.9 88.9,232.0 89.0,230.5 C89.0,228.9 89.4,230.6 89.5,230.0 C89.5,229.3 89.4,227.1 89.5,226.5 C89.5,225.8 89.9,226.4 90.0,226.0 C90.0,225.5 89.9,224.4 90.0,224.0 C90.0,223.5 90.4,223.9 90.5,223.5 C90.5,223.0 90.3,222.1 90.5,221.5 C90.6,220.8 91.3,220.0 91.5,219.5 C91.6,218.9 90.9,219.8 91.5,218.0 C92.0,216.1 94.2,210.3 95.0,208.5 C95.7,206.6 95.7,207.6 96.0,207.0 C96.2,206.3 96.2,205.1 96.5,204.5 C96.7,203.8 96.8,204.6 97.5,203.0 C98.1,201.3 99.3,197.3 100.2,194.5 C101.1,191.6 101.7,188.6 103.0,186.0 C104.2,183.3 105.6,180.8 107.5,178.5 C109.3,176.1 112.1,173.5 114.0,172.0 C115.8,170.4 116.0,170.2 118.5,169.0 C120.9,167.7 125.3,165.7 128.5,164.5 C131.6,163.2 135.7,162.0 137.5,161.5 C139.2,161.0 137.6,161.9 139.0,161.5 C140.3,161.0 144.1,159.4 145.5,159.0 C146.8,158.5 145.9,159.3 147.0,159.0 C148.0,158.6 150.9,157.3 152.0,157.0 C153.0,156.6 152.1,157.4 153.5,157.0 C154.8,156.5 158.6,154.9 160.0,154.5 C161.3,154.0 160.4,154.8 161.5,154.5 C162.5,154.1 165.4,152.8 166.5,152.5 C167.5,152.1 166.6,152.9 168.0,152.5 C169.3,152.0 173.1,150.4 174.5,150.0 C175.8,149.5 174.9,150.3 176.0,150.0 C177.0,149.6 179.9,148.3 181.0,148.0 C182.0,147.6 181.1,148.4 182.5,148.0 C183.8,147.5 187.6,145.9 189.0,145.5 C190.3,145.0 189.4,145.8 190.5,145.5 C191.5,145.1 194.4,143.8 195.5,143.5 C196.5,143.1 195.9,143.8 197.0,143.5 C198.0,143.1 200.9,141.8 202.0,141.5 C203.0,141.1 201.0,142.2 203.5,141.5 C205.9,140.7 214.0,137.7 216.5,137.0 C218.9,136.2 216.6,137.4 218.0,137.0 C219.3,136.5 223.1,134.9 224.5,134.5 C225.8,134.0 224.9,134.8 226.0,134.5 C227.0,134.1 229.9,132.8 231.0,132.5 C232.0,132.1 231.1,132.9 232.5,132.5 C233.8,132.0 237.6,130.4 239.0,130.0 C240.3,129.5 239.4,130.3 240.5,130.0 C241.5,129.6 244.4,128.3 245.5,128.0 C246.5,127.6 245.6,128.4 247.0,128.0 C248.3,127.5 252.1,125.9 253.5,125.5 C254.8,125.0 253.9,125.8 255.0,125.5 C256.0,125.1 258.9,123.8 260.0,123.5 C261.0,123.1 260.4,123.8 261.5,123.5 C262.5,123.1 265.4,121.8 266.5,121.5 C267.5,121.1 266.6,121.9 268.0,121.5 C269.3,121.0 273.1,119.4 274.5,119.0 C275.8,118.5 274.9,119.3 276.0,119.0 C277.0,118.6 279.9,117.3 281.0,117.0 C282.0,116.6 281.1,117.4 282.5,117.0 C283.8,116.5 287.6,114.9 289.0,114.5 C290.3,114.0 289.4,114.8 290.5,114.5 C291.5,114.1 294.4,112.8 295.5,112.5 C296.5,112.1 295.6,112.9 297.0,112.5 C298.3,112.0 302.1,110.4 303.5,110.0 C304.8,109.5 303.9,110.3 305.0,110.0 C306.0,109.6 308.9,108.3 310.0,108.0 C311.0,107.6 310.4,108.3 311.5,108.0 C312.5,107.6 315.4,106.3 316.5,106.0 C317.5,105.6 316.6,106.4 318.0,106.0 C319.3,105.5 323.1,103.9 324.5,103.5 C325.8,103.0 324.9,103.8 326.0,103.5 C327.0,103.1 329.9,101.8 331.0,101.5 C332.0,101.1 331.1,101.9 332.5,101.5 C333.8,101.0 337.6,99.4 339.0,99.0 C340.3,98.5 339.4,99.3 340.5,99.0 C341.5,98.6 344.4,97.3 345.5,97.0 C346.5,96.6 345.9,97.3 347.0,97.0 C348.0,96.6 350.9,95.3 352.0,95.0 C353.0,94.6 352.4,95.3 353.5,95.0 C354.5,94.6 357.4,93.3 358.5,93.0 C359.5,92.6 358.7,93.3 360.0,93.0 C361.2,92.6 364.6,91.3 366.0,91.0 C367.3,90.6 367.1,91.1 368.0,91.0 C368.8,90.8 369.9,90.1 371.0,90.0 C372.0,89.8 373.8,90.0 374.5,90.0 C375.1,89.9 373.3,89.5 375.0,89.5 Z)SVG");
		const auto parts = data.split(' ', Qt::SkipEmptyParts);
		const auto pt = [](QString value) {
			if (value.size() > 1 && (value[0] == 'M' || value[0] == 'C')) {
				value = value.mid(1);
			}
			const auto xy = value.split(',');
			return QPointF(xy[0].toDouble(), xy[1].toDouble());
		};
		for (auto i = 0; i < parts.size();) {
			if (parts[i][0] == 'M') {
				path.moveTo(pt(parts[i]));
				i += 1;
			} else if (parts[i][0] == 'C') {
				path.cubicTo(pt(parts[i]), pt(parts[i + 1]), pt(parts[i + 2]));
				i += 3;
			} else {
				path.closeSubpath();
				i += 1;
			}
		}
		return path;
	}();
	return result;
}

void AddButtonIcon(
		not_null<Ui::AbstractButton*> button,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	Expects(descriptor.icon != nullptr);

	struct IconWidget {
		IconWidget(QWidget *parent, IconDescriptor &&descriptor)
		: widget(parent)
		, icon(std::move(descriptor)) {
		}
		Ui::RpWidget widget;
		Icon icon;
		std::unique_ptr<Ui::Text::CustomEmoji> pack;
	};
	const auto icon = button->lifetime().make_state<IconWidget>(
		button,
		std::move(descriptor));
	// Mokagram: значки строк настроек из набора iOSTelegram (кроме бокового меню).
	const auto packable = [&] {
		for (auto w = button->parentWidget(); w; w = w->parentWidget()) {
			if (w->property("moka-main-menu").toBool()) {
				return false;
			}
		}
		return true;
	}();
	const auto own = (icon->icon.source()
		== &Mokagram::Settings::OwnRowIcon());
	if (packable) {
		const auto raw = icon;
		const auto source = icon->icon.source();
		const auto refresh = [=] {
			if (!raw->pack) {
				raw->pack = Mokagram::Settings::RowIconEmoji(
					source,
					[=] { raw->widget.update(); },
					24);
				raw->widget.update();
			}
		};
		refresh();
		if (!raw->pack) {
			Mokagram::Settings::RowIconsUpdated(
			) | rpl::on_next(refresh, raw->widget.lifetime());
		}
	}
	icon->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	// Mokagram: свой значок крупнее обычных, у него рисунок с полями.
	const auto grow = 0;
	icon->widget.resize(icon->icon.size() + QSize(grow, grow));
	icon->widget.show();
	button->sizeValue(
	) | rpl::on_next([=, left = st.iconLeft](QSize size) {
		icon->widget.moveToLeft(
			left - grow / 2,
			(size.height() - icon->widget.height()) / 2,
			size.width());
	}, icon->widget.lifetime());
	icon->widget.paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(&icon->widget);
		if (icon->pack) {
			const auto size = icon->widget.size();
			const auto side = own ? size.width() : 24;
			icon->pack->paint(p, {
				.textColor = st::windowFg->c,
				.now = crl::now(),
				.position = QPoint(
					(size.width() - side) / 2,
					(size.height() - side) / 2),
			});
			return;
		}
		if (packable && !icon->pack && Mokagram::Settings::HasRowIcon(icon->icon.source())) {
			// Старый значок не показываем, пока не загрузился набор.
			return;
		}
		icon->icon.paint(p, 0, 0);
	}, icon->widget.lifetime());
}

object_ptr<Button> CreateButtonWithIcon(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	auto result = object_ptr<Button>(parent, std::move(text), st);
	const auto button = result.data();
	button->setAllowTextLines();
	if (descriptor) {
		AddButtonIcon(button, st, std::move(descriptor));
	}
	return result;
}

not_null<Button*> AddButtonWithIcon(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	return container->add(
		CreateButtonWithIcon(
			container,
			std::move(text),
			st,
			std::move(descriptor)));
}

void CreateRightLabel(
		not_null<Button*> button,
		v::text::data &&label,
		const style::SettingsButton &st,
		rpl::producer<QString> buttonText,
		Ui::Text::MarkedContext context) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	name->show();
	const auto layout = [=, &st](const QString &buttonText) {
		const auto full = button->width()
			- st.padding.left()
			- st.padding.right()
			- st::settingsButtonRightSkip;
		const auto textWidth = st.style.font->width(buttonText);
		const auto natural = name->textMaxWidth();
		auto widestWord = 0;
		for (const auto &word : buttonText.split(' ', Qt::SkipEmptyParts)) {
			widestWord = std::max(widestWord, st.style.font->width(word));
		}

		// WHY: the value is never cut for a long label: the label wraps
		// instead (when the button allows it), keeping its widest word and
		// a third of the row, or the value moves under it when even that fails.
		const auto labelMin = std::max(widestWord, full / 3);
		const auto sideBySide = (textWidth + natural <= full)
			|| (natural <= full - labelMin);
		name->resizeToNaturalWidth(sideBySide
			? natural
			: (full + st.padding.right()));
		auto padding = st.padding;
		if (sideBySide) {
			padding.setRight(padding.right()
				+ st::settingsButtonRightSkip
				+ name->width());
		} else {
			padding.setBottom(padding.bottom() + name->height());
		}
		button->setPaddingOverride(padding);
		const auto single = st.padding.top()
			+ st.height
			+ st.padding.bottom();
		name->moveToRight(
			st::settingsButtonRightSkip,
			(sideBySide
				? (st.padding.top() + (button->height() - single) / 2)
				: (button->height() - st.padding.bottom() - name->height())));
	};
	if (v::text::is_plain(label)) {
		rpl::combine(
			button->sizeValue(),
			std::move(buttonText),
			v::text::take_plain(std::move(label))
		) | rpl::on_next([=](
				QSize,
				const QString &button,
				const QString &text) {
			name->setText(text);
			layout(button);
		}, name->lifetime());
	} else if (v::text::is_marked(label)) {
		rpl::combine(
			button->sizeValue(),
			std::move(buttonText),
			v::text::take_marked(std::move(label))
		) | rpl::on_next([=](
				QSize,
				const QString &button,
				const TextWithEntities &text) {
			name->setMarkedText(text, context);
			layout(button);
		}, name->lifetime());
	}
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

SeparatedToggle AddSeparatedToggle(
		not_null<Button*> button,
		const style::SettingsButton &st,
		bool checked) {
	const auto container = button->parentWidget();
	const auto toggle = Ui::CreateChild<Button>(container, nullptr, st);
	const auto checkView = button->lifetime().make_state<Ui::ToggleView>(
		st.toggle,
		checked,
		[=] { toggle->update(); });

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container);
	separator->paintRequest(
	) | rpl::on_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		Q_UNUSED(bg); // mokagram: без полоски у переключателя
	}, separator->lifetime());
	const auto separatorHeight = 2 * st.toggle.border + st.toggle.diameter;
	button->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		const auto width = st::rightsButtonToggleWidth;
		toggle->setGeometry(
			r.x() + r.width() - width,
			r.y(),
			width,
			r.height());
		separator->setGeometry(
			toggle->x() - st::lineWidth,
			r.y() + (r.height() - separatorHeight) / 2,
			st::lineWidth,
			separatorHeight);
	}, toggle->lifetime());

	const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggle);
	checkWidget->resize(checkView->getSize());
	checkWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggle->sizeValue(
	) | rpl::on_next([=, &st](const QSize &s) {
		checkWidget->moveToRight(
			st.toggleSkip,
			(s.height() - checkWidget->height()) / 2);
	}, toggle->lifetime());

	separator->show();
	checkWidget->show();
	toggle->show();

	return { toggle, checkView };
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto button = AddButtonWithIcon(
		container,
		rpl::duplicate(text),
		st,
		std::move(descriptor));
	CreateRightLabel(button, std::move(label), st, std::move(text));
	return button;
}

void AddDividerTextWithLottie(
		not_null<Ui::VerticalLayout*> container,
		DividerWithLottieDescriptor &&descriptor) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
		container.get(),
		0,
		st::defaultDividerBar,
		descriptor.parts);
	const auto verticalLayout = container->add(
		object_ptr<Ui::VerticalLayout>(container.get()));
	const auto size = descriptor.lottieSize.value_or(
		st::settingsFilterIconSize);
	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = descriptor.lottie,
			.sizeOverride = { size, size },
		},
		descriptor.lottieMargins.value_or(st::settingsFilterIconPadding));
	if (descriptor.showFinished) {
		const auto repeat = descriptor.lottieRepeat.value_or(
			anim::repeat::once);
		std::move(
			descriptor.showFinished
		) | rpl::on_next([animate = std::move(icon.animate), repeat] {
			animate(repeat);
		}, verticalLayout->lifetime());
	}
	verticalLayout->add(std::move(icon.widget));

	if (descriptor.about) {
		verticalLayout->add(
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				std::move(descriptor.about),
				st::settingsFilterDividerLabel),
			descriptor.aboutMargins.value_or(
				st::settingsFilterDividerLabelPadding),
			style::al_top)->setTryMakeSimilarLines(true);
	}

	verticalLayout->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

LottieIcon CreateLottieIcon(
		not_null<QWidget*> parent,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding,
		Fn<QColor()> colorOverride) {
	Expects(!descriptor.frame); // I'm not sure it considers limitFps.

	descriptor.limitFps = true;

	auto object = object_ptr<Ui::RpWidget>(parent);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize(QRect(
		QPoint(),
		descriptor.sizeOverride).marginsAdded(padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});
	const auto looped = raw->lifetime().make_state<bool>(true);

	const auto start = [=] {
		icon->animate([=] {
			raw->update();
		}, 0, icon->framesCount() - 1);
	};
	const auto animate = [=](anim::repeat repeat) {
		*looped = (repeat == anim::repeat::loop);
		start();
	};
	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top(), colorOverride
			? colorOverride()
			: std::optional<QColor>());
		if (!icon->animating() && icon->frameIndex() > 0 && *looped) {
			start();
		}

	}, raw->lifetime());

	return {
		.widget = std::move(object),
		.animate = std::move(animate),
		.animating = [=] { return icon->animating(); },
	};
}

SliderWithLabel MakeSliderWithLabel(
		QWidget *parent,
		const style::MediaSlider &sliderSt,
		const style::FlatLabel &labelSt,
		int skip,
		int minLabelWidth,
		bool ignoreWheel) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto height = std::max(
		sliderSt.seekSize.height(),
		labelSt.style.font->height);
	raw->resize(sliderSt.seekSize.width(), height);
	const auto slider = ignoreWheel
		? Ui::CreateChild<Ui::MediaSliderWheelless>(raw, sliderSt)
		: Ui::CreateChild<Ui::MediaSlider>(raw, sliderSt);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, labelSt);
	slider->resize(slider->width(), sliderSt.seekSize.height());
	rpl::combine(
		raw->sizeValue(),
		label->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize size) {
		const auto right = std::max(size.width(), minLabelWidth) + skip;
		label->moveToRight(0, (outer.height() - size.height()) / 2);
		const auto width = std::max(
			sliderSt.seekSize.width(),
			outer.width() - right);
		slider->resizeToWidth(width);
		slider->moveToLeft(0, (outer.height() - slider->height()) / 2);
	}, label->lifetime());
	return {
		.widget = std::move(result),
		.slider = slider,
		.label = label,
	};
}

void AddLottieIconWithCircle(
		not_null<Ui::VerticalLayout*> layout,
		object_ptr<Ui::RpWidget> icon,
		QMargins iconPadding,
		QSize circleSize) {
	const auto iconRow = layout->add(
		std::move(icon),
		iconPadding,
		style::al_top);

	const auto circle = Ui::CreateChild<Ui::RpWidget>(
		iconRow->parentWidget());
	circle->lower();
	circle->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (circle->width() - circleSize.width()) / 2;
		const auto top = (circle->height() - circleSize.height()) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawEllipse(QRect(QPoint(left, top), circleSize));
	});

	iconRow->geometryValue() | rpl::on_next([=](const QRect &g) {
		circle->setGeometry(g);
	}, circle->lifetime());
}

void AddPremiumStar(
		not_null<Button*> button,
		bool credits,
		Fn<bool()> isPaused) {
	const auto stops = credits
		? Ui::Premium::CreditsIconGradientStops()
		: Ui::Premium::ButtonGradientStops();

	const auto ministarsContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);
	using MiniStars = Ui::Premium::ColoredMiniStars;
	const auto ministars = button->lifetime().make_state<MiniStars>(
		ministarsContainer,
		false);
	ministars->setColorOverride(stops);

	const auto isPausedValue
		= button->lifetime().make_state<rpl::variable<bool>>(isPaused());
	isPausedValue->value() | rpl::on_next([=](bool value) {
		ministars->setPaused(value);
	}, ministarsContainer->lifetime());

	ministarsContainer->paintRequest(
	) | rpl::on_next([=] {
		(*isPausedValue) = isPaused();
		auto p = QPainter(ministarsContainer);
		{
			constexpr auto kScale = 0.35;
			const auto r = ministarsContainer->rect();
			p.translate(r.center());
			p.scale(kScale, kScale);
			p.translate(-r.center());
		}
		ministars->paint(p);
	}, ministarsContainer->lifetime());

	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto star = [&] {
		const auto factor = style::DevicePixelRatio();
		const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
		auto image = QImage(
			size * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto star = QSvgRenderer(Ui::Premium::ColorizedSvg(stops));
			star.render(&p, Rect(size));
		}
		return image;
	}();
	ministarsContainer->hide();
	badge->resize(24, 24);
	const auto packState = badge->lifetime().make_state<
		std::unique_ptr<Ui::Text::CustomEmoji>>();
	const auto alt = QString::fromUtf8(credits ? "⭐️" : "👑");
	const auto refreshPack = [=] {
		if (!*packState) {
			*packState = Mokagram::Settings::AltIconEmoji(
				alt,
				[=] { badge->update(); },
				24);
			badge->update();
		}
	};
	refreshPack();
	if (!*packState) {
		Mokagram::Settings::RowIconsUpdated(
		) | rpl::on_next(refreshPack, badge->lifetime());
	}
	badge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(badge);
		if (*packState) {
			(*packState)->paint(p, {
				.textColor = st::windowFg->c,
				.now = crl::now(),
				.position = QPoint(),
			});
			return;
		}
		p.drawImage(
			(badge->width() - star.width() / style::DevicePixelRatio()) / 2,
			(badge->height() - star.height() / style::DevicePixelRatio()) / 2,
			star);
	}, badge->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		badge->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - badge->width()) / 2,
			(s.height() - badge->height()) / 2);
		ministarsContainer->moveToLeft(
			badge->x() - (fullHeight - badge->height()) / 2,
			0);
	}, badge->lifetime());

	ministarsContainer->resize(fullHeight, fullHeight);
	ministars->setCenter(ministarsContainer->rect());
}

SectionSearchRow CreateSectionSearchRow(
		not_null<QWidget*> parent,
		const QString &query) {
	auto controller = std::make_unique<Ui::SearchFieldController>(query);
	auto rowView = controller->createRowView(
		parent,
		st::infoLayerMediaSearch);
	const auto row = rowView.wrap.release();
	const auto field = rowView.field.data();
	row->show();
	return {
		.controller = std::move(controller),
		.row = row,
		.field = field,
	};
}

QStringList SearchWords(const QString &text) {
	auto simple = text;
	return TextUtilities::PrepareSearchWords(simple.replace('#', ' '));
}

bool MatchesWords(const QStringList &terms, const QStringList &words) {
	for (const auto &word : words) {
		auto found = false;
		for (const auto &term : terms) {
			if (term.startsWith(word)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

} // namespace Settings
