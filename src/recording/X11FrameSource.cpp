#include "recording/X11FrameSource.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace rr {

namespace {
// Number of low-order zero bits in a channel mask (e.g. 0x00FF0000 -> 16).
int maskShift(unsigned long mask) {
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; ++s; }
    return s;
}
// Number of set bits in a channel mask (e.g. 0x00FF0000 -> 8).
int maskBits(unsigned long mask) {
    int b = 0;
    while (mask) { b += static_cast<int>(mask & 1); mask >>= 1; }
    return b;
}
long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

X11FrameSource::~X11FrameSource() { close(); }

bool X11FrameSource::open(const CaptureRegion& region, int fps) {
    Display* dpy = XOpenDisplay(std::getenv("DISPLAY"));
    if (!dpy) return false;
    dpy_ = dpy;

    const int screen = DefaultScreen(dpy);
    root_ = static_cast<unsigned long>(RootWindow(dpy, screen));
    Visual* visual = DefaultVisual(dpy, screen);
    const int depth = DefaultDepth(dpy, screen);
    const int screenW = DisplayWidth(dpy, screen);
    const int screenH = DisplayHeight(dpy, screen);

    // The region arrives in Qt logical coordinates; the X server addresses
    // physical pixels, so map through the screen's device pixel ratio first.
    const double scale = region.dpiScale > 0 ? region.dpiScale : 1.0;
    const int rx = static_cast<int>(std::lround(region.x * scale));
    const int ry = static_cast<int>(std::lround(region.y * scale));
    // Clamp the requested region to the screen so XShmGetImage cannot read out
    // of bounds (which raises a BadMatch and would tear down the connection).
    x_ = rx < 0 ? 0 : rx;
    y_ = ry < 0 ? 0 : ry;
    int w = static_cast<int>(std::lround(region.w * scale));
    int h = static_cast<int>(std::lround(region.h * scale));
    if (x_ + w > screenW) w = screenW - x_;
    if (y_ + h > screenH) h = screenH - y_;
    if (w <= 0 || h <= 0) { close(); return false; }
    // Encoders dislike odd dimensions; round down to even.
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) { close(); return false; }
    width_ = w;
    height_ = h;
    fps_ = fps > 0 ? fps : 10;

    useShm_ = XShmQueryExtension(dpy) == True;
    if (useShm_) {
        auto* shm = new XShmSegmentInfo;
        std::memset(shm, 0, sizeof(*shm));
        XImage* img = XShmCreateImage(dpy, visual, static_cast<unsigned>(depth),
                                      ZPixmap, nullptr, shm,
                                      static_cast<unsigned>(width_),
                                      static_cast<unsigned>(height_));
        if (!img) {
            delete shm;
            useShm_ = false;
        } else {
            shm->shmid = shmget(IPC_PRIVATE,
                                static_cast<size_t>(img->bytes_per_line) * img->height,
                                IPC_CREAT | 0600);
            if (shm->shmid < 0) {
                XDestroyImage(img);
                delete shm;
                useShm_ = false;
            } else {
                shm->shmaddr = static_cast<char*>(shmat(shm->shmid, nullptr, 0));
                img->data = shm->shmaddr;
                shm->readOnly = False;
                if (shm->shmaddr == reinterpret_cast<char*>(-1) ||
                    XShmAttach(dpy, shm) != True) {
                    if (shm->shmaddr != reinterpret_cast<char*>(-1)) shmdt(shm->shmaddr);
                    shmctl(shm->shmid, IPC_RMID, nullptr);
                    XDestroyImage(img);
                    delete shm;
                    useShm_ = false;
                } else {
                    XSync(dpy, False);
                    // Mark for destruction now; the segment is freed once we
                    // detach (or on process exit) even if we crash.
                    shmctl(shm->shmid, IPC_RMID, nullptr);
                    image_ = img;
                    shm_ = shm;
                }
            }
        }
    }

    if (!useShm_) {
        // Non-SHM fallback: probe one XGetImage to learn the pixel layout. The
        // actual per-frame grab re-fetches because the image is destroyed each
        // time (XGetImage allocates its own buffer).
        XImage* probe = XGetImage(dpy, root_, x_, y_,
                                  static_cast<unsigned>(width_),
                                  static_cast<unsigned>(height_),
                                  AllPlanes, ZPixmap);
        if (!probe) { close(); return false; }
        image_ = probe;
    }

    XImage* img = static_cast<XImage*>(image_);
    bitsPerPixel_ = img->bits_per_pixel;
    redShift_   = maskShift(img->red_mask);
    greenShift_ = maskShift(img->green_mask);
    blueShift_  = maskShift(img->blue_mask);
    redBits_    = maskBits(img->red_mask);
    greenBits_  = maskBits(img->green_mask);
    blueBits_   = maskBits(img->blue_mask);
    if (!useShm_) {
        // The probe is regenerated each readFrame in the fallback path.
        XDestroyImage(img);
        image_ = nullptr;
    }

    startNs_ = 0;
    frameIndex_ = 0;
    return true;
}

