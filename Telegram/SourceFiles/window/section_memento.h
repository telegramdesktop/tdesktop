/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {

class Controller;
class SectionWidget;
class LayerWidget;
enum class Column;

class SectionMemento {
public:
	virtual object_ptr<SectionWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		Column column,
		const QRect &geometry) = 0;

	virtual object_ptr<LayerWidget> createLayer(
			not_null<Controller*> controller,
			const QRect &geometry) {
		return nullptr;
	}
	virtual bool instant() const {
		return false;
	}

	virtual ~SectionMemento() = default;

};

} // namespace Window
