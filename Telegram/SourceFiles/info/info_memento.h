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

#include "ui/rp_widget.h"
#include "info/info_wrap_widget.h"
#include "window/section_memento.h"

namespace Storage {
enum class SharedMediaType : char;
} // namespace Storage

namespace Ui {
class ScrollArea;
struct ScrollToRequest;
} // namespace Ui

namespace Info {

class ContentMemento;
class WrapWidget;

class Memento final : public Window::SectionMemento {
public:
	Memento(PeerId peerId);
	Memento(PeerId peerId, Section section);
	Memento(std::vector<std::unique_ptr<ContentMemento>> stack);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) override;

	object_ptr<Window::LayerWidget> createLayer(
		not_null<Window::Controller*> controller,
		const QRect &geometry) override;

	int stackSize() const {
		return int(_stack.size());
	}
	std::vector<std::unique_ptr<ContentMemento>> takeStack();

	not_null<ContentMemento*> content() {
		Expects(!_stack.empty());
		return _stack.back().get();
	}

	static Section DefaultSection(not_null<PeerData*> peer);
	static Memento Default(not_null<PeerData*> peer);

	~Memento();

private:
	static std::vector<std::unique_ptr<ContentMemento>> DefaultStack(
		PeerId peerId,
		Section section);
	static std::unique_ptr<ContentMemento> DefaultContent(
		PeerId peerId,
		Section section);

	std::vector<std::unique_ptr<ContentMemento>> _stack;

};

class MoveMemento final : public Window::SectionMemento {
public:
	MoveMemento(object_ptr<WrapWidget> content);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) override;

	object_ptr<Window::LayerWidget> createLayer(
		not_null<Window::Controller*> controller,
		const QRect &geometry) override;

	bool instant() const override {
		return true;
	}

	object_ptr<WrapWidget> takeContent(
		QWidget *parent,
		Wrap wrap);

private:
	object_ptr<WrapWidget> _content;

};

} // namespace Info
