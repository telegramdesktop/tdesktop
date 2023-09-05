/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {
class LayerWidget;
} // namespace Ui

namespace Data {
class ForumTopic;
} // namespace Data

namespace Window {

class SessionController;
class SectionWidget;
enum class Column;

class SectionMemento {
public:
	[[nodiscard]] virtual object_ptr<SectionWidget> createWidget(
		QWidget *parent,
		not_null<SessionController*> controller,
		Column column,
		const QRect &geometry) = 0;

	[[nodiscard]] virtual object_ptr<Ui::LayerWidget> createLayer(
			not_null<SessionController*> controller,
			const QRect &geometry) {
		return nullptr;
	}
	[[nodiscard]] virtual bool instant() const {
		return false;
	}

	[[nodiscard]] virtual Data::ForumTopic *topicForRemoveRequests() const {
		return nullptr;
	}
	[[nodiscard]] virtual rpl::producer<> removeRequests() const {
		return rpl::never<>();
	}

	virtual ~SectionMemento() = default;

};

} // namespace Window
