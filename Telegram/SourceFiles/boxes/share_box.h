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
#include "mtproto/sender.h"

namespace style {
struct MultiSelect;
struct InputField;
struct PeerList;
} // namespace style

namespace SendMenu {
enum class Type;
} // namespace SendMenu

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

class ShareBox final : public Ui::BoxContent {
public:
	using CopyCallback = Fn<void()>;
	using SubmitCallback = Fn<void(
		std::vector<not_null<PeerData*>>&&,
		TextWithTags&&,
		Api::SendOptions)>;
	using FilterCallback = Fn<bool(PeerData*)>;

	struct Descriptor {
		not_null<Main::Session*> session;
		CopyCallback copyCallback;
		SubmitCallback submitCallback;
		FilterCallback filterCallback;
		Window::SessionNavigation *navigation = nullptr;
		Fn<void(not_null<Ui::InputField*>)> initSpellchecker;
		Fn<void(not_null<Ui::InputField*>)> initEditLink;
		object_ptr<Ui::RpWidget> bottomWidget = { nullptr };
		rpl::producer<QString> copyLinkText;
		const style::MultiSelect *stMultiSelect = nullptr;
		const style::InputField *stComment = nullptr;
		const style::PeerList *st = nullptr;
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
	void copyLink();
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

	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);
	void innerSelectedChanged(PeerData *peer, bool checked);

	void peopleDone(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	void peopleFail(const MTP::Error &error, mtpRequestId requestId);

	Descriptor _descriptor;
	MTP::Sender _api;

	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::SlideWrap<Ui::InputField>> _comment;
	object_ptr<Ui::RpWidget> _bottomWidget;

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
