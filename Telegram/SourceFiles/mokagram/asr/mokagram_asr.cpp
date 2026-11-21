#include "mokagram/asr/mokagram_asr.h"

#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "history/history_item.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_file_origin.h"
#include "ui/image/image_location.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "media/media_audio_waveform.h"

extern "C" {
#include "tinyasr.h"
}

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace Mokagram::Asr {
namespace {

using namespace FFmpeg;

constexpr auto kKeyEnabled = "asr.enabled";
constexpr auto kKeyAutoNew = "asr.autoNew";
constexpr auto kAsrModelFile = "en-ru-uk.tzf";
constexpr auto kVadModelFile = "silero_vad.onnx";
constexpr auto kSampleRate = 16000;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(Lang::Tr(text));
}

[[nodiscard]] QString ModelDir() {
	return cWorkingDir() + u"tdata/asr"_q;
}

// Распаковываем gzip из ресурса в каталог профиля один раз. Файл модели
// большой, поэтому держим её на диске, а не в Qt-ресурсе: так движок
// расходует меньше памяти и не копирует 38 МБ при каждом старте.
bool EnsureFile(const QString &resource, const QString &target) {
	if (QFile::exists(target)) {
		return true;
	}
	QFile input(resource);
	if (!input.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto compressed = input.readAll();
	z_stream stream{};
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
		return false;
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
	stream.avail_in = compressed.size();
	QFile output(target);
	if (!output.open(QIODevice::WriteOnly)) {
		return false;
	}
	char buffer[64 * 1024];
	while (true) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer);
		stream.avail_out = sizeof(buffer);
		const auto result = inflate(&stream, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END) {
			return false;
		}
		const auto written = sizeof(buffer) - stream.avail_out;
		if (output.write(buffer, written) != qint64(written)) {
			return false;
		}
		if (result == Z_STREAM_END) {
			break;
		}
	}
	return output.flush();
}

// Decode an Ogg/Opus voice message into mono float PCM at 16 kHz, which is
// the tinyasr input format. Uses the same FFmpeg helpers as the client's
// waveform decoder, but resamples to float and to the model's sample rate.
std::vector<float> DecodePcm(const QByteArray &bytes) {
	std::vector<float> result;
	if (bytes.isEmpty()) {
		return result;
	}
	auto bytesWrap = FFmpeg::ReadBytesWrap{
		.size = bytes.size(),
		.data = reinterpret_cast<const uchar*>(bytes.constData()),
	};
	auto format = FFmpeg::MakeFormatPointer(
		&bytesWrap,
		&FFmpeg::ReadBytesWrap::Read,
		nullptr,
		&FFmpeg::ReadBytesWrap::Seek);
	if (!format) {
		return result;
	}
	if (avformat_find_stream_info(format.get(), nullptr) < 0) {
		return result;
	}
	const auto streamIndex = av_find_best_stream(
		format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (streamIndex < 0) {
		return result;
	}
	auto codec = FFmpeg::MakeCodecPointer({ .stream = format->streams[streamIndex] });
	if (!codec) {
		return result;
	}
	auto frame = FFmpeg::MakeFramePointer();
	if (!frame) {
		return result;
	}
	auto swr = FFmpeg::SwresamplePointer();
	auto packet = FFmpeg::Packet();
	auto eof = false;
	while (true) {
		const auto received = avcodec_receive_frame(codec.get(), frame.get());
		if (received == 0) {
			auto mono = AVChannelLayout AV_CHANNEL_LAYOUT_MONO;
			swr = FFmpeg::MakeSwresamplePointer(
				&frame->ch_layout,
				AVSampleFormat(frame->format),
				frame->sample_rate,
				&mono,
				AV_SAMPLE_FMT_FLT,
				kSampleRate,
				&swr);
			if (!swr) {
				return {};
			}
			const auto capacity = swr_get_out_samples(swr.get(), frame->nb_samples);
			if (capacity <= 0) {
				continue;
			}
			const auto oldSize = result.size();
			result.resize(oldSize + capacity);
			auto out = reinterpret_cast<uint8_t*>(result.data() + oldSize);
			const auto samples = swr_convert(
				swr.get(), &out, capacity,
				(const uint8_t**)frame->extended_data, frame->nb_samples);
			if (samples < 0) {
				return {};
			}
			result.resize(oldSize + samples);
			continue;
		} else if (received == AVERROR_EOF) {
			break;
		} else if (received != AVERROR(EAGAIN)) {
			return {};
		}
		if (eof) {
			if (avcodec_send_packet(codec.get(), nullptr) < 0) {
				return {};
			}
			continue;
		}
		av_packet_unref(&packet.fields());
		const auto read = av_read_frame(format.get(), &packet.fields());
		if (read == AVERROR_EOF) {
			eof = true;
			continue;
		} else if (read < 0) {
			return {};
		} else if (packet.fields().stream_index != streamIndex) {
			continue;
		}
		if (avcodec_send_packet(codec.get(), &packet.fields()) < 0) {
			return {};
		}
	}
	return result;
}

// The recognizer emits a JSON array of final words per file piece:
//   [{"text":"привет","start":0.12,"end":0.5}, ...]
// There are no "commit" wrappers here: those belong to session events.
QString ParseTranscript(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isArray()) {
		return QString();
	}
	auto result = QString();
	auto first = true;
	for (const auto &value : document.array()) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		const auto text = object.value(u"text"_q).toString();
		if (text.isEmpty()) {
			continue;
		}
		// "join": true means no space before the word (Khmer syllables).
		if (!first && !object.value(u"join"_q).toBool()) {
			result.append(u" "_q);
		}
		result.append(text);
		first = false;
	}
	return result.trimmed();
}

