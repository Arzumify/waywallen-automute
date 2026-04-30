// waywallen-image-renderer — FFmpeg-decoded still image renderer subprocess
// for the waywallen daemon. Spawned for wallpapers of type "image".
//

#include <waywallen-bridge/bridge.h>

#include "av_image.hpp"
#include "vk_producer.hpp"
#include <waywallen-bridge/probe_vk.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct Options {
    std::string ipc_path;
    std::string image_path;
    uint32_t    width { 1920 };
    uint32_t    height { 1080 };
    bool        decode_only { false };
    bool        vulkan_probe { false };
    bool        produce_once { false };
    // Test hook: probe the picked Vulkan device for supported (fourcc,
    // modifier) pairs and emit a `PeerCaps`-shaped JSON document on
    // stdout, then exit. Consumed by the dmabuf_roundtrip_e2e test
    // orchestrator to compute the producer×consumer cap intersection
    // before per-pair iteration.
    bool        print_caps { false };
};

uint64_t now_ns() {
    const auto t = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "waywallen-image-renderer: %s\n", msg.c_str());
    std::exit(1);
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) return {};
            return argv[++i];
        };
        if (a == "--ipc") {
            o.ipc_path = next();
        } else if (a == "--width") {
            o.width = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (a == "--height") {
            o.height = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (a == "--image" || a == "--path") {
            o.image_path = next();
        } else if (a == "--decode-only") {
            // Test hook: run the ffmpeg decode path and exit without
            // opening the bridge socket. Non-zero exit on decode failure.
            o.decode_only = true;
        } else if (a == "--vulkan-probe") {
            // Test hook: build one VkProducer slot, print its layout,
            // exit. Non-zero on failure. No IPC, no decode.
            o.vulkan_probe = true;
        } else if (a == "--produce-once") {
            // Test hook: decode --image, upload into one VkProducer slot,
            // export a sync_fd, close fds, exit. No IPC.
            o.produce_once = true;
        } else if (a == "--print-caps") {
            o.print_caps = true;
        } else {
            // Swallow unknown --key value pairs forwarded by the daemon from
            // source-plugin metadata (e.g. --fps, --workshop_id for animated
            // formats we don't implement yet).
            if (!a.empty() && a.rfind("--", 0) == 0 && i + 1 < argc
                && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                ++i;
            }
        }
    }
    return o;
}


// ---------------------------------------------------------------------------
// IPC
// ---------------------------------------------------------------------------

struct HostState {
    int                   sock { -1 };
    std::mutex            send_mu;
    std::atomic<bool>     shutdown { false };
    std::mutex            wake_mu;
    std::condition_variable wake_cv;

    // Producer + last-uploaded RGBA buffer + bind-buffers generation.
    // Held under send_mu when used from apply_control's
    // ConfigureBuffers branch (rebuild + re-export + re-emit
    // bind_buffers + frame_ready). The main thread populates these
    // before the reader thread starts.
    ww_image::VkProducer* producer { nullptr };
    const uint8_t*        rgba_data { nullptr };
    size_t                rgba_size { 0 };
    uint64_t              bind_generation { 0 };
    uint64_t              next_seq { 1 };
    // Monotonic counter for `frame_ready.release_point`. Each new
    // submit waits on `last_release_point` (the previous frame's
    // point — daemon will have transferred consumer release fences
    // onto it once they signal) and publishes a fresh point. Starts
    // at 0 so the very first submit skips the wait.
    uint64_t              last_release_point { 0 };
};

void signal_shutdown(HostState& s) {
    s.shutdown.store(true, std::memory_order_release);
    s.wake_cv.notify_all();
}

