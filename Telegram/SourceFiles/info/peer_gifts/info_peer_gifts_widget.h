/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_star_gift.h"
#include "info/info_content_widget.h"

class UserData;
struct PeerListState;

namespace Ui {
class RpWidget;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info::PeerGifts {

struct ListState {
	std::vector<Data::SavedStarGift> list;
	QString offset;
};

struct Filter {
	bool sortByValue : 1 = false;
	bool skipUnlimited : 1 = false;
	bool skipLimited : 1 = false;
	bool skipUnique : 1 = false;
	bool skipSaved : 1 = false;
	bool skipUnsaved : 1 = false;

	friend inline bool operator==(Filter, Filter) = default;
};

class InnerWidget;

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<PeerData*> peer);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	void setListState(std::unique_ptr<ListState> state);
	std::unique_ptr<ListState> listState();

	~Memento();

private:
	std::unique_ptr<ListState> _listState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

	rpl::producer<QString> title() override;

	rpl::producer<bool> desiredBottomShadowVisibility() override;

	void showFinished() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	void setupNotifyCheckbox(bool enabled);

	InnerWidget *_inner = nullptr;
	QPointer<Ui::SlideWrap<Ui::RpWidget>> _pinnedToBottom;
	rpl::variable<bool> _hasPinnedToBottom;
	rpl::variable<Filter> _filter;
	bool _shown = false;

};

} // namespace Info::PeerGifts
