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
#include "base/object_ptr.h"

namespace Api {
struct WhoReadList;
} // namespace Api

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Data {
class ForumTopic;
class SavedSublist;
struct ReactionId;
} // namespace Data

namespace Ui {
class ScrollArea;
struct ScrollToRequest;
} // namespace Ui

namespace Info {
namespace Settings {
struct Tag;
} // namespace Settings

namespace Downloads {
struct Tag;
} // namespace Downloads

class ContentMemento;
class WrapWidget;

class Memento final : public Window::SectionMemento {
public:
	explicit Memento(not_null<PeerData*> peer);
	Memento(not_null<PeerData*> peer, Section section);
	explicit Memento(not_null<Data::ForumTopic*> topic);
	Memento(not_null<Data::ForumTopic*> topic, Section section);
	explicit Memento(not_null<Data::SavedSublist*> sublist);
	Memento(not_null<Data::SavedSublist*> sublist, Section section);
	Memento(Settings::Tag settings, Section section);
	Memento(not_null<PollData*> poll, FullMsgId contextId);
	Memento(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);
	explicit Memento(std::vector<std::shared_ptr<ContentMemento>> stack);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	object_ptr<Ui::LayerWidget> createLayer(
		not_null<Window::SessionController*> controller,
		const QRect &geometry) override;

	rpl::producer<> removeRequests() const override {
		return _removeRequests.events();
	}

	int stackSize() const {
		return int(_stack.size());
	}
	std::vector<std::shared_ptr<ContentMemento>> takeStack();

	not_null<ContentMemento*> content() {
		Expects(!_stack.empty());

		return _stack.back().get();
	}

	static Section DefaultSection(not_null<PeerData*> peer);
	static std::shared_ptr<Memento> Default(not_null<PeerData*> peer);

	~Memento();

private:
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		not_null<PeerData*> peer,
		Section section);
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		not_null<Data::ForumTopic*> topic,
		Section section);
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		not_null<Data::SavedSublist*> sublist,
		Section section);
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		Settings::Tag settings,
		Section section);
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		not_null<PollData*> poll,
		FullMsgId contextId);
	static std::vector<std::shared_ptr<ContentMemento>> DefaultStack(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);

	static std::shared_ptr<ContentMemento> DefaultContent(
		not_null<PeerData*> peer,
		Section section);
	static std::shared_ptr<ContentMemento> DefaultContent(
		not_null<Data::ForumTopic*> topic,
		Section section);
	static std::shared_ptr<ContentMemento> DefaultContent(
		not_null<Data::SavedSublist*> sublist,
		Section section);

	std::vector<std::shared_ptr<ContentMemento>> _stack;
	rpl::event_stream<> _removeRequests;
	rpl::lifetime _lifetime;

};

class MoveMemento final : public Window::SectionMemento {
public:
	MoveMemento(object_ptr<WrapWidget> content);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	object_ptr<Ui::LayerWidget> createLayer(
		not_null<Window::SessionController*> controller,
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
