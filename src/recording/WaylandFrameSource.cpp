#include "recording/WaylandFrameSource.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace rr {

// QObject helper that captures one xdg-desktop-portal Request "Response" signal
// (u response, a{sv} results) and quits a local event loop. Lives on the worker
// thread that runs the handshake; AUTOMOC compiles it via the .moc include below.
class PortalResponseHandler : public QObject {
    Q_OBJECT
public:
    uint code = 1;  // default nonzero = treat "no response" as failure
    QVariantMap results;
    QEventLoop loop;
public slots:
    void onResponse(uint c, const QVariantMap& r) {
        code = c;
        results = r;
        loop.quit();
    }
};

namespace {

long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Portal session_handle arrives in results as either a string or an object path
// depending on the backend; accept both and return the path string.
QString sessionHandleFromResults(const QVariantMap& results) {
    const QVariant v = results.value(QStringLiteral("session_handle"));
    if (v.canConvert<QDBusObjectPath>()) return v.value<QDBusObjectPath>().path();
    return v.toString();
}

// PipeWire stream callback table, built once. Designated initializers are not
// standard C++17, so populate the struct via an immediately-invoked lambda.
const pw_stream_events* streamEventsTable();

void onParamChanged(void* data, uint32_t id, const struct spa_pod* param) {
    auto* self = static_cast<WaylandFrameSource*>(data);
    if (!param || id != SPA_PARAM_Format) return;
    struct spa_video_info_raw info;
    std::memset(&info, 0, sizeof(info));
    if (spa_format_video_raw_parse(param, &info) < 0) return;
    self->onFormatChanged(static_cast<int>(info.size.width),
                          static_cast<int>(info.size.height),
                          static_cast<int>(info.format));
}

void onProcessTrampoline(void* data) {
    static_cast<WaylandFrameSource*>(data)->onProcess();
}

const pw_stream_events* streamEventsTable() {
    static pw_stream_events ev = [] {
        pw_stream_events e;
        std::memset(&e, 0, sizeof(e));
        e.version = PW_VERSION_STREAM_EVENTS;
        e.param_changed = onParamChanged;
        e.process = onProcessTrampoline;
        return e;
    }();
    return &ev;
}

constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenCastIface = "org.freedesktop.portal.ScreenCast";

}  // namespace

WaylandFrameSource::~WaylandFrameSource() { close(); }

// Deterministic per-instance D-Bus connection name so open() and close() agree
// without storing it (the header keeps a fixed member layout).
static QString connectionName(const void* self) {
    return QStringLiteral("rr-portal-%1").arg(reinterpret_cast<quintptr>(self));
}

