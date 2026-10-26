#include "image_decode.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <cstdio>

namespace cs2 {
namespace {
template <typename T> struct Com {
    T* ptr = nullptr;
    ~Com() { if (ptr) ptr->Release(); }
    T* operator->() const { return ptr; }
};
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
bool checked(HRESULT result, const char* stage, std::string& error) {
    if (SUCCEEDED(result)) return true;
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lx", static_cast<unsigned long>(result));
    error = std::string(stage) + " (" + code + ")";
    return false;
}
} // namespace

bool decode_preview(const std::filesystem::path& path, RgbaImage& image, std::string& error) {
    image = {};
    error.clear();
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes == 0 || bytes > 8 * 1024 * 1024) {
        error = "Preview file is missing, empty, or too large";
        return false;
    }
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        return checked(apartment.result, "Cannot initialize image decoder", error);
    Com<IWICImagingFactory> factory;
    if (!checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory.ptr)), "Cannot create image decoder", error)) return false;
    Com<IWICBitmapDecoder> decoder;
    if (!checked(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder.ptr), "Cannot read preview image", error)) return false;
    Com<IWICBitmapFrameDecode> frame;
    if (!checked(decoder->GetFrame(0, &frame.ptr), "Cannot read image frame", error)) return false;
    UINT width = 0, height = 0;
    if (!checked(frame->GetSize(&width, &height), "Cannot read image dimensions", error)) return false;
    if (width == 0 || height == 0 || width > 2048 || height > 2048) {
        error = "Preview dimensions are outside supported limits";
        return false;
    }
    Com<IWICFormatConverter> converter;
    if (!checked(factory->CreateFormatConverter(&converter.ptr), "Cannot create pixel converter", error)) return false;
    if (!checked(converter->Initialize(frame.ptr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom), "Cannot decode RGBA pixels", error)) return false;
    RgbaImage candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    if (!checked(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(candidate.pixels.size()),
        candidate.pixels.data()), "Cannot copy image pixels", error)) return false;
    image = std::move(candidate);
    return true;
}
} // namespace cs2
