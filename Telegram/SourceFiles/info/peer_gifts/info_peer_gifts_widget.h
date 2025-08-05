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
	bool skipUpgradable : 1 = false;
	bool skipUnique : 1 = false;
	bool skipSaved : 1 = false;
	bool skipUnsaved : 1 = false;

	[[nodiscard]] bool skipsSomething() const {
		return skipLimited
			|| skipUnlimited
			|| skipSaved
			|| skipUnsaved
			|| skipUpgradable
			|| skipUnique;
	}

	friend inline bool operator==(Filter, Filter) = default;
};

struct Descriptor {
	Filter filter;
	int collectionId = 0;

	friend inline bool operator==(
		const Descriptor &,
		const Descriptor &) = default;
};

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer, int collectionId);
	~Memento();

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	void setListState(std::unique_ptr<ListState> state);
	std::unique_ptr<ListState> listState();

private:
	std::unique_ptr<ListState> _listState;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

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

	void setupNotifyCheckbox(int wasBottomHeight, bool enabled);
	void setupBottomButton(int wasBottomHeight);
	void refreshBottom();

	InnerWidget *_inner = nullptr;
	QPointer<Ui::SlideWrap<Ui::RpWidget>> _pinnedToBottom;
	rpl::variable<bool> _hasPinnedToBottom;
	rpl::variable<bool> _emptyCollectionShown;
	rpl::variable<Descriptor> _descriptor;
	std::optional<bool> _notifyEnabled;
	bool _shown = false;

};

[[nodiscard]] std::shared_ptr<Info::Memento> Make(
	not_null<PeerData*> peer,
	int collectionId = 0);

} // namespace Info::PeerGifts