bool WaylandFrameSource::open(const CaptureRegion& region, int fps) {
    fps_ = fps > 0 ? fps : 10;

    // A private, worker-thread-owned session-bus connection. The portal Response
    // signals are then delivered to this thread's event loop (the recording loop
    // runs on a QThread::create lambda, not the GUI thread).
    const QString connName = connectionName(this);
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connName);
    if (!bus.isConnected()) {
        QDBusConnection::disconnectFromBus(connName);
        return false;
    }

    static std::atomic<unsigned> tokenCounter{0};
    const QString sender = bus.baseService().mid(1).replace('.', '_');

    // Issues a ScreenCast method that follows the portal Request/Response pattern,
    // pre-subscribing to the predicted Request object path to avoid the race where
    // the Response fires before we connect. Returns false on error/timeout/nonzero.
    auto portalCall = [&](const QString& method, QVariantList args,
                          QVariantMap optionsTemplate, QVariantMap& outResults) -> bool {
        const QString token =
            QStringLiteral("rr%1").arg(tokenCounter.fetch_add(1));
        const QString reqPath =
            QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

        PortalResponseHandler handler;
        bus.connect(QString::fromLatin1(kPortalService), reqPath,
                    QStringLiteral("org.freedesktop.portal.Request"),
                    QStringLiteral("Response"), &handler,
                    SLOT(onResponse(uint, QVariantMap)));

        optionsTemplate.insert(QStringLiteral("handle_token"), token);
        args.append(optionsTemplate);

        QDBusMessage msg = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
            QString::fromLatin1(kScreenCastIface), method);
        msg.setArguments(args);
        const QDBusMessage reply = bus.call(msg);
        if (reply.type() == QDBusMessage::ErrorMessage) return false;

        QTimer::singleShot(60000, &handler.loop, &QEventLoop::quit);
        handler.loop.exec();
        if (handler.code != 0) return false;
        outResults = handler.results;
        return true;
    };

    // 1) CreateSession
    QVariantMap createOpts;
    createOpts.insert(QStringLiteral("session_handle_token"),
                      QStringLiteral("rrsess%1").arg(tokenCounter.fetch_add(1)));
    QVariantMap createResults;
    if (!portalCall(QStringLiteral("CreateSession"), {}, createOpts, createResults)) {
        close();
        return false;
    }
    const QString sessionHandle = sessionHandleFromResults(createResults);
    if (sessionHandle.isEmpty()) {
        close();
        return false;
    }
    sessionPath_ = new QString(sessionHandle);
    const QDBusObjectPath sessionObj(sessionHandle);

    // 2) SelectSources: monitor, single, embedded cursor.
    QVariantMap selectOpts;
    selectOpts.insert(QStringLiteral("types"), static_cast<uint>(1));      // 1 = MONITOR
    selectOpts.insert(QStringLiteral("multiple"), false);
    selectOpts.insert(QStringLiteral("cursor_mode"), static_cast<uint>(2));  // 2 = EMBEDDED
    QVariantMap selectResults;
    if (!portalCall(QStringLiteral("SelectSources"),
                    {QVariant::fromValue(sessionObj)}, selectOpts, selectResults)) {
        close();
        return false;
    }

    // 3) Start: compositor shows its own picker; results carry the stream node id
    //    and (best-effort) the source geometry used to position the crop.
    QVariantMap startResults;
    if (!portalCall(QStringLiteral("Start"),
                    {QVariant::fromValue(sessionObj), QString()}, QVariantMap(),
                    startResults)) {
        close();
        return false;
    }
    bool gotStream = false;
    if (startResults.contains(QStringLiteral("streams"))) {
        QDBusArgument streams =
            startResults.value(QStringLiteral("streams")).value<QDBusArgument>();
        streams.beginArray();
        while (!streams.atEnd()) {
            streams.beginStructure();
            uint node = 0;
            QVariantMap props;
            streams >> node >> props;
            streams.endStructure();
            if (!gotStream) {
                nodeId_ = node;
                gotStream = true;
                if (props.contains(QStringLiteral("size"))) {
                    QDBusArgument s =
                        props.value(QStringLiteral("size")).value<QDBusArgument>();
                    s.beginStructure();
                    int w = 0, h = 0;
                    s >> w >> h;
                    s.endStructure();
                    srcW_ = w;
                    srcH_ = h;
                }
                if (props.contains(QStringLiteral("position"))) {
                    QDBusArgument p =
                        props.value(QStringLiteral("position")).value<QDBusArgument>();
                    p.beginStructure();
                    int x = 0, y = 0;
                    p >> x >> y;
                    p.endStructure();
                    srcX_ = x;
                    srcY_ = y;
                }
            }
        }
        streams.endArray();
    }
    if (!gotStream) {
        close();
        return false;
    }

    // 4) OpenPipeWireRemote returns the fd synchronously (not via Response).
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
            QString::fromLatin1(kScreenCastIface),
            QStringLiteral("OpenPipeWireRemote"));
        msg.setArguments({QVariant::fromValue(sessionObj), QVariantMap()});
        const QDBusMessage reply = bus.call(msg);
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
            close();
            return false;
        }
        const QDBusUnixFileDescriptor fd =
            reply.arguments().first().value<QDBusUnixFileDescriptor>();
        if (!fd.isValid()) {
            close();
            return false;
        }
        pwFd_ = ::dup(fd.fileDescriptor());
        if (pwFd_ < 0) {
            close();
            return false;
        }
    }

    // Crop: the requested region in global coords, mapped into the source. The
    // negotiated stream dimensions (onFormatChanged) may differ on HiDPI; we clamp
    // again at copy time. Output dims are fixed here so the encoder stays consistent.
    cropX_ = region.x - srcX_;
    cropY_ = region.y - srcY_;
    if (cropX_ < 0) cropX_ = 0;
    if (cropY_ < 0) cropY_ = 0;
    int w = region.w;
    int h = region.h;
    if (srcW_ > 0 && cropX_ + w > srcW_) w = srcW_ - cropX_;
    if (srcH_ > 0 && cropY_ + h > srcH_) h = srcH_ - cropY_;
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) {
        close();
        return false;
    }
    width_ = w;
    height_ = h;
    // Start with a black frame so readFrame has something before the first buffer.
    latest_.assign(static_cast<size_t>(width_) * height_ * 3, 0);
    haveFrame_ = false;

    // 5) PipeWire: connect to the remote over the portal fd and start the stream.
    pw_init(nullptr, nullptr);
    auto* loop = pw_thread_loop_new("rr-capture", nullptr);
    if (!loop) {
        close();
        return false;
    }
    threadLoop_ = loop;
    if (pw_thread_loop_start(loop) < 0) {
        close();
        return false;
    }

    pw_thread_loop_lock(loop);
    auto* context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    context_ = context;
    if (!context) {
        pw_thread_loop_unlock(loop);
        close();
        return false;
    }
    auto* core = pw_context_connect_fd(context, pwFd_, nullptr, 0);
    core_ = core;
    if (!core) {
        pw_thread_loop_unlock(loop);
        close();
        return false;
    }
    pwFd_ = -1;  // ownership transferred to PipeWire; it closes the fd on disconnect.

    auto* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                    "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);
    auto* stream = pw_stream_new(core, "regionrecord", props);
    stream_ = stream;
    if (!stream) {
        pw_thread_loop_unlock(loop);
        close();
        return false;
    }
    auto* hook = new spa_hook;
    std::memset(hook, 0, sizeof(*hook));
    streamHook_ = hook;
    pw_stream_add_listener(stream, hook, streamEventsTable(), this);

    uint8_t podBuf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuf, sizeof(podBuf));
    struct spa_rectangle sizeDef = {320u, 240u}, sizeMin = {1u, 1u}, sizeMax = {8192u, 8192u};
    struct spa_fraction rateDef = {30u, 1u}, rateMin = {0u, 1u}, rateMax = {1000u, 1u};
    const struct spa_pod* params[1];
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
        SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_BGRA,
                               SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBA,
                               SPA_VIDEO_FORMAT_RGBx),
        SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&sizeDef, &sizeMin, &sizeMax),
        SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&rateDef, &rateMin, &rateMax)));

    const int rc = pw_stream_connect(
        stream, PW_DIRECTION_INPUT, nodeId_,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                     PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1);
    pw_thread_loop_unlock(loop);
    if (rc < 0) {
        close();
        return false;
    }

    startNs_ = 0;
    frameIndex_ = 0;
    return true;
}

