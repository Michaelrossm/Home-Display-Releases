#include "VideoService.h"
#include "NetworkWorker.h"
#include "NetworkService.h"
#include "IntercomService.h"
#include "../apps/lua/LuaStorageBridge.h"
#include "../apps/lua/LuaJobService.h"
#include "intercom/MediaProtocol.h"
#include "intercom/VideoRetryPolicy.h"
#include <NetworkClient.h>
#include <esp_heap_caps.h>
#include <driver/jpeg_decode.h>
#include <esp_h264_dec_sw.h>
#include <mbedtls/base64.h>
#include <mbedtls/md5.h>
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include <new>
#include <cstring>
namespace hp {
    namespace {
        struct DecodeJob {
            VideoService *service;
            uint8_t *bytes = nullptr;
            size_t size = 0;
            bool h264 = false;
            uint32_t generation = 0, stamp = 0;
            VideoOptions options;
        };
        struct Decoder {
            esp_h264_dec_handle_t handle = nullptr;
            uint32_t generation = 0;
            bool validSps = false, needIdr = true;
            ~Decoder() {
                if (handle) {
                    esp_h264_dec_close(handle);
                    esp_h264_dec_del(handle);
                }
            }
        };
        bool localIp(const char *host, IPAddress &ip) {
            addrinfo hints{}, *reply = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(host, nullptr, &hints, &reply) || !reply)
                return false;
            const auto *addr = reinterpret_cast<sockaddr_in *>(reply->ai_addr);
            const auto n = ntohl(addr->sin_addr.s_addr);
            freeaddrinfo(reply);
            const auto a = n >> 24, b = (n >> 16) & 255;
            if (!(a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)))
                return false;
            ip = IPAddress(a, b, (n >> 8) & 255, n & 255);
            return true;
        }
        String field(const String &text, const char *name) {
            String low = text;
            low.toLowerCase();
            String key = String("\n") + name + ":";
            key.toLowerCase();
            const int at = low.indexOf(key);
            if (at < 0)
                return {};
            const int end = text.indexOf('\n', at + 1);
            String value = text.substring(at + key.length(), end < 0 ? text.length() : end);
            value.trim();
            return value;
        }
        String base64(const String &text) {
            size_t used = 0;
            unsigned char out[512];
            if (mbedtls_base64_encode(out, sizeof(out) - 1, &used,
                                      reinterpret_cast<const unsigned char *>(text.c_str()),
                                      text.length()))
                return {};
            out[used] = 0;
            return String(reinterpret_cast<char *>(out));
        }
        // Digest for older Dahua/Goliath RTSP cameras. Keep challenge material in RAM only.
        String digestParam(const String &challenge, const char *name) {
            const String key = String(name) + "=";
            int p = 0;
            while (p < int(challenge.length())) {
                while (p < int(challenge.length()) && (challenge[p] == ' ' || challenge[p] == ',')) ++p;
                const int start = p;
                while (p < int(challenge.length()) && challenge[p] != '=' && challenge[p] != ',') ++p;
                if (p >= int(challenge.length()) || challenge[p] != '=') { ++p; continue; }
                String found = challenge.substring(start, p); found.trim(); ++p;
                String value;
                if (p < int(challenge.length()) && challenge[p] == '"') {
                    ++p;
                    while (p < int(challenge.length()) && challenge[p] != '"') {
                        if (challenge[p] == '\\' && p + 1 < int(challenge.length())) ++p;
                        value += challenge[p++];
                    }
                    if (p < int(challenge.length())) ++p;
                } else {
                    const int end = challenge.indexOf(',', p);
                    value = challenge.substring(p, end < 0 ? challenge.length() : end);
                    value.trim();
                    p = end < 0 ? challenge.length() : end;
                }
                if (found.equalsIgnoreCase(name)) return value;
            }
            return {};
        }
        String md5hex(const String &value) {
            unsigned char hash[16];
            if (mbedtls_md5(reinterpret_cast<const unsigned char *>(value.c_str()),
                            value.length(), hash) != 0) return {};
            char out[33];
            constexpr char digits[] = "0123456789abcdef";
            for (unsigned i = 0; i < 16; ++i) {
                out[2*i] = digits[hash[i] >> 4]; out[2*i+1] = digits[hash[i] & 15];
            }
            out[32] = 0;
            return String(out);
        }
        int clipped(int v) { return std::max(0, std::min(255, v)); }
    } // namespace
    struct VideoService::Runtime {
        NetworkClient socket;
        VideoOptions options;
        char owner[65]{};
        uint32_t generation = 0, cseq = 0, deadline = 0, lastData = 0;
        enum Phase { Describe, Setup, Play, Stream, Mjpeg };
        Phase phase = Describe;
        String url, track, session, authorization, headers;
        String user, password, digestRealm, digestNonce, digestOpaque, digestQop;
        bool digest = false, basic = false;
        unsigned authRetries = 0, nonceCount = 0;
        int contentLength = -1;
        uint8_t payloadType = 96;
        uint8_t *packet = nullptr, *encoded = nullptr;
        size_t packetUsed = 0, packetNeed = 0, mjpegUsed = 0;
        uint8_t prefix[4]{};
        unsigned prefixUsed = 0;
        bool jpegStarted = false, keepalivePending = false, controlReply = false;
        bool singleImage=false, imageSubmitted=false;
        uint32_t keepaliveAt = 0;
        uint8_t videoChannel = 0;
        intercom::H264Assembler *assembler = nullptr;
        uint8_t parameterSets[1536]{};
        size_t parameterSize = 0;
        String authentication(const char *method,const String &target) {
            String r;
            if (digest) {
                const String nc = String(++nonceCount, HEX);
                String nc8 = nc;
                while (nc8.length() < 8) nc8 = "0" + nc8;
                const String cnonce = md5hex(String(millis()) + ":" + String(cseq)).substring(0, 16);
                const String ha1 = md5hex(user + ":" + digestRealm + ":" + password);
                const String ha2 = md5hex(String(method) + ":" + target);
                const String response = digestQop.length()
                    ? md5hex(ha1 + ":" + digestNonce + ":" + nc8 + ":" + cnonce + ":auth:" + ha2)
                    : md5hex(ha1 + ":" + digestNonce + ":" + ha2);
                r += "Authorization: Digest username=\"" + user + "\", realm=\"" +
                     digestRealm + "\", nonce=\"" + digestNonce +
                     "\", uri=\"" + target + "\", response=\"" + response + "\"";
                if (digestQop.length()) r += ", qop=auth, nc=" + nc8 + ", cnonce=\"" + cnonce + "\"";
                if (digestOpaque.length()) r += ", opaque=\"" + digestOpaque + "\"";
                r += "\r\n";
            } else if (basic && authorization.length()) {
                r += "Authorization: Basic " + authorization + "\r\n";
            }
            return r;
        }
        bool challenge(const String& value) {
            if(value.startsWith("Digest ")){
                const String fields=value.substring(7);
                digestRealm=digestParam(fields,"realm");digestNonce=digestParam(fields,"nonce");
                digestOpaque=digestParam(fields,"opaque");const String algorithm=digestParam(fields,"algorithm");
                const String qop=digestParam(fields,"qop");
                bool authQop=false;
                for(int at=0;at<int(qop.length());){int end=qop.indexOf(',',at);
                    String value=qop.substring(at,end<0?qop.length():end);value.trim();
                    authQop|=value.equalsIgnoreCase("auth");if(end<0)break;at=end+1;}
                if(!digestRealm.length()||!digestNonce.length()||(algorithm.length()&&!algorithm.equalsIgnoreCase("MD5"))||
                    (qop.length()&&!authQop))return false;
                // Reject challenge text that could break the outgoing quoted header.
                for(const auto* v:{&digestRealm,&digestNonce,&digestOpaque})for(size_t i=0;i<v->length();++i)
                    if((*v)[i]=='"'||(*v)[i]=='\\'||static_cast<unsigned char>((*v)[i])<32)return false;
                digestQop=qop.length()?"auth":"";digest=true;basic=false;return true;
            }
            basic=value.startsWith("Basic ");return basic;
        }
        bool httpRequest(){
            String r=String("GET ")+options.path+" HTTP/1.1\r\nHost: "+options.host+":"+String(options.port)+
                "\r\nConnection: close\r\n"+authentication("GET",options.path)+"\r\n";
            headers="";contentLength=-1;deadline=millis()+4000;
            return socket.write(reinterpret_cast<const uint8_t*>(r.c_str()),r.length())==r.length();
        }
        bool request(const char *method, const String &target, const String &extra = {}) {
            String r = String(method) + " " + target + " RTSP/1.0\r\nCSeq: " + String(++cseq) +
                       "\r\nUser-Agent: HomeDisplay\r\n";
            if (session.length())r += "Session: " + session + "\r\n";
            r+=authentication(method,target);
            r += extra + "\r\n";
            headers = "";
            contentLength = -1;
            deadline = millis() + 4000;
            return socket.write(reinterpret_cast<const uint8_t *>(r.c_str()), r.length()) ==
                   r.length();
        }
        ~Runtime() {
            // Best effort only, on the existing worker. Never wait for a teardown reply.
            if(options.rtsp&&session.length()&&socket.connected())request("TEARDOWN",url);
            socket.stop();
            delete assembler;
            heap_caps_free(packet);
            heap_caps_free(encoded);
        }
    };
    VideoService &VideoService::instance() {
        static VideoService s;
        return s;
    }
    bool VideoService::begin() {
        if (registered_)
            return true;
        registered_ = NetworkWorker::instance().addMaintenance(maintenance, this);
        if (registered_)
            LuaJobService::instance().registerOwnerCleanup(
                [](const char *owner) { VideoService::instance().close(owner); });
        return registered_;
    }
    bool VideoService::open(const char *owner, const VideoOptions &o, String &error) {
        if (!owner || !*owner || strlen(owner) > 64 || !memchr(o.host, 0, sizeof(o.host)) ||
            !memchr(o.path, 0, sizeof(o.path)) || !memchr(o.username, 0, sizeof(o.username)) ||
            !memchr(o.secretKey, 0, sizeof(o.secretKey)) || !o.host[0] || o.path[0] != '/' ||
            !o.port || !o.width || !o.height || o.width > 640 || o.height > 480 || !o.fps ||
            o.fps > 10) {
            error = "video_options";
            return false;
        }
        for (const char *p = o.host; *p; ++p)
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') {
                error = "video_host";
                return false;
            }
        for (const char *p = o.path; *p; ++p)
            if ((unsigned char)*p < 33 || *p == '@' || *p == '#') {
                error = "video_path";
                return false;
            }
        for (const char *p = o.username; *p; ++p)
            if ((unsigned char)*p < 32 || *p == ':' || *p=='"' || *p=='\\') {
                error = "video_username";
                return false;
            }
        if (!begin()) {
            error = "video_worker";
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_.active && strcmp(owner_, owner)) {
            error = "video_busy";
            return false;
        }
        options_ = o;
        retries_=0;retryAt_=0;
        fpsAt_=millis();fpsFrames_=state_.frames;state_.fpsMilli=0;state_.lastFrameAt=0;
        state_.width=0;state_.height=0;state_.codec[0]=0;
        strlcpy(owner_, owner, sizeof(owner_));
        ++state_.generation;
        state_.active = true;
        state_.connecting = true;
        state_.error[0] = 0;
        latest_.reset();
        return true;
    }
    void VideoService::close(const char *owner) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!owner || strcmp(owner_, owner))
            return;
        state_.active = false;
        state_.connecting = false;
        state_.fpsMilli=0;
        retryAt_=0;
        ++state_.generation;
        latest_.reset();
        memset(options_.nativePassword, 0, sizeof(options_.nativePassword));
        uiPending_ = true;
    }
    bool VideoService::snapshot(const char *owner, VideoSnapshot &out) const {
        std::unique_lock<std::mutex> guard(mutex_, std::try_to_lock);
        if (!guard.owns_lock() || !owner || strcmp(owner_, owner))
            return false;
        out = state_;
        return true;
    }
    bool VideoService::diagnosticSnapshot(VideoSnapshot &out) const {
        std::unique_lock<std::mutex> guard(mutex_, std::try_to_lock);
        if (!guard.owns_lock())
            return false;
        out = state_;
        return true;
    }
    std::shared_ptr<VideoFrame> VideoService::frame(const char *owner) const {
        std::unique_lock<std::mutex> guard(mutex_, std::try_to_lock);
        return guard.owns_lock() && owner && !strcmp(owner_, owner) ? latest_ : nullptr;
    }
    void VideoService::fail(const char *code, uint32_t generation) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (generation && generation != state_.generation)
                return;
            state_.active = false;
            state_.fpsMilli=0;
            const auto delay=intercom::videoRetryDelay(code,retries_);
            retryAt_=delay?millis()+delay:0;if(delay)++retries_;
            state_.connecting = delay!=0;
            ++state_.generation;
            latest_.reset();
            strlcpy(state_.error, code, sizeof(state_.error));
            ++state_.errors;
        }
        IntercomService::instance().diagnostic(code);
    }
    void VideoService::maintenance(void *ctx) { static_cast<VideoService *>(ctx)->poll(); }
    void VideoService::poll() {
        VideoOptions opts;
        VideoSnapshot state;
        char owner[65];
        {
            std::unique_lock<std::mutex> guard(mutex_, std::try_to_lock);
            if (!guard.owns_lock())
                return;
            if(!state_.active&&retryAt_&&int32_t(millis()-retryAt_)>=0){
                retryAt_=0;state_.active=true;state_.connecting=true;++state_.generation;
            }
            opts = options_;
            state = state_;
            strlcpy(owner, owner_, sizeof(owner));
        }
        const auto fail = [this, generation = state.generation](const char *code) {
            this->fail(code, generation);
        };
        if (runtime_ && (!state.active || runtime_->generation != state.generation)) {
            runtime_->~Runtime();
            heap_caps_free(runtime_);
            runtime_ = nullptr;
        }
        if (!state.active) {
            if (decoderAlive_ && !decoding_.exchange(true)) {
                auto *job = new (std::nothrow) DecodeJob{this};
                if (!job ||
                    !NetworkWorker::instance().submitTransfer(decodeJob, job, "video-close")) {
                    delete job;
                    decoding_ = false;
                }
            }
            return;
        }
        if(NetworkWorker::instance().priorityDepth()||IntercomService::instance().urgent())return;
        if (!NetworkService::instance().online()) {
            fail("video_offline");
            return;
        }
        if (!runtime_) {
            void *memory = heap_caps_malloc(sizeof(Runtime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!memory) {
                fail("video_memory");
                return;
            }
            runtime_ = new (memory) Runtime;
            auto &r = *runtime_;
            r.options = opts;
            r.generation = state.generation;
            strlcpy(r.owner, owner, sizeof(r.owner));
            r.packet = static_cast<uint8_t *>(
                heap_caps_malloc(65536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            r.encoded = static_cast<uint8_t *>(
                heap_caps_malloc(encodedLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            r.assembler = new (std::nothrow) intercom::H264Assembler(r.encoded, encodedLimit);
            if (!r.packet || !r.encoded || !r.assembler) {
                fail("video_memory");
                return;
            }
            IPAddress ip;
            if (!localIp(opts.host, ip)) {
                fail("video_local_target");
                return;
            }
            r.socket.setTimeout(50);
            NetworkWorker::instance().beginNetworkActivity();
            const bool connected = r.socket.connect(ip, opts.port, 500);
            NetworkWorker::instance().endNetworkActivity();
            if (!connected) {
                fail("video_connect");
                return;
            }
            if (opts.username[0]) {
                r.user = opts.username;
                r.password = opts.nativePassword[0] ? String(opts.nativePassword)
                    : luaSecretForNative(owner, opts.secretKey);
                r.authorization = base64(r.user + ":" + r.password);
            }
            r.url = String(opts.rtsp ? "rtsp://" : "http://") + opts.host + ":" +
                    String(opts.port) + opts.path;
            r.lastData = millis();
            if (opts.rtsp) {
                if (!r.request("DESCRIBE", r.url, "Accept: application/sdp\r\n"))
                    fail("rtsp_write");
            } else {
                r.phase = Runtime::Mjpeg;
                r.basic=r.authorization.length()>0;
                if(!r.httpRequest())fail("mjpeg_write");
            }
            return;
        }
        auto &r = *runtime_;
        if(r.singleImage&&r.imageSubmitted){
            if(decoding_.load())return;
            std::lock_guard<std::mutex> guard(mutex_);
            if(state_.generation==r.generation){state_.active=false;state_.connecting=false;retryAt_=0;}
            return;
        }
        if (!r.socket.connected() && !r.socket.available()) {
            fail("video_disconnected");
            return;
        }
        if (r.phase == Runtime::Stream) {
            if (r.keepalivePending && int32_t(millis() - r.deadline) >= 0) {
                fail("rtsp_keepalive_timeout");
                return;
            }
            if (!r.keepalivePending && int32_t(millis() - r.keepaliveAt) >= 0) {
                r.keepalivePending = true;
                if (!r.request("OPTIONS", r.url)) {
                    fail("rtsp_keepalive_write");
                    return;
                }
                r.keepaliveAt = millis() + 20000;
            }
        }
        if (uint32_t(millis() - r.lastData) > 8000) {
            fail("video_timeout");
            return;
        }
        const uint32_t start = micros();
        unsigned consumed = 0;
        while (r.socket.available() && consumed < 8192 && uint32_t(micros() - start) < 3000) {
            const int read = r.socket.read();
            if (read < 0)
                break;
            const uint8_t byte = read;
            ++consumed;
            r.lastData = millis();
            if (r.phase <= Runtime::Play) {
                r.headers += char(byte);
                if (r.headers.length() > 12288) {
                    fail("rtsp_header_limit");
                    return;
                }
                const int separator = r.headers.indexOf("\r\n\r\n");
                if (separator < 0)
                    continue;
                if (r.contentLength < 0) {
                    r.contentLength = field(r.headers, "content-length").toInt();
                    if (r.contentLength < 0 || r.contentLength > 8192) {
                        fail("rtsp_body_limit");
                        return;
                    }
                }
                if (r.headers.length() < size_t(separator + 4 + r.contentLength))
                    continue;
                if (r.headers.startsWith("RTSP/1.0 401") && r.authRetries++ == 0 &&
                    r.user.length()) {
                    if(!r.challenge(field(r.headers,"www-authenticate"))){fail("rtsp_auth_unsupported");return;}
                    const char *method = r.phase == Runtime::Describe ? "DESCRIBE" :
                                         r.phase == Runtime::Setup ? "SETUP" : "PLAY";
                    const String target = r.phase == Runtime::Setup ? r.track : r.url;
                    const String extra = r.phase == Runtime::Describe ? "Accept: application/sdp\r\n" :
                                         r.phase == Runtime::Setup ? "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n" : "";
                    if (!r.request(method, target, extra)) fail("rtsp_auth_retry_write");
                    return;
                }
                if (!r.headers.startsWith("RTSP/1.0 200")) {
                    fail(r.headers.startsWith("RTSP/1.0 401") ? "rtsp_auth_failed"
                                                              : "rtsp_status");
                    return;
                }
                if (r.phase == Runtime::Describe) {
                    const String sdp = r.headers.substring(separator + 4);
                    int video = sdp.indexOf("m=video ");
                    if (video < 0) {
                        fail("rtsp_no_video");
                        return;
                    }
                    int next = sdp.indexOf("\nm=", video + 1);
                    String section = sdp.substring(video, next < 0 ? sdp.length() : next);
                    int codec = section.indexOf(" H264/90000");
                    if (codec < 0) {
                        fail("rtsp_codec_unsupported");
                        return;
                    }
                    int mapping = section.lastIndexOf("a=rtpmap:", codec);
                    if (mapping < 0) {
                        fail("rtsp_sdp");
                        return;
                    }
                    const int payload = section.substring(mapping + 9, codec).toInt();
                    if (payload < 0 || payload > 127) {
                        fail("rtsp_payload");
                        return;
                    }
                    r.payloadType = payload;
                    int control = section.indexOf("a=control:");
                    if (control < 0) {
                        fail("rtsp_track");
                        return;
                    }
                    r.track = section.substring(control + 10, section.indexOf('\n', control));
                    r.track.trim();
                    const String authority =
                        String("rtsp://") + opts.host + ":" + String(opts.port);
                    String base = field(r.headers, "content-base");
                    if (!base.length())
                        base = r.url;
                    auto sameOrigin = [&](const String &uri) {
                        return uri.startsWith(authority + "/") ||
                               (opts.port == 554 &&
                                uri.startsWith(String("rtsp://") + opts.host + "/"));
                    };
                    if (!sameOrigin(base)) {
                        fail("rtsp_foreign_base");
                        return;
                    }
                    if (r.track == "*")
                        r.track = r.url;
                    else if (r.track.startsWith("rtsp://")) {
                        if (!sameOrigin(r.track)) {
                            fail("rtsp_foreign_track");
                            return;
                        }
                    } else if (r.track.startsWith("/"))
                        r.track = authority + r.track;
                    else
                        r.track = base + (base.endsWith("/") ? "" : "/") + r.track;
                    int sets = section.indexOf("sprop-parameter-sets=");
                    if (sets >= 0) {
                        String encoded = section.substring(sets + 21);
                        int end = encoded.indexOf(';');
                        if (end >= 0)
                            encoded = encoded.substring(0, end);
                        end = encoded.indexOf('\r');
                        if (end >= 0)
                            encoded = encoded.substring(0, end);
                        end = encoded.indexOf('\n');
                        if (end >= 0)
                            encoded = encoded.substring(0, end);
                        int comma = encoded.indexOf(',');
                        if (comma >= 0)
                            for (unsigned part = 0; part < 2; ++part) {
                                const String value = part ? encoded.substring(comma + 1)
                                                          : encoded.substring(0, comma);
                                size_t n = 0;
                                uint8_t raw[1024];
                                if (mbedtls_base64_decode(
                                        raw, sizeof(raw), &n,
                                        reinterpret_cast<const unsigned char *>(value.c_str()),
                                        value.length()) ||
                                    !n || r.parameterSize + n + 4 > sizeof(r.parameterSets)) {
                                    fail("rtsp_parameter_sets");
                                    return;
                                }
                                static const uint8_t prefix[] = {0, 0, 0, 1};
                                memcpy(r.parameterSets + r.parameterSize, prefix, 4);
                                r.parameterSize += 4;
                                memcpy(r.parameterSets + r.parameterSize, raw, n);
                                r.parameterSize += n;
                            }
                    }
                    r.phase = Runtime::Setup;
                    if (!r.request("SETUP", r.track,
                                   "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")) {
                        fail("rtsp_write");
                        return;
                    }
                } else if (r.phase == Runtime::Setup) {
                    const String transport = field(r.headers, "transport");
                    int channel = transport.indexOf("interleaved=");
                    if (transport.indexOf("RTP/AVP/TCP") < 0 || channel < 0) {
                        fail("rtsp_transport");
                        return;
                    }
                    const int number = transport.substring(channel + 12).toInt();
                    if (number < 0 || number > 254) {
                        fail("rtsp_channel");
                        return;
                    }
                    r.videoChannel = number;
                    r.session = field(r.headers, "session");
                    int semi = r.session.indexOf(';');
                    if (semi >= 0)
                        r.session = r.session.substring(0, semi);
                    if (!r.session.length() || r.session.length() > 128) {
                        fail("rtsp_session");
                        return;
                    }
                    r.phase = Runtime::Play;
                    if (!r.request("PLAY", r.url)) {
                        fail("rtsp_write");
                        return;
                    }
                } else {
                    r.phase = Runtime::Stream;
                    r.headers = "";
                    r.keepaliveAt = millis() + 20000;
                    std::lock_guard<std::mutex> guard(mutex_);
                    if (state_.generation == r.generation)
                        state_.connecting = false;
                }
            } else if (r.phase == Runtime::Stream) {
                if (r.controlReply) {
                    r.headers += char(byte);
                    if (r.headers.length() > 12288) {
                        fail("rtsp_reply_limit");
                        return;
                    }
                    const int end = r.headers.indexOf("\r\n\r\n");
                    if (end >= 0) {
                        const int count = field(r.headers, "content-length").toInt();
                        if (count < 0 || count > 8192) {
                            fail("rtsp_reply_limit");
                            return;
                        }
                        if (r.headers.length() >= size_t(end + 4 + count)) {
                            if (!r.headers.startsWith("RTSP/1.0 200")) {
                                fail("rtsp_keepalive_status");
                                return;
                            }
                            r.headers = "";
                            r.controlReply = false;
                            r.keepalivePending = false;
                        }
                    }
                    continue;
                }
                if (!r.prefixUsed && byte == 'R') {
                    r.headers = "R";
                    r.controlReply = true;
                    continue;
                }
                if (r.prefixUsed < 4) {
                    r.prefix[r.prefixUsed++] = byte;
                    if (r.prefixUsed == 1 && byte != '$') {
                        fail("rtsp_interleaving");
                        return;
                    }
                    if (r.prefixUsed == 4) {
                        r.packetNeed = intercom::RtpPacket::be16(r.prefix + 2);
                        r.packetUsed = 0;
                        if (!r.packetNeed) {
                            r.prefixUsed = 0;
                        }
                    }
                    continue;
                }
                r.packet[r.packetUsed++] = byte;
                if (r.packetUsed < r.packetNeed)
                    continue;
                if (r.prefix[1] == r.videoChannel) {
                    intercom::RtpPacket packet;
                    const bool valid = packet.parse(r.packet, r.packetUsed);
                    const bool ready =
                        valid && packet.type == r.payloadType && r.assembler->packet(packet);
                    if (!valid || (valid && packet.type == r.payloadType &&
                                   (r.assembler->gap() || (packet.marker && !ready))))
                        resync_ = true;
                    if (ready) {
                        if (r.parameterSize &&
                            r.assembler->size() + r.parameterSize <= encodedLimit) {
                            const size_t n = r.assembler->size();
                            memmove(r.encoded + r.parameterSize, r.encoded, n);
                            memcpy(r.encoded, r.parameterSets, r.parameterSize);
                            submit(r.encoded, n + r.parameterSize, true, packet.timestamp);
                        } else
                            submit(r.assembler->data(), r.assembler->size(), true,
                                   packet.timestamp);
                        r.assembler->consumed();
                    }
                }
                r.prefixUsed = 0;
            } else {
                if (r.contentLength < 0) {
                    r.headers += char(byte);
                    if (r.headers.length() > 4096) {
                        fail("mjpeg_header_limit");
                        return;
                    }
                    if (r.headers.endsWith("\r\n\r\n")) {
                        if((r.headers.startsWith("HTTP/1.1 401")||r.headers.startsWith("HTTP/1.0 401"))&&
                            r.authRetries++==0&&r.user.length()){
                            if(!r.challenge(field(r.headers,"www-authenticate"))){fail("http_auth_unsupported");return;}
                            r.socket.stop();IPAddress ip;
                            if(!localIp(opts.host,ip)||!r.socket.connect(ip,opts.port,500)){fail("http_auth_connect");return;}
                            if(!r.httpRequest())fail("http_auth_write");return;
                        }
                        if (!r.headers.startsWith("HTTP/1.1 200") &&
                            !r.headers.startsWith("HTTP/1.0 200")) {
                            fail("mjpeg_status");
                            return;
                        }
                        if (field(r.headers, "transfer-encoding").length()) {
                            fail("mjpeg_chunked_unsupported");
                            return;
                        }
                        String contentType=field(r.headers,"content-type");contentType.toLowerCase();
                        r.singleImage=contentType.startsWith("image/jpeg");
                        r.contentLength = 0;
                        r.headers = "";
                        std::lock_guard<std::mutex> guard(mutex_);
                        state_.connecting = false;
                    }
                    continue;
                }
                if (!r.jpegStarted) {
                    if (r.prefixUsed && byte == 0xd8) {
                        r.encoded[0] = 0xff;
                        r.encoded[1] = 0xd8;
                        r.mjpegUsed = 2;
                        r.jpegStarted = true;
                    }
                    r.prefixUsed = byte == 0xff ? 1 : 0;
                    continue;
                }
                if (r.mjpegUsed >= encodedLimit) {
                    fail("mjpeg_frame_limit");
                    return;
                }
                r.encoded[r.mjpegUsed++] = byte;
                if (r.mjpegUsed > 2 && byte == 0xd9 && r.encoded[r.mjpegUsed - 2] == 0xff) {
                    const bool accepted=submit(r.encoded, r.mjpegUsed, false);
                    if(r.singleImage){if(!accepted){fail("jpeg_snapshot_queue");return;}r.imageSubmitted=true;return;}
                    r.jpegStarted = false;
                    r.prefixUsed = 0;
                    r.mjpegUsed = 0;
                }
            }
        }
        if (r.phase <= Runtime::Play && int32_t(millis() - r.deadline) >= 0)
            fail("rtsp_handshake_timeout");
        if(r.phase==Runtime::Mjpeg&&r.contentLength<0&&int32_t(millis()-r.deadline)>=0)
            fail("http_handshake_timeout");
    }
    bool VideoService::submit(const uint8_t *bytes, size_t count, bool h264, uint32_t stamp) {
        if (!NetworkWorker::instance().isWorkerContext() || !bytes || !count ||
            count > encodedLimit)
            return false;
        if (decoding_.exchange(true)) {
            if (h264)
                resync_ = true;
            std::lock_guard<std::mutex> guard(mutex_);
            ++state_.dropped;
            return false;
        }
        auto *job = new (std::nothrow) DecodeJob{this};
        if (!job) {
            decoding_ = false;
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            job->generation = state_.generation;
            job->options = options_;
            if (!state_.active) {
                delete job;
                decoding_ = false;
                return false;
            }
        }
        job->bytes = static_cast<uint8_t *>(heap_caps_aligned_alloc(
            64, (count + 63) & ~size_t(63), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        job->size = count;
        job->h264 = h264;
        job->stamp = stamp;
        if (job->bytes)
            memcpy(job->bytes, bytes, count);
        if (!job->bytes ||
            !NetworkWorker::instance().submitTransfer(decodeJob, job, "video-decode")) {
            heap_caps_free(job->bytes);
            delete job;
            decoding_ = false;
            std::lock_guard<std::mutex> guard(mutex_);
            ++state_.dropped;
            return false;
        }
        return true;
    }
    void VideoService::decodeJob(void *opaque) {
        auto *job = static_cast<DecodeJob *>(opaque);
        job->service->decode(job);
        heap_caps_free(job->bytes);
        delete job;
    }
    void VideoService::decode(void *opaque) {
        auto &job = *static_cast<DecodeJob *>(opaque);
        auto *decoder = static_cast<Decoder *>(decoderState_);
        const auto fail = [this, generation = job.generation](const char *code) {
            this->fail(code, generation);
        };
        bool current;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            current = job.bytes && state_.active && state_.generation == job.generation;
        }
        if (!current) {
            delete decoder;
            decoderState_ = nullptr;
            decoderAlive_ = false;
            decoding_ = false;
            return;
        }
        int slot = -1;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (unsigned i = 0; i < 3; ++i)
                if (images_[i].expired()) {
                    slot = i;
                    break;
                }
        }
        if (slot < 0) {
            if (decoder)
                decoder->needIdr = true;
            std::lock_guard<std::mutex> guard(mutex_);
            ++state_.dropped;
            decoding_ = false;
            return;
        }
        auto image = std::shared_ptr<DecodedImage>(new (std::nothrow) DecodedImage);
        if (!image) {
            decoding_ = false;
            return;
        }
        if (job.h264) {
            if (!decoder || decoder->generation != job.generation) {
                delete decoder;
                decoder = new (std::nothrow) Decoder;
                decoderState_ = decoder;
                if (!decoder) {
                    fail("h264_memory");
                    decoding_ = false;
                    return;
                }
                decoderAlive_ = true;
                decoder->generation = job.generation;
                esp_h264_dec_cfg_sw_t cfg{};
                cfg.pic_type = ESP_H264_RAW_FMT_I420;
                if (esp_h264_dec_sw_new(&cfg, &decoder->handle) != ESP_H264_ERR_OK ||
                    esp_h264_dec_open(decoder->handle) != ESP_H264_ERR_OK) {
                    fail("h264_init");
                    decoding_ = false;
                    return;
                }
            }
            bool idr = false;
            for (size_t i = 0; i + 4 < job.size;) {
                size_t start = i;
                while (start + 3 < job.size &&
                       !(job.bytes[start] == 0 && job.bytes[start + 1] == 0 &&
                         job.bytes[start + 2] == 1) &&
                       !(start + 4 < job.size && job.bytes[start] == 0 &&
                         job.bytes[start + 1] == 0 && job.bytes[start + 2] == 0 &&
                         job.bytes[start + 3] == 1))
                    ++start;
                if (start + 3 >= job.size)
                    break;
                const size_t nal = start + (job.bytes[start + 2] == 1 ? 3 : 4);
                size_t end = nal + 1;
                while (end + 3 < job.size &&
                       !(job.bytes[end] == 0 && job.bytes[end + 1] == 0 &&
                         (job.bytes[end + 2] == 1 ||
                          (job.bytes[end + 2] == 0 && job.bytes[end + 3] == 1))))
                    ++end;
                if (end + 3 >= job.size)
                    end = job.size;
                const unsigned type = job.bytes[nal] & 31;
                if (type == 7) {
                    intercom::SpsInfo info;
                    if (!intercom::baselineSps(job.bytes + nal, end - nal, info) ||
                        info.width > job.options.width || info.height > job.options.height) {
                        fail("h264_sps_unsupported");
                        decoding_ = false;
                        return;
                    }
                    decoder->validSps = true;
                }
                if (type == 5)
                    idr = true;
                i = end;
            }
            if (resync_.exchange(false))
                decoder->needIdr = true;
            if (!decoder->validSps || (decoder->needIdr && !idr)) {
                decoding_ = false;
                return;
            }
            if (idr)
                decoder->needIdr = false;
            esp_h264_dec_in_frame_t input{};
            input.raw_data = {job.bytes, uint32_t(job.size)};
            input.pts = job.stamp;
            esp_h264_dec_out_frame_t output{};
            const uint32_t started = millis();
            while (input.raw_data.len) {
                input.consume = 0;
                output = {};
                if (esp_h264_dec_process(decoder->handle, &input, &output) != ESP_H264_ERR_OK ||
                    !input.consume || input.consume > input.raw_data.len) {
                    decoder->needIdr = true;
                    break;
                }
                input.raw_data.buffer += input.consume;
                input.raw_data.len -= input.consume;
                if (output.out_size && output.outbuf) {
                    esp_h264_dec_param_sw_handle_t params = nullptr;
                    esp_h264_resolution_t res{};
                    esp_h264_dec_sw_get_param_hd(decoder->handle, &params);
                    esp_h264_dec_get_resolution(params, &res);
                    unsigned w = res.width, h = res.height;
                    if (!w || !h || w > job.options.width || h > job.options.height || w % 2 ||
                        h % 2 || output.out_size < size_t(w) * h * 3 / 2) {
                        fail("h264_dimensions");
                        break;
                    }
                    if (!image->pixels)
                        image->pixels = static_cast<uint8_t *>(
                            heap_caps_malloc(size_t(job.options.width) * job.options.height * 2,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                    if (!image->pixels) {
                        fail("video_frame_memory");
                        break;
                    }
                    image->width = w;
                    image->height = h;
                    image->rgb565 = true;
                    const auto *y = output.outbuf;
                    const auto *u = y + w * h;
                    const auto *v = u + w * h / 4;
                    auto *rgb = reinterpret_cast<uint16_t *>(image->pixels);
                    for (unsigned row = 0; row < h; ++row)
                        for (unsigned col = 0; col < w; ++col) {
                            int yy = int(y[row * w + col]) - 16,
                                uu = int(u[(row / 2) * (w / 2) + col / 2]) - 128,
                                vv = int(v[(row / 2) * (w / 2) + col / 2]) - 128;
                            int red = clipped((298 * yy + 409 * vv + 128) >> 8),
                                green = clipped((298 * yy - 100 * uu - 208 * vv + 128) >> 8),
                                blue = clipped((298 * yy + 516 * uu + 128) >> 8);
                            rgb[row * w + col] =
                                ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
                        }
                }
                if (uint32_t(millis() - started) > 150) {
                    decoder->needIdr = true;
                    break;
                }
            }
        } else {
            jpeg_decode_picture_info_t info{};
            if (jpeg_decoder_get_info(job.bytes, job.size, &info) != ESP_OK || !info.width ||
                !info.height || info.width > job.options.width ||
                info.height > job.options.height) {
                fail("jpeg_dimensions");
                decoding_ = false;
                return;
            }
            const unsigned paddedWidth = (info.width + 15) & ~15u,
                           paddedHeight = (info.height + 15) & ~15u;
            jpeg_decode_engine_cfg_t cfg{};
            cfg.timeout_ms = 150;
            jpeg_decoder_handle_t engine = nullptr;
            if (jpeg_new_decoder_engine(&cfg, &engine) == ESP_OK) {
                jpeg_decode_memory_alloc_cfg_t alloc{};
                alloc.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
                size_t allocated = 0;
                image->pixels = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(
                    size_t(paddedWidth) * paddedHeight * 2, &alloc, &allocated));
                jpeg_decode_cfg_t format{};
                format.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
                format.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
                format.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
                uint32_t actual = 0;
                if (image->pixels &&
                    jpeg_decoder_process(engine, &format, job.bytes, job.size, image->pixels,
                                         allocated, &actual) == ESP_OK &&
                    actual >= size_t(paddedWidth) * paddedHeight * 2) {
                    for (unsigned row = 1; row < info.height; ++row)
                        if (paddedWidth != info.width)
                            memmove(image->pixels + row * info.width * 2,
                                    image->pixels + row * paddedWidth * 2, info.width * 2);
                    image->width = info.width;
                    image->height = info.height;
                    image->rgb565 = true;
                }
                jpeg_del_decoder_engine(engine);
            }
            if (!image->width) {
                fail("jpeg_decode");
                decoding_ = false;
                return;
            }
        }
        if (image->width) {
            std::lock_guard<std::mutex> guard(mutex_);
            if (state_.active && state_.generation == job.generation) {
                const uint32_t interval = 1000 / job.options.fps;
                if (!latest_ || uint32_t(millis() - latest_->at) >= interval) {
                    images_[slot] = image;
                    auto frame = std::shared_ptr<VideoFrame>(new (std::nothrow) VideoFrame);
                    if (frame) {
                        frame->image = image;
                        frame->generation = job.generation;
                        frame->revision = ++state_.revision;
                        frame->at = millis();
                        latest_ = frame;
                        ++state_.frames;
                        state_.lastFrameAt=frame->at;state_.width=image->width;state_.height=image->height;
                        strlcpy(state_.codec,job.h264?"H264":"JPEG",sizeof(state_.codec));
                        const auto elapsed=uint32_t(frame->at-fpsAt_);
                        if(elapsed>=1000){state_.fpsMilli=(state_.frames-fpsFrames_)*1000000UL/elapsed;fpsAt_=frame->at;fpsFrames_=state_.frames;}
                        uiPending_ = true;
                    }
                }
            }
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!state_.active || state_.generation != job.generation) {
                delete decoder;
                decoderState_ = nullptr;
                decoderAlive_ = false;
            }
        }
        decoding_ = false;
    }
} // namespace hp
