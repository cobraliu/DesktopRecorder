#include "recording/MacFrameSource.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

// This file is compiled with ARC (see CMakeLists); the ObjC objects held across
// calls live in the C++ object as CFBridgingRetain'ed opaque pointers.

@interface RRStreamHandler : NSObject <SCStreamOutput, SCStreamDelegate>
// atomic (default): read from the SCK sample-handler queue, cleared from close().
@property(assign) rr::MacFrameSource* owner;
@end

@implementation RRStreamHandler

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    rr::MacFrameSource* owner = self.owner;
    if (type != SCStreamOutputTypeScreen || !owner) return;
    if (!CMSampleBufferIsValid(sampleBuffer)) return;

    // Startup/idle buffers carry no usable pixels; only complete frames do.
    NSArray* attachments =
        (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    NSDictionary* info = attachments.firstObject;
    NSNumber* status = info[SCStreamFrameInfoStatus];
    if (status && status.intValue != SCFrameStatusComplete) return;

    CVImageBufferRef img = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!img) return;
    if (CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
        return;
    const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(img));
    if (base) {
        owner->storeFrame(base, static_cast<int>(CVPixelBufferGetWidth(img)),
                          static_cast<int>(CVPixelBufferGetHeight(img)),
                          static_cast<int>(CVPixelBufferGetBytesPerRow(img)));
    }
    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    // The system ended the capture (display unplugged, permission revoked, ...).
    rr::MacFrameSource* owner = self.owner;
    if (owner) owner->markStreamDead();
}

@end

namespace rr {

namespace {

long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Wait for an SCK completion handler; false on timeout so a wedged system call
// cannot hang the recording thread forever.
bool waitSem(dispatch_semaphore_t sem, int seconds) {
    return dispatch_semaphore_wait(
               sem, dispatch_time(DISPATCH_TIME_NOW,
                                  static_cast<int64_t>(seconds) * NSEC_PER_SEC)) == 0;
}

}  // namespace

MacFrameSource::~MacFrameSource() { close(); }

bool MacFrameSource::open(const CaptureRegion& region, int fps) {
    // Enumerating shareable content is also where a missing Screen Recording
    // permission surfaces: the call errors out and open() fails cleanly.
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block SCShareableContent* content = nil;
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c,
                                                                     NSError* e) {
                                                   content = c;
                                                   dispatch_semaphore_signal(sem);
                                                 }];
    if (!waitSem(sem, 30) || !content) return false;

    // Pick the display containing the region's origin (global points, matching
    // CaptureRegion's coordinate space); fall back to the first display.
    SCDisplay* display = content.displays.firstObject;
    for (SCDisplay* d in content.displays) {
        if (CGRectContainsPoint(d.frame, CGPointMake(region.x, region.y))) {
            display = d;
            break;
        }
    }
    if (!display) return false;

    // Convert to display-local points. Clip, don't shift: keep only the on-screen
    // part of a region hanging off an edge. Encoders dislike odd dimensions.
    const CGRect db = display.frame;
    int lx = region.x - static_cast<int>(db.origin.x);
    int ly = region.y - static_cast<int>(db.origin.y);
    int w = region.w;
    int h = region.h;
    if (lx < 0) { w += lx; lx = 0; }
    if (ly < 0) { h += ly; ly = 0; }
    if (lx + w > static_cast<int>(db.size.width)) w = static_cast<int>(db.size.width) - lx;
    if (ly + h > static_cast<int>(db.size.height)) h = static_cast<int>(db.size.height) - ly;
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) return false;

    // Capture at native pixel resolution. SCStreamConfiguration.width/height are in
    // PIXELS, so on a Retina display (backing scale 2) requesting the point size would
    // halve the linear resolution and blur the output - especially text. Encode at the
    // display's pixel dimensions instead, matching the physical-pixel video the X11 and
    // Windows backends already produce.
    double scale = 1.0;
    if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID)) {
        const size_t ptW = CGDisplayModeGetWidth(mode);
        if (ptW > 0)
            scale = static_cast<double>(CGDisplayModeGetPixelWidth(mode)) / static_cast<double>(ptW);
        CGDisplayModeRelease(mode);
    }
    const int pw = static_cast<int>(std::lround(w * scale)) & ~1;
    const int ph = static_cast<int>(std::lround(h * scale)) & ~1;
    if (pw <= 0 || ph <= 0) return false;

    // Exclude this application's own windows (Stop HUD, red capture frame,
    // countdown) from the stream.
    NSMutableArray<SCRunningApplication*>* excluded = [NSMutableArray array];
    const pid_t selfPid = getpid();
    for (SCRunningApplication* app in content.applications) {
        if (app.processID == selfPid) [excluded addObject:app];
    }
    SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display
                                                 excludingApplications:excluded
                                                      exceptingWindows:@[]];

    SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
    // sourceRect selects the display-local region in POINTS; width/height set the output
    // size in PIXELS. Setting them to the region's native pixel size (points x scale)
    // captures 1:1 with no downscaling, so the recording is sharp on Retina displays.
    cfg.sourceRect = CGRectMake(lx, ly, w, h);
    cfg.width = static_cast<size_t>(pw);
    cfg.height = static_cast<size_t>(ph);
    cfg.pixelFormat = kCVPixelFormatType_32BGRA;
    cfg.minimumFrameInterval = CMTimeMake(1, fps > 0 ? fps : 10);
    cfg.queueDepth = 5;
    cfg.showsCursor = YES;

    RRStreamHandler* handler = [[RRStreamHandler alloc] init];
    handler.owner = this;
    SCStream* stream = [[SCStream alloc] initWithFilter:filter
                                          configuration:cfg
                                               delegate:handler];
    if (!stream) return false;

    dispatch_queue_t queue = dispatch_queue_create("rr.sck.frames", DISPATCH_QUEUE_SERIAL);
    NSError* err = nil;
    if (![stream addStreamOutput:handler
                            type:SCStreamOutputTypeScreen
              sampleHandlerQueue:queue
                           error:&err])
        return false;

    __block BOOL started = NO;
    [stream startCaptureWithCompletionHandler:^(NSError* e) {
      started = (e == nil);
      dispatch_semaphore_signal(sem);
    }];
    if (!waitSem(sem, 30) || !started) return false;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        width_ = pw;
        height_ = ph;
        // Until the first frame lands the reader hands out black, so a slow first
        // delivery cannot stall the recording thread or skew the timeline.
        latest_.assign(static_cast<size_t>(pw) * ph * 3, 0);
        haveFrame_ = false;
    }
    fps_ = fps > 0 ? fps : 10;
    startNs_ = 0;
    frameIndex_ = 0;
    streamDead_.store(false);

    stream_ = (void*)CFBridgingRetain(stream);
    handler_ = (void*)CFBridgingRetain(handler);
    queue_ = (void*)CFBridgingRetain(queue);
    return true;
}

