// Metal permits compatible float views of integer backing created with atomic usage.
// --reproduce-invalid deliberately triggers the original descriptor validation abort.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)
int main(int argc, char **argv) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { fprintf(stderr, "Metal device unavailable\n"); return 77; }
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:4 height:1 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsagePixelFormatView;
        if (argc == 2 && !strcmp(argv[1], "--reproduce-invalid")) {
            d.usage |= MTLTextureUsageShaderAtomic;
            (void)[dev newTextureWithDescriptor:d];
            fprintf(stderr, "invalid descriptor unexpectedly accepted\n");
            return 1;
        }
        id<MTLTexture> typed_float = [dev newTextureWithDescriptor:d];
        CHECK(typed_float && typed_float.pixelFormat == MTLPixelFormatR32Float);
        CHECK(!(typed_float.usage & MTLTextureUsageShaderAtomic));
        MTLPixelFormat integers[] = { MTLPixelFormatR32Uint, MTLPixelFormatR32Sint };
        for (unsigned i = 0; i < sizeof integers / sizeof integers[0]; i++) {
            d.pixelFormat = integers[i];
            d.usage |= MTLTextureUsageShaderAtomic;
            id<MTLTexture> t = [dev newTextureWithDescriptor:d];
            CHECK(t && t.pixelFormat == integers[i]);
            id<MTLTexture> f = [t newTextureViewWithPixelFormat:MTLPixelFormatR32Float];
            CHECK(f && f.pixelFormat == MTLPixelFormatR32Float);
            id<MTLTexture> u = [t newTextureViewWithPixelFormat:MTLPixelFormatR32Uint];
            CHECK(u && u.pixelFormat == MTLPixelFormatR32Uint);
        }
    }
    printf("atomic_texture_metal: %d failures\n", failures);
    return failures ? 1 : 0;
}
