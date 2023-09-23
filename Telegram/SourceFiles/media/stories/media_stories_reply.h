/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class History;
enum class SendMediaType;

namespace Api {
struct SendAction;
struct SendOptions;
struct MessageToSend;
} // namespace Api

namespace Data {
struct ReactionId;
} // namespace Data

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace HistoryView::Controls {
struct VoiceToSend;
} // namespace HistoryView::Controls

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct PreparedList;
class SendFilesWay;
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Controller;

struct ReplyAreaData {
	PeerData *peer = nullptr;
	StoryId id = 0;

	friend inline auto operator<=>(ReplyAreaData, ReplyAreaData) = default;
	friend inline bool operator==(ReplyAreaData, ReplyAreaData) = default;
};

class ReplyArea final : public base::has_weak_ptr {
public:
	explicit ReplyArea(not_null<Controller*> controller);
	~ReplyArea();

	void show(
		ReplyAreaData data,
		rpl::producer<Data::ReactionId> likedValue);
	void sendReaction(const Data::ReactionId &id);

	[[nodiscard]] bool focused() const;
	[[nodiscard]] rpl::producer<bool> focusedValue() const;
	[[nodiscard]] rpl::producer<bool> activeValue() const;
	[[nodiscard]] rpl::producer<bool> hasSendTextValue() const;

	[[nodiscard]] bool ignoreWindowMove(QPoint position) const;
	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	[[nodiscard]] not_null<Ui::RpWidget*> likeAnimationTarget() const;

private:
	class Cant;

	using VoiceToSend = HistoryView::Controls::VoiceToSend;

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<History*> history() const;

	void send(
		Api::MessageToSend message,
		Api::SendOptions options,
		bool skipToast = false);

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Ui::PreparedList &list) const;
	bool showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const;
	void sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter);
	void finishSending(bool skipToast = false);

	void sendExistingDocument(not_null<DocumentData*> document);
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId);
	void sendExistingPhoto(not_null<PhotoData*> photo);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId);

	void initGeometry();
	void initActions();

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void send(Api::SendOptions options);
	void sendVoice(VoiceToSend &&data);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);

	void showPremiumToast(not_null<DocumentData*> emoji);

	const not_null<Controller*> _controller;
	const std::unique_ptr<HistoryView::ComposeControls> _controls;
	std::unique_ptr<Cant> _cant;

	ReplyAreaData _data;
	base::has_weak_ptr _shownPeerGuard;
	bool _chooseAttachRequest = false;
	rpl::variable<bool> _choosingAttach;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
