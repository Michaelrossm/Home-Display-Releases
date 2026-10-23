#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
namespace hp::intercom {
    inline int16_t mulawDecode(uint8_t v) {
        v = ~v;
        int t = ((v & 15) << 3) + 132;
        t <<= (v & 112) >> 4;
        return (v & 128) ? 132 - t : t - 132;
    }
    inline uint8_t mulawEncode(int16_t sample) {
        int v = sample, sign = v < 0 ? 128 : 0;
        if (v < 0)
            v = -v;
        v = std::min(v, 32635) + 132;
        unsigned exponent = 7;
        for (int mask = 0x4000; exponent && !(v & mask); mask >>= 1)
            --exponent;
        return ~(sign | (exponent << 4) | ((v >> (exponent + 3)) & 15));
    }
    inline int16_t alawDecode(uint8_t a) {
        a ^= 0x55;
        int v = (a & 15) << 4;
        unsigned seg = (a & 112) >> 4;
        if (!seg)
            v += 8;
        else {
            v += 264;
            if (seg > 1)
                v <<= seg - 1;
        }
        return (a & 128) ? v : -v;
    }
    inline uint8_t alawEncode(int16_t sample) {
        int v = sample;
        const uint8_t mask = v >= 0 ? 0xd5 : 0x55;
        if (v < 0)
            v = -v - 1;
        v = std::min(v, 32767);
        unsigned seg = 0;
        for (int bound = 255; seg < 7 && v > bound; ++seg, bound = (bound << 1) | 1) {
        };
        return ((seg << 4) | ((v >> (seg ? seg + 3 : 4)) & 15)) ^ mask;
    }
    struct RtpPacket {
        const uint8_t *data = nullptr;
        size_t size = 0;
        uint16_t sequence = 0;
        uint32_t timestamp = 0, ssrc = 0;
        uint8_t type = 0;
        bool marker = false;
        static uint16_t be16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
        static uint32_t be32(const uint8_t *p) {
            return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        }
        bool parse(const uint8_t *bytes, size_t n) {
            if (!bytes || n < 12 || (bytes[0] >> 6) != 2)
                return false;
            size_t off = 12 + 4 * (bytes[0] & 15);
            if (off > n)
                return false;
            if (bytes[0] & 16) {
                if (n - off < 4)
                    return false;
                const size_t ext = 4 + 4 * be16(bytes + off + 2);
                if (ext > n - off)
                    return false;
                off += ext;
            }
            if (bytes[0] & 32) {
                const unsigned pad = bytes[n - 1];
                if (!pad || pad > n - off)
                    return false;
                n -= pad;
            }
            if (off >= n)
                return false;
            data = bytes + off;
            size = n - off;
            sequence = be16(bytes + 2);
            timestamp = be32(bytes + 4);
            ssrc = be32(bytes + 8);
            type = bytes[1] & 127;
            marker = bytes[1] & 128;
            return true;
        }
    };
    // RFC 6184 single NAL / STAP-A / FU-A, caller-owned bounded Annex-B buffer.
    class H264Assembler {
        uint8_t *out_;
        size_t capacity_, used_ = 0;
        uint16_t next_ = 0;
        uint32_t stamp_ = 0, ssrc_ = 0;
        bool sequence_ = false, fragment_ = false, lost_ = false, gap_ = false;
        bool append(const uint8_t *p, size_t n) {
            if (n > capacity_ - used_) {
                lost_ = true;
                return false;
            }
            memcpy(out_ + used_, p, n);
            used_ += n;
            return true;
        }
        bool nal(const uint8_t *p, size_t n) {
            static const uint8_t start[] = {0, 0, 0, 1};
            return n && append(start, 4) && append(p, n);
        }