// Test hook: when WAYWALLEN_IMAGE_DUMP_DIR is set, write the RGBA8
// bytes the renderer is about to upload to the GPU to a file the
// orchestrator can compare against the consumer-side dump. The dump
// captures the *input* (post-decode, pre-staging) so it's always
// linear regardless of the picked DRM modifier — the consumer also
// dumps post-readback linear bytes, so byte-equality is meaningful.
//
// Filename: producer-{seq:06}-0x{fourcc:08x}-0x{modifier:016x}.bin
// Sidecar:  same name with .json — width/height/stride/fourcc/modifier.
//
// Cheap and best-effort: any I/O failure is logged but does not break
// the renderer (a CI sandbox without write perms shouldn't take down
// the producer).
static void maybe_dump_producer_frame(const HostState& s, uint64_t seq) {
    const char* dir = std::getenv("WAYWALLEN_IMAGE_DUMP_DIR");
    if (!dir || !*dir) return;
    if (!s.producer || !s.rgba_data || s.rgba_size == 0) return;
    const auto& L = s.producer->layout();

    char path[512];
    std::snprintf(path, sizeof(path),
                  "%s/producer-%06llu-0x%08x-0x%016llx.bin",
                  dir,
                  static_cast<unsigned long long>(seq),
                  L.drm_fourcc,
                  static_cast<unsigned long long>(L.drm_modifier));
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: dump open %s: %s\n",
                     path, std::strerror(errno));
        return;
    }
    size_t w = std::fwrite(s.rgba_data, 1, s.rgba_size, f);
    std::fclose(f);
    if (w != s.rgba_size) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: dump short write %zu/%zu to %s\n",
                     w, s.rgba_size, path);
        return;
    }

    char sidecar[520];
    std::snprintf(sidecar, sizeof(sidecar),
                  "%s/producer-%06llu-0x%08x-0x%016llx.json",
                  dir,
                  static_cast<unsigned long long>(seq),
                  L.drm_fourcc,
                  static_cast<unsigned long long>(L.drm_modifier));
    FILE* sf = std::fopen(sidecar, "w");
    if (!sf) return;
    // Note: the dump is always tightly-packed RGBA8 (`width*height*4` bytes)
    // — that's the input format `decode_to_rgba` produces and what
    // `upload_and_submit` accepts. The DMA-BUF stride/plane_offset are the
    // *destination* layout in the GPU buffer, which the consumer reads
    // back into the same tightly-packed shape; both sides' dumps are
    // therefore directly comparable.
    std::fprintf(sf,
                 "{\n"
                 "  \"kind\": \"producer\",\n"
                 "  \"seq\": %llu,\n"
                 "  \"fourcc\": \"0x%08x\",\n"
                 "  \"modifier\": \"0x%016llx\",\n"
                 "  \"width\": %u,\n"
                 "  \"height\": %u,\n"
                 "  \"stride\": %u,\n"
                 "  \"plane_offset\": %u,\n"
                 "  \"size\": %u,\n"
                 "  \"row_bytes\": %u,\n"
                 "  \"row_count\": %u,\n"
                 "  \"dump_layout\": \"tightly_packed_rgba8\"\n"
                 "}\n",
                 static_cast<unsigned long long>(seq),
                 L.drm_fourcc,
                 static_cast<unsigned long long>(L.drm_modifier),
                 L.width, L.height, L.stride, L.plane_offset, L.size,
                 L.width * 4u, L.height);
    std::fclose(sf);
}

// Re-export the producer's current slot, send fresh bind_buffers + a
// frame_ready that signals the just-uploaded image. Caller must hold
// `s.send_mu`.
static bool emit_bind_and_frame_locked(HostState& s, int sync_fd) {
    if (!s.producer) return false;
    const auto& L = s.producer->layout();

    s.bind_generation += 1;

    uint64_t sizes[1] = { L.size };
    int      fds[1]   = { L.dmabuf_fd };

    ww_evt_bind_buffers_t bb {};
    bb.generation   = s.bind_generation;
    bb.flags        = s.producer->flags();
    bb.count        = 1;
    bb.fourcc       = L.drm_fourcc;
    bb.width        = L.width;
    bb.height       = L.height;
    bb.stride       = L.stride;
    bb.modifier     = L.drm_modifier;
    bb.plane_offset = L.plane_offset;
    bb.sizes.count  = 1;
    bb.sizes.data   = sizes;

    if (int rc = ww_bridge_send_bind_buffers(s.sock, &bb, fds); rc != 0) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: send bind_buffers failed: %d\n",
                     rc);
        ::close(sync_fd);
        return false;
    }

    // Advance the release timeline: this submit's release fence will
    // arrive on the daemon side as `release_timeline_sem_ @ next_point`
    // once every consumer signals their per-frame syncobj.
    const uint64_t next_release_point = s.last_release_point + 1;

    ww_evt_frame_ready_t fr {};
    fr.image_index   = 0;
    fr.seq           = s.next_seq++;
    fr.ts_ns         = now_ns();
    fr.release_point = next_release_point;
    maybe_dump_producer_frame(s, fr.seq);
    int rc = ww_bridge_send_frame_ready(s.sock, &fr, sync_fd);
    ::close(sync_fd);
    if (rc != 0) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: send frame_ready failed: %d\n",
                     rc);
        return false;
    }
    s.last_release_point = next_release_point;
    return true;
}

