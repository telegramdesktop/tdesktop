/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "info/info_controller.h"

namespace Settings {
class Section;
} // namespace Settings

namespace Info {
namespace Settings {

using Type = Section::SettingsType;

struct Tag;

class Memento final : public ContentMemento {
public:
	Memento(not_null<UserData*> self, Type type);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	Type type() const {
		return _type;
	}

	not_null<UserData*> self() const {
		return settingsSelf();
	}

	~Memento();

private:
	Type _type = Type();

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller);
	~Widget();

	not_null<UserData*> self() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<bool> canSaveChanges() const override;
	void saveChanges(FnMut<void()> done) override;

	rpl::producer<bool> desiredShadowVisibility() const override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	not_null<UserData*> _self;
	Type _type = Type();

	not_null<::Settings::Section*> _inner;

};

} // namespace Settings
} // namespace Info
