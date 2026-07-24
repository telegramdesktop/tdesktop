/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class DynamicImage;
class GenericBox;
class VerticalLayout;
} // namespace Ui

namespace UrlAuthBox {

struct Result {
	bool auth : 1 = false;
	bool allowWrite : 1 = false;
	bool sharePhone : 1 = false;
	QString matchCode;
};

class SwitchableUserpicButton final : public Ui::RippleButton {
public:
	SwitchableUserpicButton(
		not_null<Ui::RpWidget*> parent,
		int size);

	void setExpanded(bool expanded);
	void setUserpic(not_null<Ui::RpWidget*>);

private:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	const int _size;
	const int _userpicSize;
	const int _skip;
	bool _expanded = false;
	Ui::RpWidget *_userpic = nullptr;

};

void AddAuthInfoRow(
	not_null<Ui::VerticalLayout*> container,
	const QString &topText,
	const QString &bottomText,
	const QString &leftText,
	const style::icon &icon);

void ShowMatchCodesBox(
	not_null<Ui::GenericBox*> box,
	Fn<std::shared_ptr<Ui::DynamicImage>(QString)> emojiImageFactory,
	const QString &domain,
	const QStringList &codes,
	Fn<void(QString)> callback,
	bool isApp = false);

void Show(
	not_null<Ui::GenericBox*> box,
	const QString &url,
	const QString &domain,
	const QString &selfName,
	const QString &botName,
	Fn<void(Result)> callback);

void ShowDetails(
	not_null<Ui::GenericBox*> box,
	const QString &url,
	const QString &domain,
	Fn<std::shared_ptr<Ui::DynamicImage>(QString)> emojiImageFactory,
	Fn<void(Result)> callback,
	object_ptr<Ui::RpWidget> userpicOwned,
	rpl::producer<QString> botName,
	const QString &browser,
	const QString &platform,
	const QString &ip,
	const QString &region,
	rpl::producer<QStringList> matchCodes = rpl::single(QStringList()),
	bool isApp = false);

} // namespace UrlAuthBox
