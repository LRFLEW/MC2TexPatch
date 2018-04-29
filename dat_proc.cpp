#include "dat_proc.hpp"

#include <cstddef>
#include <cstdint>

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

#include "fix_dxt.hpp"
#include "mc2_exception.hpp"

class zlib_exception : public std::exception {
public:
    zlib_exception(int err, z_stream *strm) {
        std::stringstream out(msg);
        out << "Zlib error " << err << ": " << strm->msg;
    }
    virtual const char *what() const noexcept override { return msg.c_str(); }

private:
    std::string msg;
};

struct dat_header { std::uint32_t magic, numFiles, metaLen, nameLen; };
struct file_info { std::uint32_t nameOffset, dataOffset, decompressLen, compressLen; };

constexpr std::uint32_t MAGIC_DAVE = 0x45564144;
constexpr std::uint32_t MAGIC_Dave = 0x65766144;

constexpr char chartable[65] = "\0 #$()-./?0123456789_abcdefghijklmnopqrstuvwxyz~++++++++++++++++";

int Zlib_Compression_Level = Z_DEFAULT_COMPRESSION;

template<class T> static void helper_read_at(std::istream &in, const std::streampos pos, T &t) {
    in.seekg(pos);
    in.read(reinterpret_cast<std::istream::char_type *>(&t), sizeof(T));
}

template<class T> static void helper_read_at(std::istream &in, const std::streampos pos, std::vector<T> &v) {
    in.seekg(pos);
    in.read(reinterpret_cast<std::istream::char_type *>(v.data()), v.size() * sizeof(T));
}

template<class T> static void helper_write_at(std::ostream &out, const std::streampos pos, const T &t) {
    out.seekp(pos);
    out.write(reinterpret_cast<const std::ostream::char_type *>(&t), sizeof(T));
}

template<class T> static void helper_write_at(std::ostream &out, const std::streampos pos, const std::vector<T> &v) {
    out.seekp(pos);
    out.write(reinterpret_cast<const std::ostream::char_type *>(v.data()), v.size() * sizeof(T));
}

template<class T> static std::uint32_t helper_write_pad(std::ostream &out, const std::vector<T> &v) {
    const size_t padding = (2048 - (out.tellp() % 2048)) % 2048;
    // Don't pad if data can fit in padding
    if (v.size() * sizeof(T) > padding) out.seekp(padding, std::ios_base::cur);
    std::uint32_t output = static_cast<std::uint32_t>(out.tellp());
    out.write(reinterpret_cast<const std::ostream::char_type *>(v.data()), v.size() * sizeof(T));
    return output;
}

static bool decompress(const std::vector<char> &compressed, std::vector<char> &decompressed) {
    if (decompressed.size() < FixingSize) return false;

    int ret;
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    ret = inflateInit2(&strm, -MAX_WBITS);
    if (ret != Z_OK) throw zlib_exception(ret, &strm);

    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    strm.avail_out = static_cast<uInt>(FixingSize);
    strm.next_out = reinterpret_cast<Bytef *>(decompressed.data());

    ret = inflate(&strm, Z_SYNC_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) throw zlib_exception(ret, &strm);;
    if (strm.avail_out != 0) throw mc2_exception("Unable to decompress Tex header");
    if (ret == Z_STREAM_END || !needs_fixing(decompressed)) {
        ret = inflateEnd(&strm);
        if (ret != Z_OK) throw zlib_exception(ret, &strm);;
        return false;
    }

    strm.avail_out = static_cast<uInt>(decompressed.size() - FixingSize);
    ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) throw zlib_exception(ret, &strm);;
    if (strm.avail_out != 0) throw mc2_exception("Decompressed size incorrect");
    if (strm.avail_in != 0) throw mc2_exception("Compressed size incorrect");

    /* clean up and return */
    ret = inflateEnd(&strm);
    if (ret != Z_OK) throw zlib_exception(ret, &strm);;
    return true;
}

static bool compress(const std::vector<char> &decompressed, std::vector<char> &compressed) {
    int ret;
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    ret = deflateInit2(&strm, Zlib_Compression_Level, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) throw zlib_exception(ret, &strm);;

    /* compress until end of file */
    strm.avail_in = static_cast<uInt>(decompressed.size());
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(decompressed.data()));
    strm.avail_out = static_cast<uInt>(compressed.size());
    strm.next_out = reinterpret_cast<Bytef *>(compressed.data());

    ret = deflate(&strm, Z_FINISH);    /* no bad return value */
    if (ret == Z_OK) {
        // compression increases file size, so abort it
        ret = deflateEnd(&strm);
        if (ret != Z_OK) throw zlib_exception(ret, &strm);;
        return false;
    }
    if (ret != Z_STREAM_END) throw zlib_exception(ret, &strm);;
    if (strm.avail_in != 0) throw mc2_exception("Texture not completely compressed?");
    compressed.resize(compressed.size() - strm.avail_out);

    ret = deflateEnd(&strm);
    if (ret != Z_OK) throw zlib_exception(ret, &strm);;
    return true;
}