void WaylandFrameSource::onFormatChanged(int w, int h, int spaFormat) {
    std::lock_guard<std::mutex> lk(mutex_);
    fmtW_ = w;
    fmtH_ = h;
    spaFormat_ = spaFormat;
}

void WaylandFrameSource::onProcess() {
    auto* stream = static_cast<pw_stream*>(stream_);
    if (!stream) return;
    pw_buffer* pwb = pw_stream_dequeue_buffer(stream);
    if (!pwb) return;
    spa_buffer* buf = pwb->buffer;

    if (buf && buf->n_datas > 0 && buf->datas[0].data && buf->datas[0].chunk &&
        buf->datas[0].chunk->size > 0) {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto* base = static_cast<const uint8_t*>(buf->datas[0].data);
        int stride = static_cast<int>(buf->datas[0].chunk->stride);
        if (stride <= 0) stride = (fmtW_ > 0 ? fmtW_ : width_) * 4;
        const bool bgr =
            (spaFormat_ == SPA_VIDEO_FORMAT_BGRA || spaFormat_ == SPA_VIDEO_FORMAT_BGRx);
        const int srcMaxW = fmtW_ > 0 ? fmtW_ : (cropX_ + width_);
        const int srcMaxH = fmtH_ > 0 ? fmtH_ : (cropY_ + height_);

        for (int row = 0; row < height_; ++row) {
            uint8_t* dst = latest_.data() + static_cast<size_t>(row) * width_ * 3;
            const int sy = cropY_ + row;
            if (sy >= srcMaxH) {  // beyond the captured frame: leave black
                std::memset(dst, 0, static_cast<size_t>(width_) * 3);
                continue;
            }
            const uint8_t* srcRow = base + static_cast<size_t>(sy) * stride;
            for (int col = 0; col < width_; ++col) {
                const int sx = cropX_ + col;
                if (sx >= srcMaxW) {
                    dst[0] = dst[1] = dst[2] = 0;
                } else {
                    const uint8_t* p = srcRow + static_cast<size_t>(sx) * 4;
                    if (bgr) {
                        dst[0] = p[2];
                        dst[1] = p[1];
                        dst[2] = p[0];
                    } else {
                        dst[0] = p[0];
                        dst[1] = p[1];
                        dst[2] = p[2];
                    }
                }
                dst += 3;
            }
        }
        haveFrame_ = true;
    }
    pw_stream_queue_buffer(stream, pwb);
}

