#include "image.h"
#include "support.h"

/* Decode PNG with the real OS codec, outside DllMain. Submit linear RGBA8
   pixels through FH2's raw-image path, which does not use its empty WIC GUIDs.
 */
#define CINTERFACE
#define COBJMACROS
#include <wincodec.h>
typedef HRESULT(WINAPI *GamerpicWicFactoryFn)(UINT, IWICImagingFactory **);
static const GUID gamerpic_rgba = {
    0xf5c7ad2d, 0x6a8d, 0x43dd, {0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9}};

unsigned char *image_decode_png(const unsigned char *png_bytes, DWORD png_size, DWORD *output_size,
                                UINT *output_width, UINT *output_height) {
    *output_size = 0;
    *output_width = 0;
    *output_height = 0;
    HMODULE codec = LoadLibraryExW(L"WindowsCodecs.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!codec) {
        return 0;
    }
    GamerpicWicFactoryFn create_factory =
        (GamerpicWicFactoryFn)(uintptr_t)GetProcAddress(codec, "WICCreateImagingFactory_Proxy");
    IWICImagingFactory *factory = 0;
    IWICStream *stream = 0;
    IWICBitmapDecoder *decoder = 0;
    IWICBitmapFrameDecode *frame = 0;
    IWICFormatConverter *converter = 0;
    unsigned char *pixels = 0;
    UINT width = 0, height = 0;
    DWORD pixel_bytes = 0;
    if (!create_factory || FAILED(create_factory(0x237, &factory)) || !factory) {
        goto done;
    }
    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream)) || !stream) {
        goto done;
    }
    if (FAILED(IWICStream_InitializeFromMemory(stream, (BYTE *)(uintptr_t)png_bytes, png_size))) {
        goto done;
    }
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(
            factory, (IStream *)stream, 0, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        !decoder) {
        goto done;
    }
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) || !frame) {
        goto done;
    }
    if (FAILED(IWICBitmapFrameDecode_GetSize(frame, &width, &height)) || !width || !height ||
        width > 2048 || height > 2048) {
        goto done;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) || !converter) {
        goto done;
    }
    if (FAILED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &gamerpic_rgba,
                                              WICBitmapDitherTypeNone, 0, 0,
                                              WICBitmapPaletteTypeCustom))) {
        goto done;
    }
    /* FH2 chooses a 1D texture for height=1; repeat that row to keep a 2D image.
     */
    UINT texture_height = height == 1 ? 2 : height;
    pixel_bytes = width * texture_height * 4u;
    pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pixel_bytes);
    if (!pixels) {
        goto done;
    }
    if (FAILED(IWICFormatConverter_CopyPixels(converter, 0, width * 4u, pixel_bytes, pixels))) {
        HeapFree(GetProcessHeap(), 0, pixels);
        pixels = 0;
        goto done;
    }
    if (height == 1) {
        memory_copy(pixels + width * 4u, pixels, width * 4u);
    }
    *output_size = pixel_bytes;
    *output_width = width;
    *output_height = texture_height;
done:
    if (converter) {
        IWICFormatConverter_Release(converter);
    }
    if (frame) {
        IWICBitmapFrameDecode_Release(frame);
    }
    if (decoder) {
        IWICBitmapDecoder_Release(decoder);
    }
    if (stream) {
        IWICStream_Release(stream);
    }
    if (factory) {
        IWICImagingFactory_Release(factory);
    }
    FreeLibrary(codec);
    return pixels;
}
