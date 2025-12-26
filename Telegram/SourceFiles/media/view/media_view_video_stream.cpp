/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_video_stream.h"

#include "data/data_message_reactions.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_members.h"
#include "calls/group/calls_group_messages.h"
#include "calls/group/calls_group_messages_ui.h"
#include "calls/group/calls_group_viewport.h"
#include "calls/calls_instance.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "payments/ui/payments_reaction_box.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "styles/style_calls.h"
#include "styles/style_media_view.h"

namespace Media::View {

class VideoStream::Delegate final : public Calls::GroupCall::Delegate {
public:
	explicit Delegate(Fn<void()> close);

	void groupCallFinished(not_null<Calls::GroupCall*> call) override;
	void groupCallFailed(not_null<Calls::GroupCall*> call) override;
	void groupCallRequestPermissionsOrFail(Fn<void()> onSuccess) override;
	void groupCallPlaySound(GroupCallSound sound) override;
	auto groupCallGetVideoCapture(const QString &deviceId)
		-> std::shared_ptr<tgcalls::VideoCaptureInterface> override;
	FnMut<void()> groupCallAddAsyncWaiter() override;

private:
	Fn<void()> _close;

};

class VideoStream::Loading final {
public:
	Loading(not_null<QWidget*> parent, not_null<VideoStream*> stream);

	void lower();
	void setGeometry(int x, int y, int width, int height);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setup(not_null<QWidget*> parent);

	const not_null<VideoStream*> _stream;
	std::unique_ptr<Ui::RpWidget> _bg;
	std::unique_ptr<Ui::PathShiftGradient> _gradient;

};

auto TopVideoStreamDonors(not_null<Calls::GroupCall*> call)
-> rpl::producer<std::vector<Data::MessageReactionsTopPaid>> {
	const auto messages = call->messages();
	return rpl::single(rpl::empty) | rpl::then(
		messages->starsValueChanges() | rpl::to_empty
	) | rpl::map([=] {
		const auto &list = messages->starsTop().topDonors;
		auto still = Ui::MaxTopPaidDonorsShown();
		auto result = std::vector<Data::MessageReactionsTopPaid>();
		result.reserve(list.size());
		for (const auto &item : list) {
			result.push_back({
				.peer = item.peer,
				.count = uint32(item.stars),
				.my = item.my ? 1U : 0U,
			});
			if (!item.my && !--still) {
				break;
			}
		}
		return result;
	});
}

auto TopDonorPlaces(not_null<Calls::GroupCall*> call)
-> rpl::producer<std::vector<not_null<PeerData*>>> {
	return TopVideoStreamDonors(
		call
	) | rpl::map([=](const std::vector<Data::MessageReactionsTopPaid> &lst) {
		auto result = std::vector<not_null<PeerData*>>();
		auto left = Ui::MaxTopPaidDonorsShown();
		result.reserve(lst.size());
		for (const auto &donor : lst) {
			result.push_back(donor.peer);
			if (!--left) {
				break;
			}
		}
		return result;
	});
}

VideoStream::Delegate::Delegate(Fn<void()> close)
: _close(std::move(close)) {
}

void VideoStream::Delegate::groupCallFinished(
		not_null<Calls::GroupCall*> call) {
	crl::on_main(call, _close);
}

void VideoStream::Delegate::groupCallFailed(not_null<Calls::GroupCall*> call) {
	crl::on_main(call, _close);
}

void VideoStream::Delegate::groupCallRequestPermissionsOrFail(
		Fn<void()> onSuccess) {
}

void VideoStream::Delegate::groupCallPlaySound(GroupCallSound sound) {
}

auto VideoStream::Delegate::groupCallGetVideoCapture(const QString &deviceId)
-> std::shared_ptr<tgcalls::VideoCaptureInterface> {
	return nullptr;
}

FnMut<void()> VideoStream::Delegate::groupCallAddAsyncWaiter() {
	return [] {};
}

VideoStream::Loading::Loading(
	not_null<QWidget*> parent,
	not_null<VideoStream*> stream)
: _stream(stream) {
	setup(parent);
}

void VideoStream::Loading::lower() {
	_bg->lower();
}

void VideoStream::Loading::setGeometry(int x, int y, int width, int height) {
	_bg->setGeometry(x, y, width, height);
}

rpl::lifetime &VideoStream::Loading::lifetime() {
	return _bg->lifetime();
}

void VideoStream::Loading::setup(not_null<QWidget*> parent) {
	_bg = std::make_unique<Ui::RpWidget>(parent);

	_gradient = std::make_unique<Ui::PathShiftGradient>(
		st::storiesComposeBg,
		st::storiesComposeBgRipple,
		[=] { _bg->update(); });

	_bg->show();
	_bg->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_bg.get());
		auto hq = PainterHighQualityEnabler(p);