// Honour the daemon's `negotiate_buffers`: rebuild the producer's
// slot with the requested (modifier, placement), re-upload the
// cached RGBA buffer, and re-emit bind_buffers + frame_ready.
// Returns 0 on success, a negative errno-ish value on rebuild
// failure (caller maps to `bind_failed`), or a positive non-zero
// for fatal errors that should shut the renderer down.
static int apply_negotiate(HostState& s, uint32_t flags, uint64_t modifier) {
    std::lock_guard<std::mutex> lock(s.send_mu);
    std::fprintf(stderr,
                 "waywallen-image-renderer: NegotiateBuffers "
                 "(requested flags=0x%x modifier=0x%016llx, current flags=0x%x modifier=0x%016llx)\n",
                 flags,
                 static_cast<unsigned long long>(modifier),
                 s.producer ? s.producer->flags() : 0u,
                 s.producer ? static_cast<unsigned long long>(s.producer->modifier())
                            : 0ULL);
    if (!s.producer || !s.rgba_data) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: NegotiateBuffers ignored "
                     "(no producer/image yet)\n");
        return 1;
    }
    if (flags == s.producer->flags() && modifier == s.producer->modifier()) {
        // Already at the requested (flags, modifier) — re-upload to
        // re-emit a fresh bind_buffers generation.
        std::string uerr;
        int sync_fd = s.producer->upload_and_submit(
            s.rgba_data, s.rgba_size, s.last_release_point, &uerr);
        if (sync_fd < 0) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: re-upload failed: %s\n",
                         uerr.c_str());
            return 1;
        }
        emit_bind_and_frame_locked(s, sync_fd);
        return 0;
    }

    std::string rerr;
    if (!s.producer->rebuild(flags, modifier, &rerr)) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: rebuild(flags=0x%x modifier=0x%016llx) failed: %s\n",
                     flags,
                     static_cast<unsigned long long>(modifier),
                     rerr.c_str());
        // Non-fatal: tell the daemon to retry with a different
        // (fourcc, modifier) via the iter-5 blacklist loop. The
        // producer's slot is now in a half-torn-down state; the
        // next successful rebuild will replace it. If the daemon
        // runs out of options it will eventually give up.
        return -1;
    }
    // Rebuild created a fresh image (different memory), so the prior
    // release_point no longer corresponds to anyone holding the new
    // dma-buf. Reset and skip the wait on this submit.
    s.last_release_point = 0;
    std::string uerr;
    int sync_fd = s.producer->upload_and_submit(
        s.rgba_data, s.rgba_size, /*wait_release_point=*/0, &uerr);
    if (sync_fd < 0) {
        std::fprintf(stderr,
                     "waywallen-image-renderer: post-rebuild upload failed: %s\n",
                     uerr.c_str());
        return 1;
    }
    if (!emit_bind_and_frame_locked(s, sync_fd)) {
        return 1;
    }
    return 0;
}