struct Task {
	FullMsgId id;
	QByteArray bytes;
	base::weak_ptr<Main::Session> session;

	Task(FullMsgId id, QByteArray bytes, not_null<Main::Session*> session)
	: id(id), bytes(std::move(bytes)), session(base::make_weak(session)) {
	}
};

bool AutoEnabled() {
	return Settings::Bool(kKeyEnabled) && Settings::Bool(kKeyAutoNew);
}

void TranscribeNewMessage(const Data::MessageUpdate &update) {
	const auto item = update.item;
	if (!AutoEnabled() || !item->isRegular() || !IsServerMsgId(item->id)) {
		return;
	}
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document || !document->isVoiceMessage() || media->ttlSeconds()) {
		return;
	}
	const auto session = &document->session();
	const auto id = item->fullId();
	auto &engine = Engine::Instance();
	if (engine.result(id)) {
		return;
	}
	if (const auto view = document->activeMediaView()) {
		const auto bytes = view->bytes();
		if (!bytes.isEmpty()) {
			engine.transcribe(session, id, bytes);
			return;
		}
	}

	// Upload stores the recording here even if its message has no media view.
	const auto weak = base::make_weak(session);
	session->data().cache().get(document->cacheKey(), [=](QByteArray &&bytes) {
		if (bytes.isEmpty() || bytes.startsWith("partial:")) {
			return;
		}
		crl::on_main(weak, [=, bytes = std::move(bytes)] {
			const auto current = session->data().message(id);
			const auto media = current ? current->media() : nullptr;
			if (AutoEnabled() && media && media->document() == document
				&& !media->ttlSeconds()) {
				Engine::Instance().transcribe(session, id, bytes);
			}
		});
	});
}

} // namespace

class Engine::Impl final {
public:
	Impl() = default;
	~Impl() {
		{
			std::lock_guard lock(_mutex);
			_stop = true;
		}
		_queueCondition.notify_one();
		if (_thread.joinable()) {
			_thread.join();
		}
		std::lock_guard lock(_mutex);
		if (_models) {
			tinyasr_free(_models);
		}
	}

	bool init() {
		std::lock_guard lock(_mutex);
		if (_initialized) {
			return _models != nullptr;
		}
		_initialized = true;
		const auto dir = ModelDir();
		if (!QDir().mkpath(dir)) {
			LOG(("[mokagram-asr] cannot create model directory."));
			return false;
		}
		const auto asrPath = dir + '/' + kAsrModelFile;
		const auto vadPath = dir + '/' + kVadModelFile;
		if (!EnsureFile(u":/asr/"_q + kAsrModelFile + u".gz"_q, asrPath)
			|| !EnsureFile(u":/asr/"_q + kVadModelFile + u".gz"_q, vadPath)) {
			LOG(("[mokagram-asr] cannot unpack model files."));
			return false;
		}
		QFile asrFile(asrPath);
		QFile vadFile(vadPath);
		if (!asrFile.open(QIODevice::ReadOnly) || !vadFile.open(QIODevice::ReadOnly)) {
			return false;
		}
		const auto asrBytes = asrFile.readAll();
		const auto vadBytes = vadFile.readAll();
		char error[512] = {};
		_models = tinyasr_load(
			reinterpret_cast<const uint8_t*>(asrBytes.constData()), asrBytes.size(),
			reinterpret_cast<const uint8_t*>(vadBytes.constData()), vadBytes.size(),
			error, sizeof(error));
		if (!_models) {
			LOG(("[mokagram-asr] model load failed: %1").arg(QString::fromLatin1(error)));
			return false;
		}
		// The vendored crate is built without std: its parallel-for and
		// built-in thread pool are compiled out, so this is a no-op.
		// Recognition still runs on our own background thread.
		_thread = std::thread([this] { Loop(); });
		return true;
	}

	[[nodiscard]] bool ready() const {
		std::lock_guard lock(_mutex);
		return _models != nullptr;
	}