		_gradient->startFrame(0, _bg->width(), _bg->width() / 3);
		_gradient->paint([&](const Ui::PathShiftGradient::Background &bg) {
			const auto stroke = style::ConvertScaleExact(2);
			if (const auto color = std::get_if<style::color>(&bg)) {
				auto pen = (*color)->p;
				pen.setWidthF(stroke);
				p.setPen(pen);
				p.setBrush(*color);
			} else {
				const auto gradient = v::get<QLinearGradient*>(bg);

				auto copy = *gradient;
				copy.setStops({
					{ 0., st::storiesComposeBg->c },
					{ 0.5, st::white->c },
					{ 1., st::storiesComposeBg->c },
				});
				auto pen = QPen(QBrush(copy), stroke);
				p.setPen(pen);
				p.setBrush(*gradient);
			}
			const auto half = stroke / 2.;
			const auto remove = QMarginsF(half, half, half, half);
			const auto rect = QRectF(_bg->rect()).marginsRemoved(remove);
			const auto radius = st::storiesRadius - half;
			p.drawRoundedRect(rect, radius, radius);
			return true;
		});
	}, _bg->lifetime());
}

VideoStream::VideoStream(
	not_null<QWidget*> parent,
	not_null<Ui::RpWidgetWrap*> borrowedRp,
	bool borrowedOpenGL,
	Ui::GL::Backend backend,
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::GroupCall> call,
	QString callLinkSlug,
	MsgId callJoinMessageId)
: _show(std::move(show))
, _delegate(std::make_unique<Delegate>([=] { _closeRequests.fire({}); }))
, _loading(std::make_unique<Loading>(parent, this))
, _call(std::make_unique<Calls::GroupCall>(
	_delegate.get(),
	Calls::StartConferenceInfo{
		.show = _show,
		.call = std::move(call),
		.linkSlug = callLinkSlug,
		.joinMessageId = callJoinMessageId,
	}))
, _members(
	std::make_unique<Calls::Group::Members>(
		parent,
		_call.get(),
		Calls::Group::PanelMode::VideoStream,
		backend))
, _viewport(
	std::make_unique<Calls::Group::Viewport>(
		parent,
		Calls::Group::PanelMode::VideoStream,
		backend,
		borrowedRp,
		borrowedOpenGL))
, _messages(
	std::make_unique<Calls::Group::MessagesUi>(
		parent,
		_show,
		Calls::Group::MessagesMode::VideoStream,
		_call->messages()->listValue(),
		TopDonorPlaces(_call.get()),
		_call->messages()->idUpdates(),
		_call->canManageValue(),
		_commentsShown.value())) {
	Core::App().calls().registerVideoStream(_call.get());
	setupMembers();
	setupVideo();
	setupMessages();
}

VideoStream::~VideoStream() {
}

not_null<Calls::GroupCall*> VideoStream::call() const {
	return _call.get();
}

rpl::producer<> VideoStream::closeRequests() const {
	return _closeRequests.events();
}

