#include "settings/detailed_settings_button.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

DetailedSettingsButton::DetailedSettingsButton(
	QWidget *parent,
	rpl::producer<QString> title,
	rpl::producer<QString> description,
	Settings::IconDescriptor icon,
	rpl::producer<bool> toggled,
	const style::DetailedSettingsButtonStyle &rowStyle)
: Ui::RippleButton(parent, rowStyle.button.ripple)
, _style(rowStyle)
, _title(0)
, _description(0)
, _toggle(std::make_unique<Ui::ToggleView>(
	_style.button.toggle,
	false,
	[this] { rtlupdate(toggleRect()); }))
, _iconForeground(icon.icon)
, _iconBackground(icon.background)
, _iconBackgroundBrush(std::move(icon.backgroundBrush)) {
	std::move(
		title
	) | rpl::on_next([=](const QString &text) {
		_title.setText(
			_style.button.style,
			text,
			kDefaultTextOptions);
		accessibilityNameChanged();
		refreshLayout();
	}, lifetime());
	std::move(
		description
	) | rpl::on_next([=](const QString &text) {
		_description.setText(
			_style.description.style,
			text,
			kDefaultTextOptions);
		refreshLayout();
	}, lifetime());
	addClickHandler([=] {
		if (!_toggleLocked) {
			_toggle->setChecked(!_toggle->checked(), anim::type::normal);
		}
	});
	std::move(
		toggled
	) | rpl::on_next([=](bool checked) {
		_toggle->setChecked(checked, anim::type::normal);
	}, lifetime());
	_toggle->checkedChanges() | rpl::on_next([=] {
		accessibilityStateChanged({ .checked = true });
	}, lifetime());
	widthValue() | rpl::on_next([=](int width) {
		if (width > 0) {
			refreshLayout();
		}
	}, lifetime());
}

QString DetailedSettingsButton::accessibilityName() {
	return _title.toString();
}

Ui::AccessibilityState DetailedSettingsButton::accessibilityState() const {
	return { .checkable = true, .checked = _toggle->checked() };
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
QAccessible::Role DetailedSettingsButton::accessibilityRole() {
	return QAccessible::Role::Switch;
}
#endif

bool DetailedSettingsButton::toggled() const {
	return _toggle->checked();
}

rpl::producer<bool> DetailedSettingsButton::toggledChanges() const {
	return _toggle->checkedChanges();
}

rpl::producer<not_null<QEvent*>>
DetailedSettingsButton::clickAreaEvents() const {
	return events();
}

void DetailedSettingsButton::setToggleLocked(bool locked) {
	_toggleLocked = locked;
	_toggle->setLocked(locked);
}

void DetailedSettingsButton::finishAnimating() {
	_toggle->finishAnimating();
}

void DetailedSettingsButton::onStateChanged(
		State was,
		StateChangeSource source) {
	const auto wasDisabled = !!(was & StateFlag::Disabled);
	const auto nowDisabled = isDisabled();
	if (!nowDisabled || !isDown()) {
		RippleButton::onStateChanged(was, source);
	}
	_toggle->setStyle(
		isOver()
			? _style.button.toggleOver
			: _style.button.toggle);
	if (nowDisabled != wasDisabled) {
		setPointerCursor(!nowDisabled);
	}
	update();
}

void DetailedSettingsButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = isOver() || isDown();
	p.fillRect(
		e->rect(),
		over ? _style.button.textBgOver : _style.button.textBg);
	paintRipple(p, 0, 0);

	paintIcon(p);

	p.setPen(over
		? _style.button.textFgOver
		: _style.button.textFg);
	_title.drawLeftElided(
		p,
		_style.button.padding.left(),
		titleTop(),
		_titleWidth,
		width());
	p.setPen(_style.description.textFg);
	if (_descriptionHeight > 0) {
		_description.drawLeft(
			p,
			_style.button.padding.left(),
			_descriptionTop,
			_descriptionWidth,
			width());
	}

	const auto toggle = toggleRect();
	_toggle->paint(p, toggle.left(), toggle.top(), width());
}

int DetailedSettingsButton::resizeGetHeight(int newWidth) {
	_titleWidth = textAvailableWidth(newWidth);
	_descriptionWidth
		= std::max(1, _titleWidth - _style.descriptionRightSkip);
	_descriptionTop = descriptionTopValue();
	_descriptionHeight = _description.countHeight(_descriptionWidth);
	return _descriptionTop
		+ _descriptionHeight
		+ _style.descriptionBottomSkip;
}

