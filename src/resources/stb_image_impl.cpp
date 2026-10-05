// Реализация stb_image
// Этот файл должен быть скомпилирован только ОДИН раз

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Запись PNG — для скриншотов редактора.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <stb_image_write.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