void VideoStream::updateGeometry(int x, int y, int width, int height) {
	const auto skip = st::groupCallMessageSkip;
	_viewport->setGeometry(false, { x, y, width, height });
	_messages->move(x + skip, y + height, width - 2 * skip, height / 2);
	if (_loading) {
		_loading->setGeometry(x, y, width, height);
	}
}

void VideoStream::toggleCommentsOn(rpl::producer<bool> shown) {
	_commentsShown = std::move(shown);
}

void VideoStream::ensureBorrowedRenderer(QOpenGLFunctions &f) {
	_viewport->ensureBorrowedRenderer(f);
}

void VideoStream::borrowedPaint(QOpenGLFunctions &f) {
	_viewport->borrowedPaint(f);
}

void VideoStream::ensureBorrowedRenderer() {
	_viewport->ensureBorrowedRenderer();
}

void VideoStream::borrowedPaint(Painter &p, const QRegion &clip) {
	_viewport->borrowedPaint(p, clip);
}

rpl::lifetime &VideoStream::lifetime() {
	return _lifetime;
}

void VideoStream::setupMembers() {
}

void VideoStream::setupVideo() {
	const auto setupTile = [=](
			const Calls::VideoEndpoint &endpoint,
			const std::unique_ptr<Calls::GroupCall::VideoTrack> &track) {
		using namespace rpl::mappers;
		const auto row = endpoint.rtmp()
			? _members->rtmpFakeRow(Calls::GroupCall::TrackPeer(track)).get()
			: _members->lookupRow(Calls::GroupCall::TrackPeer(track));
		Assert(row != nullptr);

		auto pinned = rpl::combine(
			_call->videoEndpointLargeValue(),
			_call->videoEndpointPinnedValue()
		) | rpl::map(_1 == endpoint && _2);
		const auto self = (endpoint.peer == _call->joinAs());
		_viewport->add(
			endpoint,
			Calls::Group::VideoTileTrack{
				Calls::GroupCall::TrackPointer(track),
				row,
			},
			Calls::GroupCall::TrackSizeValue(track),
			std::move(pinned),
			self);
	};
	for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
		setupTile(endpoint, track);
	}
	_call->videoStreamActiveUpdates(
	) | rpl::on_next([=](const Calls::VideoStateToggle &update) {
		if (update.value) {
			// Add async (=> the participant row is definitely in Members).
			const auto endpoint = update.endpoint;
			crl::on_main(_viewport->widget(), [=] {
				const auto &tracks = _call->activeVideoTracks();
				const auto i = tracks.find(endpoint);
				if (i != end(tracks)) {
					setupTile(endpoint, i->second);
				}
			});
		} else {
			// Remove sync.
			_viewport->remove(update.endpoint);
		}
	}, _viewport->lifetime());

	_viewport->qualityRequests(
	) | rpl::on_next([=](const Calls::VideoQualityRequest &request) {
		_call->requestVideoQuality(request.endpoint, request.quality);
	}, _viewport->lifetime());

	_loading->lower();
	_viewport->widget()->lower();
	_viewport->setControlsShown(0.);

	_call->hasVideoWithFramesValue(
	) | rpl::on_next([=](bool has) {
		if (has) {
			_loading = nullptr;
		}
	}, _loading->lifetime());

	setVolume(Core::App().settings().videoVolume());
}

void VideoStream::setupMessages() {
	_messages->hiddenShowRequested() | rpl::on_next([=] {
		_call->messages()->requestHiddenShow();
	}, _messages->lifetime());


	_messages->deleteRequests(
	) | rpl::on_next([=](Calls::Group::MessageDeleteRequest event) {
		_call->messages()->deleteConfirmed(event);
	}, _messages->lifetime());
}

void VideoStream::setVolume(float64 volume) {
	const auto value = volume * Calls::Group::kDefaultVolume;
	_call->changeVolume({
		.peer = _call->peer(),
		.volume = int(base::SafeRound(value)),
	});
}

} // namespace Media::View
