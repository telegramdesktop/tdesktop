/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;
class QOpenGLFunctions;

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {
class Members;
class Viewport;
class MessagesUi;
} // namespace Calls::Group

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
class GroupCall;
struct MessageReactionsTopPaid;
} // namespace Data

namespace Ui {
class RpWidgetWrap;
} // namespace Ui

namespace Ui::GL {
enum class Backend;
} // namespace Ui::GL

namespace Media::View {

[[nodiscard]] auto TopVideoStreamDonors(not_null<Calls::GroupCall*> call)
-> rpl::producer<std::vector<Data::MessageReactionsTopPaid>>;

class VideoStream final {
public:
	VideoStream(
		not_null<QWidget*> parent,
		not_null<Ui::RpWidgetWrap*> borrowedRp,
		bool borrowedOpenGL,
		Ui::GL::Backend backend,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::GroupCall> call,
		QString callLinkSlug,
		MsgId callJoinMessageId);
	~VideoStream();

	[[nodiscard]] not_null<Calls::GroupCall*> call() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;

	void setVolume(float64 volume);
	void updateGeometry(int x, int y, int width, int height);
	void toggleCommentsOn(rpl::producer<bool> shown);

	void ensureBorrowedRenderer(QOpenGLFunctions &f);
	void borrowedPaint(QOpenGLFunctions &f);

	void ensureBorrowedRenderer();
	void borrowedPaint(Painter &p, const QRegion &clip);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class Delegate;
	class Loading;

	void setupVideo();
	void setupMembers();
	void setupMessages();

	rpl::variable<bool> _commentsShown;

	std::shared_ptr<ChatHelpers::Show> _show;
	std::unique_ptr<Delegate> _delegate;
	std::unique_ptr<Loading> _loading;
	std::unique_ptr<Calls::GroupCall> _call;
	std::unique_ptr<Calls::Group::Members> _members;
	std::unique_ptr<Calls::Group::Viewport> _viewport;
	std::unique_ptr<Calls::Group::MessagesUi> _messages;

	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;

};

} // namespace Media::View