void apply_control(HostState& s, const ww_bridge_control_t& c) {
    switch (c.op) {
    case WW_REQ_HELLO:
        break;
    case WW_REQ_LOAD_SCENE:
        // TODO(M4): re-decode and re-upload when the daemon hot-swaps the
        // image. Today we log and keep the initial image.
        std::fprintf(stderr,
                     "waywallen-image-renderer: load_scene pkg=%s "
                     "(hot-swap not yet implemented)\n",
                     c.u.load_scene.pkg ? c.u.load_scene.pkg : "(null)");
        break;
    case WW_REQ_PLAY:
    case WW_REQ_PAUSE:
        // Static images: play/pause are no-ops. Animated formats land in M5.
        break;
    case WW_REQ_MOUSE:
    case WW_REQ_SET_FPS:
        // Images don't respond to input and pace themselves (zero fps).
        break;
    case WW_REQ_SHUTDOWN:
        signal_shutdown(s);
        break;
    case WW_REQ_NEGOTIATE_BUFFERS: {
        const auto& nb = c.u.negotiate_buffers;
        // VkProducer is fixed to VK_FORMAT_R8G8B8A8_UNORM = ABGR8888;
        // any other fourcc is genuinely unsupported. The modifier,
        // however, can be any of the entries advertised in
        // `format_caps` — `apply_negotiate` rebuilds the image with
        // that modifier in the `VkImageDrmFormatModifierListCreateInfoEXT`
        // list, and Vulkan picks it (it has nothing else to choose).
        if (nb.fourcc != WW_DRM_FORMAT_ABGR8888) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: NegotiateBuffers requested "
                         "unsupported fourcc=0x%08x — reporting bind_failed\n",
                         nb.fourcc);
            int rc;
            {
                std::lock_guard<std::mutex> lock(s.send_mu);
                rc = ww_bridge_send_bind_failed(
                    s.sock, nb.fourcc, nb.modifier,
                    /*reason=*/2 /* feature_unsupported */,
                    "VkProducer only handles ABGR8888");
            }
            if (rc != 0) signal_shutdown(s);
            break;
        }
        // Map mem_hint → BUF_HOST_VISIBLE flag.
        const uint32_t flags =
            (nb.mem_hint & WW_MEM_HINT_HOST_VISIBLE) ? WW_BUF_HOST_VISIBLE : 0u;
        std::fprintf(stderr,
                     "waywallen-image-renderer: NegotiateBuffers honored "
                     "(mem_hint=0x%x → flags=0x%x, modifier=0x%016llx)\n",
                     nb.mem_hint, flags,
                     static_cast<unsigned long long>(nb.modifier));
        int an = apply_negotiate(s, flags, nb.modifier);
        if (an < 0) {
            // Rebuild failed for this (fourcc, modifier). Report
            // bind_failed so the daemon's iter-5 blacklist loop
            // re-picks; don't shut down on a single rejection.
            int rc;
            {
                std::lock_guard<std::mutex> lock(s.send_mu);
                rc = ww_bridge_send_bind_failed(
                    s.sock, nb.fourcc, nb.modifier,
                    /*reason=*/0 /* import_failed */,
                    "vk_producer rebuild rejected modifier");
            }
            if (rc != 0) signal_shutdown(s);
        } else if (an > 0) {
            signal_shutdown(s);
        }
        break;
    }
    default:
        std::fprintf(stderr,
                     "waywallen-image-renderer: unknown control op %d\n",
                     static_cast<int>(c.op));
        break;
    }
}