	void enqueue(Task task) {
		{
			std::lock_guard lock(_mutex);
			if (_results.contains(task.id) || !_pending.emplace(task.id).second) {
				return;
			}
			_queue.push_back(std::move(task));
		}
		_queueCondition.notify_one();
	}

	[[nodiscard]] std::optional<Result> result(FullMsgId id) const {
		std::lock_guard lock(_mutex);
		const auto i = _results.find(id);
		return (i == _results.end()) ? std::nullopt : std::optional<Result>(i->second);
	}

	void markShown(FullMsgId id) {
		std::lock_guard lock(_mutex);
		auto i = _results.find(id);
		if (i != _results.end()) {
			i->second.shown = true;
		}
	}

private:
	void Loop() {
		while (true) {
			std::optional<Task> current;
			{
				std::unique_lock lock(_mutex);
				_queueCondition.wait(lock, [&] { return _stop || !_queue.empty(); });
				if (_stop) {
					return;
				}
				current.emplace(std::move(_queue.front()));
				_queue.pop_front();
			}
			const auto task = &*current;
			auto transcript = QString();
			auto failed = false;
			do {
				const auto pcm = DecodePcm(task->bytes);
				if (pcm.empty()) {
					failed = true;
					break;
				}
				auto *file = tinyasr_file_new(
					_models, pcm.data(), pcm.size(), "ru,en", 4);
				if (!file) {
					failed = true;
					break;
				}
				while (const auto *piece = tinyasr_file_next(file)) {
					const auto text = ParseTranscript(QByteArray(piece));
					if (text.isEmpty()) {
						continue;
					}
					if (!transcript.isEmpty()) {
						transcript.append(u" "_q);
					}
					transcript.append(text);
				}
				tinyasr_file_free(file);
				transcript = transcript.trimmed();
				failed = transcript.isEmpty();
			} while (false);

			{
				std::lock_guard lock(_mutex);
				_pending.erase(task->id);
				_results[task->id] = Result{ .text = transcript, .failed = failed };
			}
			crl::on_main(task->session, [id = task->id, session = task->session] {
				const auto strong = session.get();
				if (const auto item = strong->data().message(id)) {
					strong->data().requestItemResize(item);
				}
			});
		}
	}

	mutable std::mutex _mutex;
	std::condition_variable _queueCondition;
	bool _stop = false;
	bool _initialized = false;
	std::deque<Task> _queue;
	std::set<FullMsgId> _pending;
	std::map<FullMsgId, Result> _results;
	tinyasr_models *_models = nullptr;
	std::thread _thread;
};

Engine::Engine() : _impl(std::make_unique<Impl>()) {
}
Engine::~Engine() = default;

Engine &Engine::Instance() {
	static Engine instance;
	return instance;
}

bool Engine::init() {
	return _impl->init();
}

bool Engine::ready() const {
	return _impl->ready();
}

void Engine::transcribe(not_null<Main::Session*> session, FullMsgId id, const QByteArray &bytes) {
	if (!ready() || bytes.isEmpty()) {
		return;
	}
	_impl->enqueue(Task(id, bytes, session));
}

std::optional<Result> Engine::result(FullMsgId id) const {
	return _impl->result(id);
}

void Engine::markShown(FullMsgId id) {
	_impl->markShown(id);
}

void RegisterSettings() {
	Settings::Register({
		.key = kKeyEnabled,
		.section = U(Lang::Tr("Распознавание речи")),
		.title = U(Lang::Tr("Распознавать голосовые сообщения на устройстве")),
		.description = U(Lang::Tr("Текст появляется под голосовым сообщением. Звук никуда не отправляется, модель лежит в клиенте.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = true,
		.category = u"chats"_q,
		.sectionIcon = u"mic"_q,
	});
	Settings::Register({
		.key = kKeyAutoNew,
		.section = U(Lang::Tr("Распознавание речи")),
		.title = U(Lang::Tr("Распознавать новые сообщения автоматически")),
		.kind = Settings::Kind::Bool,
		.defaultValue = true,
		.dependsOn = kKeyEnabled,
		.category = u"chats"_q,
		.sectionIcon = u"mic"_q,
	});
}

void Start() {
	if (!Settings::Bool(kKeyEnabled)) {
		return;
	}
	if (!Engine::Instance().init()) {
		Settings::SetBool(kKeyEnabled, false);
		return;
	}
	static auto lifetime = rpl::lifetime();
	Core::App().domain().activeSessionValue(
	) | rpl::map([](Main::Session *session) {
		return session ? session->changes().messageUpdates(
			Data::MessageUpdate::Flag::NewAdded
			| Data::MessageUpdate::Flag::NewMaybeAdded
		) : rpl::never<Data::MessageUpdate>();
	}) | rpl::flatten_latest(
	) | rpl::on_next(TranscribeNewMessage, lifetime);
}

} // namespace Mokagram::Asr