bool WaylandFrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    if (width_ <= 0 || height_ <= 0) return false;

    // Pace to the target frame rate (mirrors X11FrameSource) so the encoder's
    // 1/fps pts cadence matches wall-clock playback.
    const long long t = nowNs();
    if (startNs_ == 0) startNs_ = t;
    const long long deadline = startNs_ + frameIndex_ * (1000000000LL / fps_);
    if (t < deadline) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - t));
    }
    ++frameIndex_;

    stride = width_ * 3;
    std::lock_guard<std::mutex> lk(mutex_);
    rgb = latest_;  // black until the first PipeWire buffer arrives
    return true;
}

void WaylandFrameSource::close() {
    if (threadLoop_) {
        auto* loop = static_cast<pw_thread_loop*>(threadLoop_);
        pw_thread_loop_stop(loop);
        if (stream_) {
            pw_stream_destroy(static_cast<pw_stream*>(stream_));
            stream_ = nullptr;
        }
        if (core_) {
            pw_core_disconnect(static_cast<pw_core*>(core_));
            core_ = nullptr;
        }
        if (context_) {
            pw_context_destroy(static_cast<pw_context*>(context_));
            context_ = nullptr;
        }
        pw_thread_loop_destroy(loop);
        threadLoop_ = nullptr;
    }
    if (streamHook_) {
        delete static_cast<spa_hook*>(streamHook_);
        streamHook_ = nullptr;
    }
    if (pwFd_ >= 0) {
        ::close(pwFd_);
        pwFd_ = -1;
    }

    const QString connName = connectionName(this);
    if (sessionPath_) {
        auto* path = static_cast<QString*>(sessionPath_);
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (QDBusConnection(connName).isConnected()) bus = QDBusConnection(connName);
        QDBusMessage closeMsg = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService), *path,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        bus.call(closeMsg, QDBus::NoBlock);
        delete path;
        sessionPath_ = nullptr;
    }
    QDBusConnection::disconnectFromBus(connName);

    haveFrame_ = false;
}

}  // namespace rr

#include "WaylandFrameSource.moc"