void reader_loop(HostState& s) {
    while (!s.shutdown.load(std::memory_order_acquire)) {
        ww_bridge_control_t msg {};
        int                 rc = ww_bridge_recv_control(s.sock, &msg);
        if (rc != 0) {
            if (!s.shutdown.load(std::memory_order_acquire)) {
                std::fprintf(stderr,
                             "waywallen-image-renderer: recv_control failed: %d\n",
                             rc);
            }
            signal_shutdown(s);
            return;
        }
        apply_control(s, msg);
        ww_bridge_control_free(&msg);
    }
}

} // namespace


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// Emit a single JSON document on stdout that mirrors the
// `PeerCapsJson` shape consumed by `dmabuf_roundtrip_e2e`. Hand-rolled
// (no nlohmann dep) because the schema is tiny and stable. Keep the
// field names and ordering in sync with
// `displays/dump-test/src/main.rs::PeerCapsJson`.
static int print_caps_json(const Options& opt) {
    std::string verr;
    auto prod = ww_image::VkProducer::create(opt.width, opt.height,
                                              /*flags=*/0,
                                              /*modifier=*/0 /* LINEAR */, &verr);
    if (!prod) {
        std::fprintf(stderr, "waywallen-image-renderer: vk_producer: %s\n",
                     verr.c_str());
        return 1;
    }
    ww_image::VkFormatCaps caps {};
    std::string ferr;
    if (!prod->query_format_caps(&caps, &ferr)) {
        std::fprintf(stderr, "waywallen-image-renderer: query_format_caps: %s\n",
                     ferr.c_str());
        return 1;
    }

    auto put_uuid = [](const uint8_t (&u)[16]) -> std::string {
        std::string s = "[";
        for (int i = 0; i < 16; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", u[i]);
            s += buf;
        }
        s += "]";
        return s;
    };

    std::printf("{\n");
    std::printf("  \"by_fourcc\": {\n");
    size_t cursor = 0;
    for (size_t i = 0; i < caps.fourccs.size(); ++i) {
        const uint32_t fc = caps.fourccs[i];
        const uint32_t n  = caps.mod_counts[i];
        std::printf("    \"0x%08x\": [", fc);
        for (uint32_t j = 0; j < n; ++j) {
            std::printf("%s\n      {\"modifier\": %llu, \"usage\": %u, \"plane_count\": %u}",
                        j ? "," : "",
                        static_cast<unsigned long long>(caps.modifiers[cursor + j]),
                        caps.usages[cursor + j],
                        caps.plane_counts[cursor + j]);
        }
        cursor += n;
        std::printf("\n    ]%s\n", (i + 1 < caps.fourccs.size()) ? "," : "");
    }
    std::printf("  },\n");
    std::printf("  \"device_uuid\": %s,\n", put_uuid(caps.device_uuid).c_str());
    std::printf("  \"driver_uuid\": %s,\n", put_uuid(caps.driver_uuid).c_str());
    std::printf("  \"drm_render_major\": %u,\n", prod->drm_render_major());
    std::printf("  \"drm_render_minor\": %u,\n", prod->drm_render_minor());
    std::printf("  \"sync\": %u,\n",
                static_cast<unsigned>(WW_SYNC_SYNCOBJ_TIMELINE | WW_SYNC_SYNCOBJ_BINARY));
    std::printf("  \"color\": %u,\n",
                static_cast<unsigned>(WW_COLOR_ENC_SRGB | WW_COLOR_RANGE_LIMITED
                                       | WW_COLOR_ALPHA_PREMUL));
    std::printf("  \"mem_hint\": %u,\n", caps.mem_hints);
    std::printf("  \"extent_max_w\": %u,\n", 16384u);
    std::printf("  \"extent_max_h\": %u\n",  16384u);
    std::printf("}\n");
    std::fflush(stdout);
    return 0;
}

