/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/timer.h"
#include "history/view/history_view_schedule_box.h"
#include "ui/chat/forward_options_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/round_checkbox.h"
#include "mtproto/sender.h"

class History;

namespace style {
struct MultiSelect;
struct InputField;
struct PeerList;
} // namespace style

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Window {
class SessionController;
} // namespace Window

namespace Api {
struct SendOptions;
} // namespace Api

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace Data {
enum class ForwardOptions;
class Thread;
} // namespace Data

namespace Ui {
class MultiSelect;
class InputField;
struct ScrollToRequest;
template <typename Widget>
class SlideWrap;
class PopupMenu;
} // namespace Ui

class ShareBox;

struct ShareBoxStyleOverrides {
	const style::MultiSelect *multiSelect = nullptr;
	const style::InputField *comment = nullptr;
	const style::PeerList *peerList = nullptr;
	const style::InputField *label = nullptr;
	const style::Checkbox *checkbox = nullptr;
	std::shared_ptr<HistoryView::ScheduleBoxStyleArgs> scheduleBox;
};
[[nodiscard]] ShareBoxStyleOverrides DarkShareBoxStyle();

void FastShareMessage(
	std::shared_ptr<Main::SessionShow> show,
	not_null<HistoryItem*> item,
	ShareBoxStyleOverrides st = {});
void FastShareMessage(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	ShareBoxStyleOverrides st = {});
void FastShareLink(
	not_null<Window::SessionController*> controller,
	const QString &url,
	ShareBoxStyleOverrides st = {});
void FastShareLink(
	std::shared_ptr<Main::SessionShow> show,
	const QString &url,
	ShareBoxStyleOverrides st = {});

struct RecipientMoneyRestrictionError;
[[nodiscard]] auto ShareMessageMoneyRestrictionError()
-> Fn<RecipientMoneyRestrictionError(not_null<UserData*>)>;

class ShareBox final : public Ui::BoxContent {
public:
	using CopyCallback = Fn<void()>;
	using CountMessagesCallback = Fn<int(const TextWithTags&)>;
	using SubmitCallback = Fn<void(
		std::vector<not_null<Data::Thread*>>&&,
		Fn<bool()> checkPaid,
		TextWithTags&&,
		Api::SendOptions,
		Data::ForwardOptions)>;
	using FilterCallback = Fn<bool(not_null<Data::Thread*>)>;

	[[nodiscard]] static auto DefaultForwardCountMessages(
		not_null<History*> history,
		MessageIdsList msgIds) -> CountMessagesCallback;
	[[nodiscard]] static SubmitCallback DefaultForwardCallback(
		std::shared_ptr<Ui::Show> show,
		not_null<History*> history,
		MessageIdsList msgIds,
		std::optional<TimeId> videoTimestamp = {});

	struct Descriptor {
		not_null<Main::Session*> session;
		CopyCallback copyCallback;
		CountMessagesCallback countMessagesCallback;
		SubmitCallback submitCallback;
		FilterCallback filterCallback;
		object_ptr<Ui::RpWidget> bottomWidget = { nullptr };
		rpl::producer<QString> copyLinkText;
		rpl::producer<QString> titleOverride;
		ShareBoxStyleOverrides st;
		std::optional<TimeId> videoTimestamp;
		struct {
			int sendersCount = 0;
			int captionsCount = 0;
			bool show = false;
		} forwardOptions;

		using MoneyRestrictionError = RecipientMoneyRestrictionError;
		Fn<MoneyRestrictionError(
			not_null<UserData*>)> moneyRestrictionError;
	};
	ShareBox(QWidget*, Descriptor &&descriptor);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void prepareCommentField();
	void scrollAnimationCallback();

	void submit(Api::SendOptions options);
	void copyLink() const;
	bool searchByUsername(bool useCache = false);

	[[nodiscard]] SendMenu::Details sendMenuDetails() const;

	void scrollTo(Ui::ScrollToRequest request);
	void needSearchByUsername();
	void applyFilterUpdate(const QString &query);
	void selectedChanged();
	void computeStarsCount();
	void createButtons();
	int getTopScrollSkip() const;
	int getBottomScrollSkip() const;
	int contentHeight() const;
	void updateScrollSkips();

	void addPeerToMultiSelect(not_null<Data::Thread*> thread);
	void innerSelectedChanged(not_null<Data::Thread*> thread, bool checked);

	void peopleDone(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	void peopleFail(const MTP::Error &error, mtpRequestId requestId);

	void showMenu(not_null<Ui::RpWidget*> parent);

	Descriptor _descriptor;
	MTP::Sender _api;

	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::SlideWrap<Ui::InputField>> _comment;
	object_ptr<Ui::RpWidget> _bottomWidget;

	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::ForwardOptions _forwardOptions;

	class Inner;
	QPointer<Inner> _inner;

	bool _hasSelected = false;
	rpl::variable<QString> _copyLinkText;
	rpl::variable<int> _starsToSend;

	base::Timer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull = false;
	mtpRequestId _peopleRequest = 0;

	RpWidget *_chatsFilters = nullptr;

	using PeopleCache = QMap<QString, MTPcontacts_Found>;
	PeopleCache _peopleCache;

	using PeopleQueries = QMap<mtpRequestId, QString>;
	PeopleQueries _peopleQueries;

	Ui::Animations::Simple _scrollAnimation;
	rpl::lifetime _submitLifetime;

};
