// stb_image.h - заглушка с реализацией
// Полную версию можно скачать с https://github.com/nothings/stb

#ifndef STB_IMAGE_H
#define STB_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char stbi_uc;

stbi_uc *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);
void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

#ifdef __cplusplus
}
#endif

#endif // STB_IMAGE_H

#ifdef STB_IMAGE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_flip_vertically = 0;

void stbi_set_flip_vertically_on_load(int flag) {
    s_flip_vertically = flag;
}

stbi_uc *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp) {
    (void)filename;
    (void)req_comp;
    
    // Создаём простую текстуру 2x2
    *x = 2;
    *y = 2;
    *comp = 3;
    
    stbi_uc *data = (stbi_uc*)malloc(2 * 2 * 3);
    if (data) {
        // Простой шахматный паттерн
        data[0] = 255; data[1] = 0; data[2] = 0;      // Красный
        data[3] = 0; data[4] = 255; data[5] = 0;      // Зелёный
        data[6] = 0; data[7] = 0; data[8] = 255;      // Синий
        data[9] = 255; data[10] = 255; data[11] = 0;  // Жёлтый
    }
    return data;
}

void stbi_image_free(void *data) {
    free(data);
}

#endif // STB_IMAGE_IMPLEMENTATION