bool X11FrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    Display* dpy = static_cast<Display*>(dpy_);
    if (!dpy) return false;

    // Pace to the target frame rate so the produced video matches wall-clock.
    const long long t = nowNs();
    if (startNs_ == 0) startNs_ = t;
    const long long deadline = startNs_ + frameIndex_ * (1000000000LL / fps_);
    if (t < deadline) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - t));
    }
    ++frameIndex_;

    XImage* img = nullptr;
    if (useShm_) {
        img = static_cast<XImage*>(image_);
        XShmSegmentInfo* shm = static_cast<XShmSegmentInfo*>(shm_);
        (void)shm;
        if (XShmGetImage(dpy, root_, img, x_, y_, AllPlanes) != True) return false;
    } else {
        img = XGetImage(dpy, root_, x_, y_,
                        static_cast<unsigned>(width_),
                        static_cast<unsigned>(height_),
                        AllPlanes, ZPixmap);
        if (!img) return false;
    }

    stride = width_ * 3;
    rgb.resize(static_cast<size_t>(stride) * height_);

    const int bytesPP = bitsPerPixel_ / 8;
    const int rMax = (1 << redBits_) - 1;
    const int gMax = (1 << greenBits_) - 1;
    const int bMax = (1 << blueBits_) - 1;
    const unsigned long rMask = img->red_mask;
    const unsigned long gMask = img->green_mask;
    const unsigned long bMask = img->blue_mask;

    for (int row = 0; row < height_; ++row) {
        const unsigned char* src =
            reinterpret_cast<const unsigned char*>(img->data) +
            static_cast<size_t>(row) * img->bytes_per_line;
        unsigned char* dst = rgb.data() + static_cast<size_t>(row) * stride;
        for (int col = 0; col < width_; ++col) {
            unsigned long px = 0;
            const unsigned char* p = src + static_cast<size_t>(col) * bytesPP;
            // X server pixels are in the image's byte order; assemble the
            // native integer accordingly.
            if (img->byte_order == LSBFirst) {
                for (int k = bytesPP - 1; k >= 0; --k) px = (px << 8) | p[k];
            } else {
                for (int k = 0; k < bytesPP; ++k) px = (px << 8) | p[k];
            }
            unsigned long r = (px & rMask) >> redShift_;
            unsigned long g = (px & gMask) >> greenShift_;
            unsigned long b = (px & bMask) >> blueShift_;
            // Scale each channel up to 8 bits when the visual uses fewer.
            dst[0] = rMax ? static_cast<unsigned char>(r * 255 / rMax) : 0;
            dst[1] = gMax ? static_cast<unsigned char>(g * 255 / gMax) : 0;
            dst[2] = bMax ? static_cast<unsigned char>(b * 255 / bMax) : 0;
            dst += 3;
        }
    }

    if (!useShm_) XDestroyImage(img);
    return true;
}

void X11FrameSource::close() {
    Display* dpy = static_cast<Display*>(dpy_);
    if (image_ && useShm_) {
        XShmSegmentInfo* shm = static_cast<XShmSegmentInfo*>(shm_);
        if (dpy && shm) XShmDetach(dpy, shm);
        XDestroyImage(static_cast<XImage*>(image_));  // frees the XImage struct
        if (shm) {
            if (shm->shmaddr && shm->shmaddr != reinterpret_cast<char*>(-1))
                shmdt(shm->shmaddr);
            delete shm;
        }
    } else if (image_) {
        XDestroyImage(static_cast<XImage*>(image_));
    }
    image_ = nullptr;
    shm_ = nullptr;
    useShm_ = false;
    if (dpy) XCloseDisplay(dpy);
    dpy_ = nullptr;
}

}  // namespace rr
