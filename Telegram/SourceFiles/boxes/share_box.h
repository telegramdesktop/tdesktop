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
enum class Type;
} // namespace SendMenu

namespace Window {
class SessionController;
} // namespace Window

namespace Api {
struct SendOptions;
} // namespace Api

namespace Main {
class Session;
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

QString AppendShareGameScoreUrl(
	not_null<Main::Session*> session,
	const QString &url,
	const FullMsgId &fullId);
void ShareGameScoreByHash(
	not_null<Window::SessionController*> controller,
	const QString &hash);
void FastShareMessage(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

class ShareBox final : public Ui::BoxContent {
public:
	using CopyCallback = Fn<void()>;
	using SubmitCallback = Fn<void(
		std::vector<not_null<Data::Thread*>>&&,
		TextWithTags&&,
		Api::SendOptions,
		Data::ForwardOptions)>;
	using FilterCallback = Fn<bool(not_null<Data::Thread*>)>;

	[[nodiscard]] static SubmitCallback DefaultForwardCallback(
		std::shared_ptr<Ui::Show> show,
		not_null<History*> history,
		MessageIdsList msgIds);

	struct Descriptor {
		not_null<Main::Session*> session;
		CopyCallback copyCallback;
		SubmitCallback submitCallback;
		FilterCallback filterCallback;
		object_ptr<Ui::RpWidget> bottomWidget = { nullptr };
		rpl::producer<QString> copyLinkText;
		const style::MultiSelect *stMultiSelect = nullptr;
		const style::InputField *stComment = nullptr;
		const style::PeerList *st = nullptr;
		const style::InputField *stLabel = nullptr;
		struct {
			int messagesCount = 0;
			bool show = false;
			bool hasCaptions = false;
		} forwardOptions;
		HistoryView::ScheduleBoxStyleArgs scheduleBoxStyle;
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
	void submitSilent();
	void submitScheduled();
	void submitWhenOnline();
	void copyLink() const;
	bool searchByUsername(bool useCache = false);

	SendMenu::Type sendMenuType() const;

	void scrollTo(Ui::ScrollToRequest request);
	void needSearchByUsername();
	void applyFilterUpdate(const QString &query);
	void selectedChanged();
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

	base::Timer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull = false;
	mtpRequestId _peopleRequest = 0;

	using PeopleCache = QMap<QString, MTPcontacts_Found>;
	PeopleCache _peopleCache;

	using PeopleQueries = QMap<mtpRequestId, QString>;
	PeopleQueries _peopleQueries;

	Ui::Animations::Simple _scrollAnimation;

};
