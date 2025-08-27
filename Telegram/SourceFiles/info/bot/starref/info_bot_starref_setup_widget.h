/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "info/bot/starref/info_bot_starref_common.h"

namespace Ui::Premium {
class TopBarAbstract;
} // namespace Ui::Premium

namespace Ui {
template <typename Widget>
class FadeWrap;
class IconButton;
class AbstractButton;
class VerticalLayout;
} // namespace Ui

namespace Info::BotStarRef::Setup {

struct State;
class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer);
	~Memento();

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

	bool showInternal(not_null<ContentMemento*> memento) override;
	rpl::producer<QString> title() override;
	rpl::producer<bool> desiredShadowVisibility() const override;
	void showFinished() override;
	void setInnerFocus() override;
	void enableBackButton() override;

	[[nodiscard]] not_null<PeerData*> peer() const;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	[[nodiscard]] std::unique_ptr<Ui::Premium::TopBarAbstract> setupTop();
	[[nodiscard]] std::unique_ptr<Ui::RpWidget> setupBottom();

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	const not_null<InnerWidget*> _inner;
	const not_null<State*> _state;

	std::unique_ptr<Ui::Premium::TopBarAbstract> _top;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backEnabled;

	std::unique_ptr<Ui::RpWidget> _bottom;

};

[[nodiscard]] bool Allowed(not_null<PeerData*> peer);
[[nodiscard]] std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer);

} // namespace Info::BotStarRef::Setup
