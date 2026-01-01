//
// Created by ASUS on 12/7/2025.
//

#pragma once

#include <string>
#include <vector>
#include <stdexcept>

namespace base64 {
    inline std::vector<uint8_t> decode(const std::string &input) {
        static const std::string base64_chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789+/";

        std::vector<uint8_t> out;
        unsigned int val = 0;
        int bits = 0;
        for (unsigned char c : input) {
            if (std::isspace(c)) continue;
            if (c == '=') break;
            auto idx = base64_chars.find(c);
            if (idx == std::string::npos) throw std::invalid_argument("Invalid Base64 character");
            val = (val << 6) | static_cast<unsigned int>(idx);
            bits += 6;
            while (bits >= 8) {
                bits -= 8;
                unsigned int byte = (val >> bits) & 0xFFu;
                out.push_back(static_cast<uint8_t>(byte));
            }
        }
        return out;
    }

    inline std::string encode(const std::vector<uint8_t> &data) {
        static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        unsigned int val = 0;
        int valb = -6; // how many bits we have in val
        for (uint8_t c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                unsigned int index = (val >> valb) & 0x3F;
                out.push_back(tbl[index]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            unsigned int index = ((val << (6 + valb)) & 0x3F);
            out.push_back(tbl[index]);
        }
        while (out.size() % 4) out.push_back('=');
        return out;
    }
} // namespace base64
