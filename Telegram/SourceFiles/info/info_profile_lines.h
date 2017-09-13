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
#pragma once

#include <rpl/producer.h>
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"

enum LangKey : int;

namespace style {
struct FlatLabel;
struct InfoProfileButton;
} // namespace style

namespace Lang {
rpl::producer<QString> Viewer(LangKey key);
} // namespace Lang

namespace Ui {
class FlatLabel;
class Checkbox;
class IconButton;
class ToggleView;
} // namespace Ui

namespace Profile {
class UserpicButton;
} // namespace Profile

namespace Info {
namespace Profile {

inline auto WithEmptyEntities() {
	return rpl::map([](QString &&text) {
		return TextWithEntities{ std::move(text), {} };
	});
}

inline auto ToUpperValue() {
	return rpl::map([](QString &&text) {
		return std::move(text).toUpper();
	});
}

rpl::producer<TextWithEntities> PhoneViewer(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> BioViewer(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> UsernameViewer(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> AboutViewer(
	not_null<PeerData*> peer);
rpl::producer<TextWithEntities> LinkViewer(
	not_null<PeerData*> peer);
rpl::producer<bool> NotificationsEnabledViewer(
	not_null<PeerData*> peer);
rpl::producer<bool> IsContactViewer(
	not_null<UserData*> user);
rpl::producer<bool> CanShareContactViewer(
	not_null<UserData*> user);
rpl::producer<bool> CanAddContactViewer(
	not_null<UserData*> user);

class FloatingIcon : public Ui::RpWidget {
public:
	FloatingIcon(
		QWidget *parent,
		not_null<RpWidget*> above,
		const style::icon &icon);

	FloatingIcon(
		QWidget *parent,
		not_null<RpWidget*> above,
		const style::icon &icon,
		QPoint position);

	FloatingIcon(
		QWidget *parent,
		const style::icon &icon);

	FloatingIcon(
		QWidget *parent,
		const style::icon &icon,
		QPoint position);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Tag {
	};
	FloatingIcon(
		QWidget *parent,
		RpWidget *above,
		const style::icon &icon,
		QPoint position,
		const Tag &);

	not_null<const style::icon*> _icon;
	QPoint _point;

};

class LabeledLine : public Ui::SlideWrap<Ui::VerticalLayout> {
public:
	LabeledLine(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&label,
		rpl::producer<TextWithEntities> &&text);

	LabeledLine(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&label,
		rpl::producer<TextWithEntities> &&text,
		const style::FlatLabel &textSt,
		const style::margins &padding);

};

class CoverLine : public Ui::RpWidget {
public:
	CoverLine(QWidget *parent, not_null<PeerData*> peer);

	void setOnlineCount(int onlineCount);
	void setHasToggle(bool hasToggle);

	rpl::producer<bool> toggled() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void initViewers();
	void initUserpicButton();
	void refreshUserpicLink();
	void refreshNameText();
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);

	not_null<PeerData*> _peer;
	int _onlineCount = 0;

	object_ptr<::Profile::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	object_ptr<Ui::Checkbox> _toggle = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };

	rpl::lifetime _lifetime;

};

class Button : public Ui::RippleButton {
public:
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text);
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::InfoProfileButton &st);

	void setToggled(bool toggled);
	rpl::producer<bool> toggledValue() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void onStateChanged(
		State was,
		StateChangeSource source) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setText(QString &&text);
	QRect toggleRect() const;
	void updateVisibleText(int newWidth);

	const style::InfoProfileButton &_st;
	QString _original;
	QString _text;
	int _originalWidth = 0;
	int _textWidth = 0;
	std::unique_ptr<Ui::ToggleView> _toggle;

};

} // namespace Profile
} // namespace Info