int main(int argc, char** argv) {
    Options opt = parse_args(argc, argv);

    if (opt.print_caps) {
        return print_caps_json(opt);
    }

    if (opt.vulkan_probe) {
        std::string verr;
        auto prod = ww_image::VkProducer::create(opt.width, opt.height, /*flags=*/0,
                                                  /*modifier=*/0 /* LINEAR */, &verr);
        if (!prod) {
            std::fprintf(stderr, "waywallen-image-renderer: vk_producer: %s\n",
                         verr.c_str());
            return 1;
        }
        const auto& L = prod->layout();
        std::fprintf(stderr,
                     "waywallen-image-renderer: vk slot "
                     "fd=%d fourcc=0x%08x mod=0x%llx "
                     "%ux%u offset=%u stride=%u size=%u\n",
                     L.dmabuf_fd, L.drm_fourcc,
                     static_cast<unsigned long long>(L.drm_modifier),
                     L.width, L.height, L.plane_offset, L.stride, L.size);
        if (L.dmabuf_fd < 0)       { std::fprintf(stderr, "FAIL: bad fd\n");   return 1; }
        if (L.stride < L.width*4)  { std::fprintf(stderr, "FAIL: stride\n");   return 1; }
        if (L.size < L.stride*L.height) { std::fprintf(stderr, "FAIL: size\n"); return 1; }
        return 0;
    }

    if (opt.decode_only) {
        if (opt.image_path.empty()) die("--decode-only requires --image");
        ww_image::DecodeError derr;
        ww_image::RgbaBuf buf =
            ww_image::decode_to_rgba(opt.image_path, opt.width, opt.height, &derr);
        if (buf.data.empty()) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: decode failed: %s\n",
                         derr.message.c_str());
            return 1;
        }
        uint64_t sum = 0;
        for (uint8_t b : buf.data) sum += b;
        std::fprintf(stderr,
                     "waywallen-image-renderer: decoded %ux%u stride=%u "
                     "bytes=%zu pixel_sum=%llu\n",
                     buf.width, buf.height, buf.stride,
                     buf.data.size(),
                     static_cast<unsigned long long>(sum));
        return 0;
    }

    if (opt.produce_once) {
        if (opt.image_path.empty()) die("--produce-once requires --image");
        ww_image::DecodeError derr;
        ww_image::RgbaBuf buf =
            ww_image::decode_to_rgba(opt.image_path, opt.width, opt.height, &derr);
        if (buf.data.empty()) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: decode failed: %s\n",
                         derr.message.c_str());
            return 1;
        }
        std::string verr;
        auto prod = ww_image::VkProducer::create(opt.width, opt.height, /*flags=*/0,
                                                  /*modifier=*/0 /* LINEAR */, &verr);
        if (!prod) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: vk_producer: %s\n",
                         verr.c_str());
            return 1;
        }
        std::string uerr;
        int sync_fd = prod->upload_and_submit(
            buf.data.data(), buf.data.size(),
            /*wait_release_point=*/0, &uerr);
        if (sync_fd < 0) {
            std::fprintf(stderr,
                         "waywallen-image-renderer: upload: %s\n",
                         uerr.c_str());
            return 1;
        }
        const auto& L = prod->layout();
        std::fprintf(stderr,
                     "waywallen-image-renderer: produced "
                     "dmabuf_fd=%d mod=0x%llx stride=%u size=%u sync_fd=%d\n",
                     L.dmabuf_fd,
                     static_cast<unsigned long long>(L.drm_modifier),
                     L.stride, L.size, sync_fd);
        ::close(sync_fd);
        return 0;
    }

    if (opt.ipc_path.empty()) die("--ipc <socket_path> is required");

    ::prctl(PR_SET_PDEATHSIG, SIGTERM);

    HostState host;
    host.sock = ww_bridge_connect(opt.ipc_path.c_str());
    if (host.sock < 0)
        die("ww_bridge_connect: " + std::string(std::strerror(-host.sock)));

    std::unique_ptr<ww_image::VkProducer> producer;
    ww_image::RgbaBuf rgba_buf; // kept alive across rebuilds
    if (!opt.image_path.empty()) {
        ww_image::DecodeError derr;
        rgba_buf = ww_image::decode_to_rgba(
            opt.image_path, opt.width, opt.height, &derr);
        if (rgba_buf.data.empty()) {
            die("decode " + opt.image_path + ": " + derr.message);
        }

        std::string verr;
        // Initial pool: zero-copy DEVICE_LOCAL + LINEAR modifier. The
        // daemon's first `NegotiateBuffers` will rebuild with whatever
        // (modifier, mem_hint) it picked from `format_caps`, so this
        // initial allocation is just a placeholder that exists long
        // enough for the format-caps probe to run on a live image.
        producer = ww_image::VkProducer::create(
            opt.width, opt.height, /*flags=*/0,
            /*modifier=*/0 /* LINEAR */, &verr);
        if (!producer) die("vk_producer: " + verr);

        // Donate our `vkGetInstanceProcAddr` to the bridge dispatch
        // table so the bridge can call into Vulkan helpers (here:
        // GPU info logger) without linking libvulkan itself.
        ww_bridge_vk_dt_t bdt {};
        ww_bridge_vk_dt_load(&bdt, vkGetInstanceProcAddr,
                             producer->instance());
        ww_bridge_vk_log_gpu_info("waywallen-image-renderer", &bdt,
                                  producer->physical_device());
    }

    // Send Ready *after* device init so the render-node we report is
    // the one actually backing the producer's slot.
    const uint32_t drm_major = producer ? producer->drm_render_major() : 0;
    const uint32_t drm_minor = producer ? producer->drm_render_minor() : 0;
    if (int rc = ww_bridge_send_ready(host.sock, drm_major, drm_minor); rc != 0)
        die("send ready failed: " + std::to_string(rc));

    std::fprintf(stderr,
                 "waywallen-image-renderer: ready image=%s %ux%u "
                 "drm_render=%u:%u\n",
                 opt.image_path.empty() ? "(none)" : opt.image_path.c_str(),
                 opt.width, opt.height, drm_major, drm_minor);

    if (producer) {
        // Modifier-negotiation v2 — probe the picked physical device's
        // supported (fourcc, modifier) set + UUID + plane counts and
        // ship the result to the daemon as `format_caps`. The daemon's
        // picker pairs this with each consumer's `consumer_caps` to
        // decide which scheme to land on.
        ww_image::VkFormatCaps caps {};
        std::string ferr;
        if (!producer->query_format_caps(&caps, &ferr))
            die("query_format_caps: " + ferr);

        ww_format_caps_caller_t m {};
        m.fourccs            = caps.fourccs.data();
        m.fourccs_count      = static_cast<uint32_t>(caps.fourccs.size());
        m.mod_counts         = caps.mod_counts.data();
        m.mod_counts_count   = static_cast<uint32_t>(caps.mod_counts.size());
        m.modifiers          = caps.modifiers.data();
        m.modifiers_count    = static_cast<uint32_t>(caps.modifiers.size());
        m.usages             = caps.usages.data();
        m.usages_count       = static_cast<uint32_t>(caps.usages.size());
        m.plane_counts       = caps.plane_counts.data();
        m.plane_counts_count = static_cast<uint32_t>(caps.plane_counts.size());
        m.device_uuid        = caps.device_uuid;
        m.driver_uuid        = caps.driver_uuid;
        m.drm_render_major   = drm_major;
        m.drm_render_minor   = drm_minor;
        m.mem_hints          = caps.mem_hints;
        m.sync_caps          = WW_SYNC_SYNCOBJ_TIMELINE | WW_SYNC_SYNCOBJ_BINARY;
        m.color_caps         = WW_COLOR_ENC_SRGB | WW_COLOR_RANGE_LIMITED
                             | WW_COLOR_ALPHA_PREMUL;
        m.extent_max_w       = 16384;
        m.extent_max_h       = 16384;
        if (int rc = ww_bridge_send_format_caps_v2(host.sock, &m); rc != 0)
            die("send format_caps failed: " + std::to_string(rc));
        std::fprintf(stderr,
                     "waywallen-image-renderer: sent format_caps "
                     "(%zu modifiers for ABGR8888)\n",
                     caps.modifiers.size());

        // Export the producer's release timeline syncobj and ship it
        // to the daemon BEFORE any frame_ready. The daemon imports it
        // via DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE on its own render node and
        // transfers consumer release fences onto each frame_ready's
        // release_point. Required by ipc_v1: every frame_ready carries
        // a release_point that names a value on this exact syncobj.
        std::string rerr;
        int release_fd = producer->export_release_syncobj_fd(&rerr);
        if (release_fd < 0)
            die("export release_syncobj fd: " + rerr);
        if (int rc = ww_bridge_send_release_syncobj(host.sock, release_fd);
            rc != 0) {
            ::close(release_fd);
            die("send release_syncobj failed: " + std::to_string(rc));
        }
        ::close(release_fd);
        std::fprintf(stderr,
                     "waywallen-image-renderer: sent release_syncobj\n");

        host.producer    = producer.get();
        host.rgba_data   = rgba_buf.data.data();
        host.rgba_size   = rgba_buf.data.size();

        // Iter 3c: NO speculative initial bind. The renderer waits for
        // the daemon's first `NegotiateBuffers` request before
        // allocating + binding. The reader thread routes that into
        // `apply_configure`, which does upload_and_submit +
        // emit_bind_and_frame_locked.
        //
        // Rationale: producer must not allocate a dma-buf before the
        // daemon has resolved a (fourcc, modifier, mem_hint) scheme
        // compatible with whoever's consuming. Pre-allocation can
        // pick a placement no consumer can import, then we burn
        // memory + force an immediate rebuild.
        std::fprintf(stderr,
                     "waywallen-image-renderer: ready, waiting for NegotiateBuffers\n");
    }

    std::thread reader([&]() { reader_loop(host); });

    // M0: we don't produce frames yet. Block until shutdown; the reader
    // thread wakes us via signal_shutdown().
    {
        std::unique_lock<std::mutex> lk(host.wake_mu);
        host.wake_cv.wait(lk, [&] {
            return host.shutdown.load(std::memory_order_acquire);
        });
    }

    if (reader.joinable()) {
        ::shutdown(host.sock, SHUT_RD);
        reader.join();
    }
    ww_bridge_close(host.sock);
    return 0;
}
