/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "info/info_wrap_widget.h"
#include "dialogs/dialogs_key.h"
#include "window/section_memento.h"

namespace Storage {
enum class SharedMediaType : char;
} // namespace Storage

namespace Data {
class Feed;
} // namespace Data

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
	Memento(not_null<Data::Feed*> feed, Section section);
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

	static Section DefaultSection(Dialogs::Key key);
	static Memento Default(Dialogs::Key key);

	~Memento();

private:
	static std::vector<std::unique_ptr<ContentMemento>> DefaultStack(
		PeerId peerId,
		Section section);
	static std::vector<std::unique_ptr<ContentMemento>> DefaultStack(
		not_null<Data::Feed*> feed,
		Section section);
	static std::unique_ptr<ContentMemento> DefaultContent(
		not_null<Data::Feed*> feed,
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