void DetailedSettingsButton::refreshLayout() {
	Ui::PostponeCall(crl::guard(this, [=] {
		const auto current = widthNoMargins();
		if (current > 0) {
			resizeToWidth(current);
		}
		update();
	}));
}

int DetailedSettingsButton::titleTop() const {
	return _style.button.padding.top();
}

int DetailedSettingsButton::titleHeight() const {
	return _style.button.height;
}

int DetailedSettingsButton::descriptionTopValue() const {
	return titleTop() + titleHeight() + _style.descriptionTopSkip;
}

int DetailedSettingsButton::firstDescriptionLineBottom() const {
	const auto top = (_descriptionTop > 0)
		? _descriptionTop
		: descriptionTopValue();
	return top + qMax(
		_style.description.style.lineHeight,
		_style.description.style.font->height);
}

int DetailedSettingsButton::textAvailableWidth(int outerw) const {
	const auto toggle = _toggle->getSize();
	return std::max(
		1,
		outerw
			- _style.button.padding.left()
			- _style.button.padding.right()
			- _style.button.toggleSkip
			- toggle.width());
}

QRect DetailedSettingsButton::toggleRect() const {
	const auto toggle = _toggle->getSize();
	const auto anchorBottom = firstDescriptionLineBottom();
	return QRect(
		width() - _style.button.toggleSkip - toggle.width(),
		(anchorBottom - toggle.height()) / 2,
		toggle.width(),
		toggle.height());
}

QRect DetailedSettingsButton::iconAreaRect() const {
	const auto size = iconSize();
	return QRect(
		_style.button.iconLeft,
		titleTop(),
		size,
		size);
}

QRect DetailedSettingsButton::iconRect() const {
	const auto area = iconAreaRect();
	const auto padding = _style.iconPadding;
	const auto width = std::max(1, area.width() - (2 * padding));
	const auto height = std::max(1, area.height() - (2 * padding));
	return QRect(
		area.x() + padding,
		area.y() + padding,
		width,
		height);
}

QRect DetailedSettingsButton::iconForegroundRect(QRect iconRect) const {
	const auto padding = _style.iconForegroundPadding;
	const auto width = std::max(1, iconRect.width() - (2 * padding));
	const auto height = std::max(1, iconRect.height() - (2 * padding));
	return QRect(
		iconRect.x() + padding,
		iconRect.y() + padding,
		width,
		height);
}

int DetailedSettingsButton::iconSize() const {
	const auto result = firstDescriptionLineBottom() - titleTop();
	return std::max(1, result);
}

void DetailedSettingsButton::paintIcon(QPainter &p) const {
	if (!_iconForeground && !_iconBackground && !_iconBackgroundBrush) {
		return;
	}
	const auto rect = iconRect();
	if (_iconBackground) {
		p.setPen(Qt::NoPen);
		p.setBrush(*_iconBackground);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.drawRoundedRect(rect, _style.iconRadius, _style.iconRadius);
		}
	} else if (_iconBackgroundBrush) {
		p.setPen(Qt::NoPen);
		p.setBrush(*_iconBackgroundBrush);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.drawRoundedRect(rect, _style.iconRadius, _style.iconRadius);
		}
	}
	if (_iconForeground) {
		const auto source = _iconForeground->size();
		if (!source.isEmpty()) {
			const auto foreground = iconForegroundRect(rect);
			const auto scale = std::min(
				double(foreground.width()) / source.width(),
				double(foreground.height()) / source.height());
			const auto shiftX = (foreground.width()
				- (source.width() * scale)) / 2.;
			const auto shiftY = (foreground.height()
				- (source.height() * scale)) / 2.;
			p.save();
			p.translate(foreground.x() + shiftX, foreground.y() + shiftY);
			p.scale(scale, scale);
			_iconForeground->paint(p, 0, 0, source.width());
			p.restore();
		}
	}
}

not_null<DetailedSettingsButton*> AddDetailedSettingsButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		Settings::IconDescriptor icon,
		rpl::producer<bool> toggled,
		const style::DetailedSettingsButtonStyle &rowStyle) {
	return container->add(object_ptr<DetailedSettingsButton>(
		container,
		std::move(title),
		std::move(description),
		std::move(icon),
		std::move(toggled),
		rowStyle));
}