static std::uint8_t helper_getBase64(const std::vector<std::uint8_t> &n, const std::uint32_t l, const std::uint32_t i) {
    const size_t k = i / 4;
    switch (i & 0x3) {
        case 0: return ((n[l + 3*k + 0] & 0x3F) << 0)                        ; break;
        case 1: return ((n[l + 3*k + 1] & 0x0F) << 2) | (n[l + 3*k + 0] >> 6); break;
        case 2: return ((n[l + 3*k + 2] & 0x03) << 4) | (n[l + 3*k + 1] >> 4); break;
        case 3: return                                  (n[l + 3*k + 2] >> 2); break;
        default: return -1; // Won't ever happen
    }
}

static std::string helper_decode64(const std::vector<std::uint8_t> &names, std::vector<char> &nameBuffer, file_info file) {
    uint32_t i = 0;
    {
        /*
        * Apparent Delta Encoding Scheme:
        * First:  111 CBA
        * Second: 10G FED
        *
        * The total number of characters to keep
        * is the binary value 0GFE DCBA
        */
        std::uint8_t v = helper_getBase64(names, file.nameOffset, 0);
        if (v >= 0x30) {
            std::uint8_t t = helper_getBase64(names, file.nameOffset, 1);
            if ((v & 0x78) != 0x38 || (t & 0x70) != 0x20) throw mc2_exception("Invalid Delta Encoding in Base64 DAT");
            i = 2;
            nameBuffer.resize((v & 0x07) | ((t & 0x0F) << 3));
        } else nameBuffer.clear();
    }
    char c;
    do {
        std::uint8_t v = helper_getBase64(names, file.nameOffset, i++);
        c = chartable[v];
        nameBuffer.push_back(c);
        if (c == '+') throw mc2_exception("Invalid Character in Name for Base64 DAT");
    } while (c != '\0');
    return nameBuffer.data();
}

void process_textures(std::istream &in, std::ostream &out) {
    std::ios_base::iostate in_exc = in.exceptions(), out_exc = out.exceptions();
    in.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    out.exceptions(std::ios_base::failbit | std::ios_base::badbit);

    dat_header header;
    helper_read_at(in, 0, header);
    helper_write_at(out, 0, header);
    
    bool isBase64;
    if (header.magic == MAGIC_DAVE) isBase64 = false;
    else if (header.magic == MAGIC_Dave) isBase64 = true;
    else throw mc2_exception("Unknown DAT file format. Maybe a ZIP file?");
    
    std::vector<file_info> files(header.numFiles);
    helper_read_at(in, 2048, files);
    
    std::vector<std::uint8_t> names(header.nameLen);
    helper_read_at(in, 2048 + header.metaLen, names);
    helper_write_at(out, 2048 + header.metaLen, names);
    
    std::vector<char> nameBuffer;
    std::vector<char> outputBuffer;
    std::vector<char> compressBuffer;
    
    out.seekp(2048 + header.metaLen + header.nameLen);
    for (file_info &file : files) {
        std::string name;
        if (isBase64) name = helper_decode64(names, nameBuffer, file);
        else name = (char *) &names[file.nameOffset];

        compressBuffer.resize(file.compressLen);
        helper_read_at(in, file.dataOffset, compressBuffer);

        // check if file extension is .tex
        if (name.length() >= 4 && name.compare(name.length() - 4, 4, ".tex") == 0) {
            bool fix;
            if (file.compressLen < file.decompressLen) {
                outputBuffer.resize(file.decompressLen);
                fix = decompress(compressBuffer, outputBuffer);
            } else if (file.compressLen == file.decompressLen) {
                outputBuffer = compressBuffer;
                fix = needs_fixing(outputBuffer);
            } else throw mc2_exception("Compressed texture larger than decompressed is invalid");

            if (fix) {
                std::cout << name << " - " << std::flush;
                bool modified = fix_dxt(outputBuffer);
                if (modified) {
                    file.decompressLen = static_cast<uint32_t>(outputBuffer.size());
                    compressBuffer.resize(outputBuffer.size() - 1);
                    bool smaller = compress(outputBuffer, compressBuffer);
                    if (!smaller) std::swap(outputBuffer, compressBuffer);
                    file.compressLen = static_cast<uint32_t>(compressBuffer.size());
                    std::cout << "Patched" << std::endl;
                } else {
                    std::cout << "Good" << std::endl;
                }
            }
        }

        file.dataOffset = helper_write_pad(out, compressBuffer);
    }
    
    // pad end of file
    out.seekp((((std::streamoff) out.tellp() + 2047) & ~((std::streamoff) 2047)) - 1, std::ios_base::beg);
    out.put('\0');
    
    // write file directory
    std::cout << "Writing new File Directory" << std::endl;
    helper_write_at(out, 2048, files);

    in.exceptions(in_exc), out.exceptions(out_exc);
}