void MacFrameSource::storeFrame(const uint8_t* bgra, int bufW, int bufH, int bytesPerRow) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (width_ <= 0 || height_ <= 0) return;
    // The buffer should match the configured size; clamp defensively in case the
    // system rounds, and honor the pixel buffer's row padding.
    const int w = std::min(width_, bufW);
    const int h = std::min(height_, bufH);
    for (int row = 0; row < h; ++row) {
        const uint8_t* s = bgra + static_cast<size_t>(row) * bytesPerRow;
        uint8_t* d = latest_.data() + static_cast<size_t>(row) * width_ * 3;
        for (int col = 0; col < w; ++col) {
            d[0] = s[2];  // R
            d[1] = s[1];  // G
            d[2] = s[0];  // B
            s += 4;
            d += 3;
        }
    }
    haveFrame_ = true;
}

bool MacFrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    if (width_ <= 0 || height_ <= 0) return false;
    if (streamDead_.load()) return false;  // system ended the capture

    // Pace to the target frame rate so the produced video matches wall-clock.
    const long long t = nowNs();
    if (startNs_ == 0) startNs_ = t;
    const long long deadline = startNs_ + frameIndex_ * (1000000000LL / fps_);
    if (t < deadline) std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - t));
    ++frameIndex_;

    std::lock_guard<std::mutex> lk(mutex_);
    stride = width_ * 3;
    rgb.assign(latest_.begin(), latest_.end());
    return true;
}

void MacFrameSource::close() {
    if (stream_) {
        SCStream* stream = (__bridge SCStream*)stream_;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [stream stopCaptureWithCompletionHandler:^(NSError* e) {
          dispatch_semaphore_signal(sem);
        }];
        waitSem(sem, 5);
    }
    if (handler_ && queue_) {
        // Sample callbacks run serially on queue_; draining it after detaching the
        // back-pointer guarantees none can touch this object once close() returns.
        RRStreamHandler* handler = (__bridge RRStreamHandler*)handler_;
        handler.owner = nullptr;
        dispatch_sync((__bridge dispatch_queue_t)queue_, ^{
        });
    }
    if (stream_) {
        CFBridgingRelease(stream_);
        stream_ = nullptr;
    }
    if (handler_) {
        CFBridgingRelease(handler_);
        handler_ = nullptr;
    }
    if (queue_) {
        CFBridgingRelease(queue_);
        queue_ = nullptr;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    width_ = 0;
    height_ = 0;
}

}  // namespace rr