      public:
        H264Assembler(uint8_t *p, size_t capacity) : out_(p), capacity_(capacity) {}
        void reset() {
            used_ = 0;
            fragment_ = false;
            lost_ = false;
            sequence_ = false;
            gap_ = false;
        }
        bool gap() const { return gap_; }
        size_t size() const { return used_; }
        const uint8_t *data() const { return out_; }
        bool packet(const RtpPacket &p) {
            if (!p.size)
                return false;
            gap_ = sequence_ && (p.ssrc != ssrc_ || p.sequence != next_);
            if (sequence_ && (p.timestamp != stamp_ || p.ssrc != ssrc_)) {
                used_ = 0;
                fragment_ = false;
                lost_ = false;
            }
            if (sequence_ && p.ssrc == ssrc_ && p.timestamp == stamp_ && p.sequence != next_)
                lost_ = true;
            stamp_ = p.timestamp;
            ssrc_ = p.ssrc;
            next_ = p.sequence + 1;
            sequence_ = true;
            const unsigned type = p.data[0] & 31;
            if (!lost_ && type > 0 && type < 24) {
                if (fragment_)
                    lost_ = true;
                else
                    nal(p.data, p.size);
            } else if (!lost_ && type == 24) {
                size_t i = 1;
                while (i < p.size) {
                    if (p.size - i < 2) {
                        lost_ = true;
                        break;
                    }
                    size_t n = RtpPacket::be16(p.data + i);
                    i += 2;
                    if (!n || n > p.size - i || (p.data[i] & 31) >= 24) {
                        lost_ = true;
                        break;
                    }
                    if (!nal(p.data + i, n))
                        break;
                    i += n;
                }
            } else if (!lost_ && type == 28) {
                if (p.size < 3)
                    lost_ = true;
                else {
                    const uint8_t h = p.data[1];
                    if ((h & 31) == 0 || (h & 31) >= 24 || (h & 32) || (h & 192) == 192)
                        lost_ = true;
                    else if (h & 128) {
                        if (fragment_)
                            lost_ = true;
                        else {
                            uint8_t n = (p.data[0] & 224) | (h & 31);
                            fragment_ = true;
                            nal(&n, 1);
                            append(p.data + 2, p.size - 2);
                        }
                    } else if (!fragment_)
                        lost_ = true;
                    else {
                        append(p.data + 2, p.size - 2);
                        if (h & 64)
                            fragment_ = false;
                    }
                }
            } else if (!lost_)
                lost_ = true;
            if (p.marker) {
                const bool ready = !lost_ && !fragment_ && used_;
                if (!ready) {
                    used_ = 0;
                    fragment_ = false;
                }
                return ready;
            }
            return false;
        }
        void consumed() {
            used_ = 0;
            fragment_ = false;
            lost_ = false;
        }
    };
    // Validate Baseline SPS BEFORE passing it to the allocating software decoder.
    struct SpsInfo {
        unsigned width = 0, height = 0, refs = 0;
    };
    inline bool baselineSps(const uint8_t *nal, size_t size, SpsInfo &info) {
        if (!nal || size < 5 || (nal[0] & 31) != 7 || size > 256)
            return false;
        uint8_t rbsp[256];
        unsigned n = 0, zeros = 0;
        for (size_t i = 1; i < size; ++i) {
            if (zeros >= 2 && nal[i] == 3) {
                zeros = 0;
                continue;
            }
            rbsp[n++] = nal[i];
            zeros = nal[i] == 0 ? zeros + 1 : 0;
        }
        struct Bits {
            const uint8_t *p;
            unsigned n, pos = 0;
            bool ok = true;
            uint32_t get(unsigned k) {
                uint32_t v = 0;
                if (k > 32 || k > n * 8 - pos) {
                    ok = false;
                    return 0;
                }
                while (k--) {
                    v = (v << 1) | ((p[pos / 8] >> (7 - pos % 8)) & 1);
                    ++pos;
                }
                return v;
            }
            uint32_t ue() {
                unsigned z = 0;
                while (ok && !get(1)) {
                    if (++z > 20) {
                        ok = false;
                        return 0;
                    }
                }
                return ((1u << z) - 1) + get(z);
            }
        } b{rbsp, n};
        if (b.get(8) != 66)
            return false;
        b.get(8);
        b.get(8);
        b.ue();
        if (b.ue() > 12)
            return false;
        auto poc = b.ue();
        if (poc == 0) {
            if (b.ue() > 12)
                return false;
        } else if (poc == 1) {
            b.get(1);
            b.ue();
            b.ue();
            unsigned count = b.ue();
            if (count > 32)
                return false;
            while (count--)
                b.ue();
        } else if (poc != 2)
            return false;
        info.refs = b.ue();
        if (info.refs > 2)
            return false;
        b.get(1);
        unsigned w = b.ue(), h = b.ue();
        if (w > 39 || h > 29)
            return false;
        if (!b.get(1))
            return false;
        b.get(1);
        unsigned left = 0, right = 0, top = 0, bottom = 0;
        if (b.get(1)) {
            left = b.ue();
            right = b.ue();
            top = b.ue();
            bottom = b.ue();
        }
        unsigned width = (w + 1) * 16, height = (h + 1) * 16;
        if (left + right >= width / 2 || top + bottom >= height / 2)
            return false;
        info.width = width - 2 * (left + right);
        info.height = height - 2 * (top + bottom);
        return b.ok && info.width && info.height;
    }
} // namespace hp::intercom
