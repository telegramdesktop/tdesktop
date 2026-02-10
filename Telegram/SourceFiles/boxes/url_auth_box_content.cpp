/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/url_auth_box_content.h"

#include "base/qthelp_url.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace UrlAuthBox {
namespace {

} // namespace

SwitchableUserpicButton::SwitchableUserpicButton(
	not_null<Ui::RpWidget*> parent,
	int size)
: RippleButton(parent, st::defaultRippleAnimation)
, _size(size)
, _userpicSize(st::restoreUserpicIcon.photoSize)
, _skip((_size - _userpicSize) / 2) {
	resize(_size, _size);
}

void SwitchableUserpicButton::setUserpic(not_null<Ui::RpWidget*> userpic) {
	_userpic = userpic;
	_userpic->setParent(this);
	_userpic->moveToRight(_skip, _skip);
	_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	_userpic->show();
	update();
}

void SwitchableUserpicButton::setExpanded(bool expanded) {
	if (_expanded == expanded) {
		return;
	}
	_expanded = expanded;
	const auto w = _expanded
		? (_size * 2.5 - _userpicSize)
		: _size;
	resize(w, _size);
	if (_userpic) {
		_userpic->moveToRight(_skip, _skip);
	}
	update();
}

void SwitchableUserpicButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintRipple(p, 0, 0);

	if (!_expanded) {
		return;
	}

	const auto arrowSize = st::lineWidth * 10;
	const auto center = QPoint(_size / 2, height() / 2 + st::lineWidth * 3);

	auto pen = QPen(st::windowSubTextFg);
	pen.setWidthF(st::lineWidth * 1.5);
	p.setPen(pen);
	p.setRenderHint(QPainter::Antialiasing);

	p.drawLine(center, center + QPoint(-arrowSize / 2, -arrowSize / 2));
	p.drawLine(center, center + QPoint(arrowSize / 2, -arrowSize / 2));
}

QImage SwitchableUserpicButton::prepareRippleMask() const {
	return _expanded
		? Ui::RippleAnimation::RoundRectMask(size(), height() / 2)
		: Ui::RippleAnimation::EllipseMask(size());
}

QPoint SwitchableUserpicButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

void AddAuthInfoRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &topText,
		const QString &bottomText,
		const QString &leftText,
		const style::icon &icon) {
	const auto row = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);

	const auto topLabel = Ui::CreateChild<Ui::FlatLabel>(
		row,
		topText,
		st::urlAuthBoxRowTopLabel);
	topLabel->setSelectable(true);
	Ui::InstallTooltip(topLabel, [=] {
		return (topLabel->textMaxWidth() > topLabel->width())
			? topText
			: QString();
	});
	const auto bottomLabel = Ui::CreateChild<Ui::FlatLabel>(
		row,
		bottomText,
		st::urlAuthBoxRowBottomLabel);
	bottomLabel->setSelectable(true);
	Ui::InstallTooltip(bottomLabel, [=] {
		return (bottomLabel->textMaxWidth() > bottomLabel->width())
			? bottomText
			: QString();
	});
	const auto leftLabel = Ui::CreateChild<Ui::FlatLabel>(
		row,
		leftText,
		st::boxLabel);

	rpl::combine(
		row->widthValue(),
		topLabel->sizeValue(),
		bottomLabel->sizeValue()
	) | rpl::on_next([=](int rowWidth, QSize topSize, QSize bottomSize) {
		const auto totalHeight = topSize.height() + bottomSize.height();
		row->resize(rowWidth, totalHeight);

		const auto left = st::sessionValuePadding.left();
		const auto availableWidth = rowWidth
			- leftLabel->width()
			- left
			- st::defaultVerticalListSkip;

		topLabel->resizeToNaturalWidth(availableWidth);
		topLabel->moveToRight(0, 0);
		bottomLabel->resizeToNaturalWidth(availableWidth);
		bottomLabel->moveToRight(0, topSize.height());

		leftLabel->moveToLeft(left, (totalHeight - leftLabel->height()) / 2);
	}, row->lifetime());

	{
		const auto widget = Ui::CreateChild<Ui::RpWidget>(row);
		widget->resize(icon.size());

		rpl::combine(
			row->widthValue(),
			topLabel->sizeValue(),
			bottomLabel->sizeValue()
		) | rpl::on_next([=](int rowWidth, QSize topSize, QSize bottomSize) {
			const auto totalHeight = topSize.height() + bottomSize.height();
			widget->moveToLeft(0, (totalHeight - leftLabel->height()) / 2);
		}, row->lifetime());

		widget->paintRequest() | rpl::on_next([=, &icon] {
			auto p = QPainter(widget);
			icon.paintInCenter(p, widget->rect());
		}, widget->lifetime());
	}
}

