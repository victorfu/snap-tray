#include "capture/X11CaptureEngine.h"
#include "capture/CaptureFrameTiming.h"
#include "X11ScreenGeometry.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QScreen>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/randr.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdlib>

struct X11CaptureEngine::State {
    std::atomic<bool> running{false};
    std::atomic<int> fps{30};
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    QImage latest;
    xcb_connection_t* connection = nullptr;
    xcb_screen_t* screen = nullptr;
    xcb_shm_seg_t segment = 0;
    int shmid = -1;
    char* memory = nullptr;
    QRect pixels;
    int stride = 0;
    bool shared = false;
    ~State() {
        if (connection && shared) xcb_shm_detach(connection, segment);
        if (connection) xcb_disconnect(connection);
        if (memory) shmdt(memory);
        if (shmid >= 0) shmctl(shmid, IPC_RMID, nullptr);
    }
};
X11CaptureEngine::X11CaptureEngine(QObject* parent) : ICaptureEngine(parent) {}
X11CaptureEngine::~X11CaptureEngine() { stop(); }
bool X11CaptureEngine::setRegion(const QRect& region, QScreen* screen) {
    return setRegion(region, CaptureScreenInfo::fromScreen(screen));
}
bool X11CaptureEngine::setRegion(const QRect& region, const CaptureScreenInfo& info) {
    if (isRunning() || !info.isValid() || region.isEmpty()) return false;
    m_captureRegion = region; m_info = info; return true;
}
void X11CaptureEngine::setFrameRate(int fps) {
    m_frameRate = qBound(1, fps, 240);
    if (d) d->fps = m_frameRate;
}
bool X11CaptureEngine::start() {
    stop();
    if (!m_info.isValid()) return false;
    d = std::make_unique<State>();
    auto& s = *d;
    int screenNumber = 0;
    s.connection = xcb_connect(nullptr, &screenNumber);
    if (xcb_connection_has_error(s.connection)) { d.reset(); return false; }
    const auto* setup = xcb_get_setup(s.connection);
    auto screens = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screenNumber; ++i) xcb_screen_next(&screens);
    s.screen = screens.data;
    const QRect native = SnapTray::x11ScreenGeometry(s.connection, s.screen, m_info.name,
        QSize(qRound(m_info.geometry.width() * m_info.devicePixelRatio),
              qRound(m_info.geometry.height() * m_info.devicePixelRatio)));
    if (native.isEmpty() || (!m_info.physicalGeometry.isEmpty() && native != m_info.physicalGeometry)) {
        emit error(QStringLiteral("Cannot resolve the X11 monitor's native bounds, or the output changed."));
        d.reset();
        return false;
    }
    const auto offset = m_captureRegion.topLeft() - m_info.geometry.topLeft();
    s.pixels = QRect(native.topLeft() + QPoint(qRound(offset.x()*m_info.devicePixelRatio),
                                             qRound(offset.y()*m_info.devicePixelRatio)),
                     QSize(qRound(m_captureRegion.width()*m_info.devicePixelRatio),
                           qRound(m_captureRegion.height()*m_info.devicePixelRatio)));
    if (!QRect(0, 0, s.screen->width_in_pixels, s.screen->height_in_pixels).contains(s.pixels)
        || setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) { d.reset(); return false; }
    bool supported = false;
    for (auto it = xcb_setup_pixmap_formats_iterator(setup); it.rem; xcb_format_next(&it))
        if (it.data->depth == s.screen->root_depth && it.data->bits_per_pixel == 32) supported = true;
    // Only native RGB888 visuals can be interpreted as QImage::RGB32.
    for (auto depths = xcb_screen_allowed_depths_iterator(s.screen); depths.rem; xcb_depth_next(&depths))
        for (auto visual = xcb_depth_visuals_iterator(depths.data); visual.rem; xcb_visualtype_next(&visual))
            if (visual.data->visual_id == s.screen->root_visual)
                supported = supported && visual.data->red_mask == 0xff0000
                    && visual.data->green_mask == 0xff00 && visual.data->blue_mask == 0xff;
    if (!supported) { emit error(QStringLiteral("Unsupported X11 pixel format.")); d.reset(); return false; }
    s.stride = s.pixels.width()*4;
    s.shmid = shmget(IPC_PRIVATE, size_t(s.stride)*s.pixels.height(), IPC_CREAT | 0600);
    if (s.shmid >= 0) {
        void* memory = shmat(s.shmid, nullptr, 0);
        if (memory != reinterpret_cast<void*>(-1)) {
            s.memory = static_cast<char*>(memory);
            s.segment = xcb_generate_id(s.connection);
            auto* error = xcb_request_check(s.connection, xcb_shm_attach_checked(s.connection, s.segment, s.shmid, 0));
            s.shared = !error; free(error);
            // Mark for deletion immediately after the server has attached.
            shmctl(s.shmid, IPC_RMID, nullptr); s.shmid = -1;
        }
    }
    s.fps = m_frameRate; s.running = true;
    s.worker = std::thread([this, &s] {
        qint64 count = 0, captureNs = 0;
        while (s.running) {
            const auto deadline = std::chrono::steady_clock::now() + SnapTray::captureFrameInterval(s.fps.load());
            QElapsedTimer timer; timer.start();
            QImage image;
            if (s.shared) {
                auto* reply = xcb_shm_get_image_reply(s.connection, xcb_shm_get_image(s.connection,
                    s.screen->root, s.pixels.x(), s.pixels.y(), s.pixels.width(), s.pixels.height(),
                    ~0u, XCB_IMAGE_FORMAT_Z_PIXMAP, s.segment, 0), nullptr);
                if (reply) image = QImage(reinterpret_cast<uchar*>(s.memory), s.pixels.width(),
                    s.pixels.height(), s.stride, QImage::Format_RGB32).copy();
                free(reply);
            } else {
                auto* reply = xcb_get_image_reply(s.connection, xcb_get_image(s.connection,
                    XCB_IMAGE_FORMAT_Z_PIXMAP, s.screen->root, s.pixels.x(), s.pixels.y(),
                    s.pixels.width(), s.pixels.height(), ~0u), nullptr);
                if (reply && xcb_get_image_data_length(reply) >= s.stride*s.pixels.height())
                    image = QImage(xcb_get_image_data(reply), s.pixels.width(), s.pixels.height(),
                        s.stride, QImage::Format_RGB32).copy();
                free(reply);
            }
            if (image.isNull()) { s.running = false; emit error(QStringLiteral("X11 screen capture failed.")); break; }
            captureNs += timer.nsecsElapsed(); ++count;
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.latest = image;
            }
            emit frameReady(image);
            std::unique_lock<std::mutex> lock(s.mutex);
            s.wake.wait_until(lock, deadline, [&] { return !s.running; });
        }
        if (count) qDebug() << "X11 capture:" << count << "frames, mean capture ms" << double(captureNs)/count/1e6;
    });
    return true;
}
void X11CaptureEngine::stop() {
    if (!d) return;
    d->running = false; d->wake.notify_all();
    if (d->worker.joinable()) d->worker.join();
    d.reset();
}
bool X11CaptureEngine::isRunning() const { return d && d->running; }
QImage X11CaptureEngine::captureFrame() {
    if (!d) return {};
    if (!d->running) { emit error(QStringLiteral("X11 screen capture stopped.")); return {}; }
    std::lock_guard<std::mutex> lock(d->mutex);
    QImage image; image.swap(d->latest); return image;
}
