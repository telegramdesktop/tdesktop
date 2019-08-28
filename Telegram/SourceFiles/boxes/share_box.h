/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/observer.h"
#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/effects/round_checkbox.h"

enum class SendMenuType;

namespace Window {
class SessionNavigation;
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

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class MultiSelect;
class InputField;
struct ScrollToRequest;
template <typename Widget>
class SlideWrap;
} // namespace Ui

QString AppendShareGameScoreUrl(
	not_null<Main::Session*> session,
	const QString &url,
	const FullMsgId &fullId);
void ShareGameScoreByHash(
	not_null<Main::Session*> session,
	const QString &hash);

class ShareBox : public BoxContent, public RPCSender {
public:
	using CopyCallback = Fn<void()>;
	using SubmitCallback = Fn<void(
		QVector<PeerData*>&&,
		TextWithTags&&,
		Api::SendOptions)>;
	using FilterCallback = Fn<bool(PeerData*)>;

	ShareBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		CopyCallback &&copyCallback,
		SubmitCallback &&submitCallback,
		FilterCallback &&filterCallback);

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
	void copyLink();
	bool searchByUsername(bool useCache = false);

	SendMenuType sendMenuType() const;

	void scrollTo(Ui::ScrollToRequest request);
	void needSearchByUsername();
	void applyFilterUpdate(const QString &query);
	void selectedChanged();
	void createButtons();
	int getTopScrollSkip() const;
	int getBottomScrollSkip() const;
	int contentHeight() const;
	void updateScrollSkips();

	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);
	void innerSelectedChanged(PeerData *peer, bool checked);

	void peopleReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	bool peopleFailed(const RPCError &error, mtpRequestId requestId);

	const not_null<Window::SessionNavigation*> _navigation;

	CopyCallback _copyCallback;
	SubmitCallback _submitCallback;
	FilterCallback _filterCallback;

	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::SlideWrap<Ui::InputField>> _comment;

	class Inner;
	QPointer<Inner> _inner;

	bool _hasSelected = false;

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