void Show(
		not_null<Ui::GenericBox*> box,
		const QString &url,
		const QString &domain,
		const QString &selfName,
		const QString &botName,
		Fn<void(Result)> callback) {
	box->setWidth(st::boxWidth);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_url_auth_open_confirm(tr::now, lt_link, url),
			st::boxLabel),
		st::boxPadding);

	const auto addCheckbox = [&](const TextWithEntities &text) {
		const auto checkbox = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				text,
				true,
				st::urlAuthCheckbox),
			style::margins(
				st::boxPadding.left(),
				st::boxPadding.bottom(),
				st::boxPadding.right(),
				st::boxPadding.bottom()));
		checkbox->setAllowTextLines();
		return checkbox;
	};

	const auto auth = addCheckbox(
		tr::lng_url_auth_login_option(
			tr::now,
			lt_domain,
			tr::bold(domain),
			lt_user,
			tr::bold(selfName),
			tr::marked));

	const auto allow = !botName.isEmpty()
		? addCheckbox(tr::lng_url_auth_allow_messages(
			tr::now,
			lt_bot,
			tr::bold(botName),
			tr::marked))
		: nullptr;

	if (allow) {
		rpl::single(
			auth->checked()
		) | rpl::then(
			auth->checkedChanges()
		) | rpl::on_next([=](bool checked) {
			if (!checked) {
				allow->setChecked(false);
			}
			allow->setDisabled(!checked);
		}, auth->lifetime());
	}

	box->addButton(tr::lng_open_link(), [=] {
		const auto authed = auth->checked();
		const auto allowed = (authed && allow && allow->checked());
		callback({
			.auth = authed,
			.allowWrite = allowed,
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ShowDetails(
		not_null<Ui::GenericBox*> box,
		const QString &url,
		const QString &domain,
		Fn<void(Result)> callback,
		object_ptr<Ui::RpWidget> userpicOwned,
		rpl::producer<QString> botName,
		const QString &browser,
		const QString &platform,
		const QString &ip,
		const QString &region) {
	box->setWidth(st::boxWidth);

	const auto content = box->verticalLayout();

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	if (userpicOwned) {
		const auto userpic = content->add(
			std::move(userpicOwned),
			st::boxRowPadding,
			style::al_top);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}

	const auto domainUrl = qthelp::validate_url(domain);
	const auto userpicButtonWidth = st::restoreUserpicIcon.photoSize;
	const auto titlePadding = style::margins(
		st::boxRowPadding.left(),
		st::boxRowPadding.top(),
		st::boxRowPadding.right() + userpicButtonWidth,
		st::boxRowPadding.bottom());
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			domainUrl.isEmpty()
				? tr::lng_url_auth_login_button(tr::marked)
				: tr::lng_url_auth_login_title(
					lt_domain,
					rpl::single(Ui::Text::Link(domain, domainUrl)),
					tr::marked),
			st::boxTitle),
		titlePadding,
		style::al_top);
	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_url_auth_site_access(tr::rich),
			st::urlAuthCheckboxAbout),
		st::boxRowPadding);

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	if (!platform.isEmpty() || !browser.isEmpty()) {
		AddAuthInfoRow(
			content,
			platform,
			browser,
			tr::lng_url_auth_device_label(tr::now),
			st::menuIconDevices);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	if (!ip.isEmpty() || !region.isEmpty()) {
		AddAuthInfoRow(
			content,
			ip,
			region,
			tr::lng_url_auth_ip_label(tr::now),
			st::menuIconAddress);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	Ui::AddDividerText(
		content,
		rpl::single(tr::lng_url_auth_login_attempt(tr::now)));
	Ui::AddSkip(content);

	auto allowMessages = (Ui::SettingsButton*)(nullptr);
	if (botName) {
		allowMessages = content->add(
			object_ptr<Ui::SettingsButton>(
				content,
				tr::lng_url_auth_allow_messages_label()));
		allowMessages->toggleOn(rpl::single(false));
		Ui::AddSkip(content);
		Ui::AddDividerText(
			content,
			tr::lng_url_auth_allow_messages_about(
				lt_bot,
				std::move(botName)));
		Ui::AddSkip(content);
	}

	box->addButton(tr::lng_url_auth_login_button(), [=] {
		callback({
			.auth = true,
			.allowWrite = (allowMessages && allowMessages->toggled()),
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace UrlAuthBox
