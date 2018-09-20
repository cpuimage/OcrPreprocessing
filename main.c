
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#define access _access
#else

#include <unistd.h>

#endif

#include "browse.h"

#define USE_SHELL_OPEN
#define STB_IMAGE_STATIC
//ref:
// https://github.com/nothings/stb/blob/master/stb_image.h
// https://github.com/nothings/stb/blob/master/stb_image_write.h
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>


#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

uint8_t *loadImage(const char *filename, int32_t *Width, int32_t *Height, int32_t *Channels) {

    return stbi_load(filename, Width, Height, Channels, 0);
}

void saveImage(const char *filename, int32_t Width, int32_t Height, int32_t Channels, uint8_t *Output) {
    if (!stbi_write_jpg(filename, Width, Height, Channels, Output, 100)) {
        fprintf(stderr, "save file fail.\n");
        return;
    }

#ifdef USE_SHELL_OPEN
    browse(filename);
#endif
}

void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
    const char *end;
    const char *p;
    const char *s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    } else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}

#define float2fixed(x)  (((int)((x)* 4096.0f + 0.5f)) << 8)

void rgb2ycbcr(uint8_t R, uint8_t G, uint8_t B, uint8_t *y, uint8_t *cb, uint8_t *cr) {
    *y = (int8_t) ((19595 * R + 38470 * G + 7471 * B) >> 16);
    *cb = (int8_t) (((36962 * (B - *y)) >> 16) + 128);
    *cr = (int8_t) (((46727 * (R - *y)) >> 16) + 128);
}

uint8_t clampToByte(int32_t Value) {
    return ((Value | ((255 - Value) >> 31)) & ~(Value >> 31));
}

int32_t clampI(int32_t Value, int32_t Min, int32_t Max) {
    if (Value < Min)
        return Min;
    else if (Value > Max)
        return Max;
    else
        return Value;
}

float getHueValue(uint8_t red, uint8_t green, uint8_t blue) {
    int32_t max_val, min_val, delta;
    if (blue > green) {
        max_val = blue;
        min_val = green;
    } else {
        max_val = green;
        min_val = blue;
    }
    if (red > max_val)
        max_val = red;
    else if (red < min_val)
        min_val = red;
    if (max_val == min_val) {
        return 0.0f;
    } else {
        delta = max_val - min_val;
        if (max_val == red) {
            if (green < blue)
                return 6.0f + (float) (green - blue) / delta;    //    (5, 6)
            else
                return (float) (green - blue) / delta;            // (0, 1)
        } else if (max_val == green)
            return 2.0f + (float) (blue - red) / delta;          // (1, 3)
        else
            return 4.0f + (float) (red - green) / delta;           // (3, 5)
    }
}

int32_t CPUImageLocalColorFilter(uint8_t *input,
                                 uint8_t *output,
                                 int32_t width,
                                 int32_t height,
                                 int32_t channels,
                                 uint8_t sampleR,
                                 uint8_t sampleG,
                                 uint8_t sampleB,
                                 int32_t tolerance) {
    if ((input == NULL) || (output == NULL)) return -1;
    if ((width <= 0) || (height <= 0)) return -1;
    if (channels < 3) return -1;
    sampleR = clampToByte(sampleR);
    sampleG = clampToByte(sampleG);
    sampleB = clampToByte(sampleB);
    tolerance = clampI(tolerance, 0, 100);
    float sampleHue = getHueValue(sampleR, sampleG, sampleB);
    float toleranceF = tolerance * 0.01f * 3;
    float invTolerance = 1.0f / toleranceF;
    for (int32_t Y = 0; Y < height; Y++) {
        uint8_t *scanLineIn = input + Y * width * channels;
        uint8_t *scanLineOut = output + Y * width * channels;
        for (int32_t X = 0; X < width; X++) {
            uint8_t blue = scanLineIn[2], green = scanLineIn[1], red = scanLineIn[0];
            uint8_t lum = (uint8_t) ((red * 19595 + green * 38469 + blue * 7472) >> 16);
            if (tolerance != 0) {
                float curHue = getHueValue(red, green, blue);
                float diffHue = sampleHue - curHue;
                float opacity, invOpacity, baseLum;

                if (diffHue < 0.0f) diffHue = -diffHue;
                if (diffHue > 3.0f) diffHue = 6.0f - diffHue;

                if (diffHue >= toleranceF)
                    opacity = 1.0f;
                else
                    opacity = diffHue * invTolerance * diffHue * invTolerance;

                baseLum = lum * opacity;
                invOpacity = 1.0f - opacity;

                scanLineOut[2] = clampToByte(blue * invOpacity + baseLum);
                scanLineOut[1] = clampToByte(green * invOpacity + baseLum);
                scanLineOut[0] = clampToByte(red * invOpacity + baseLum);
            } else {
                if ((blue != sampleB) || (green != sampleG) || (red != sampleR)) {
                    scanLineOut[2] = blue;
                    scanLineOut[1] = green;
                    scanLineOut[0] = red;
                } else {
                    scanLineOut[2] = lum;
                    scanLineOut[1] = lum;
                    scanLineOut[0] = lum;
                }
            }
            scanLineIn += channels;
            scanLineOut += channels;
        }
    }
    return 0;
}


uint8_t CPUImageCalcOstuThreshold(const uint32_t *histgram) {
    int32_t minValue = 0;
    for (int32_t i = 0; i < 256; i++) {
        if (histgram[i] != 0) {
            minValue = i;
            break;
        }
    }
    int32_t maxValue = 0;
    for (int32_t i = 255; i >= 0; i--) {
        if (histgram[i] != 0) {
            maxValue = i;
            break;
        }
    }
    if (maxValue == minValue)
        return maxValue;
    int32_t pixelIntegral = 0;
    int32_t pixelAmount = 0;
    for (int32_t Y = minValue; Y <= maxValue; Y++) {
        pixelIntegral += histgram[Y] * Y;
        pixelAmount += histgram[Y];
    }
    float omegaBack, omegaFore, microBack, microFore, sigma;
    float sigmaB = -1;
    int32_t threshold = 0;
    int32_t pixelBack = 0;
    int32_t pixelFore = 0;
    int32_t pixelIntegralBack = 0;
    int32_t pixelIntegralFore = 0;
    for (int32_t i = minValue; i <= maxValue; i++) {
        pixelBack = pixelBack + histgram[i];
        pixelFore = pixelAmount - pixelBack;
        omegaBack = (float) pixelBack / pixelAmount;
        omegaFore = (float) pixelFore / pixelAmount;
        pixelIntegralBack += histgram[i] * i;
        pixelIntegralFore = pixelIntegral - pixelIntegralBack;
        microBack = (float) pixelIntegralBack / pixelBack;
        microFore = (float) pixelIntegralFore / pixelFore;
        sigma = omegaBack * omegaFore * (microBack - microFore) * (microBack - microFore);
        if (sigma > sigmaB) {
            sigmaB = sigma;
            threshold = i;
        }
    }
    return threshold;
}

void CPUImageExtractOstu(const uint8_t *input, uint8_t *gray, uint32_t *histogram, int32_t width, int32_t height,
                         int32_t stride, int32_t extractChannels) {
    int32_t channels = stride / width;
    if (channels == 1) {
        return;
    }
    memset(histogram, 0, sizeof(uint32_t) * 256);
    for (int32_t y = 0; y < height; y++) {
        uint8_t *output = gray + (y * width);
        const uint8_t *in = input + (y * stride);
        for (int32_t x = 0; x < width; x++) {
            output[x] = in[extractChannels];
            histogram[in[extractChannels]]++;
            in += channels;
        }
    }
}

void CPUImageOstuFilter(uint8_t *Input, uint32_t *Histogram, int32_t Width, int32_t Height) {
    int32_t ostuThreshold = CPUImageCalcOstuThreshold(Histogram);
    uint8_t thrMap[256] = {0};
    for (int32_t i = 0; i < 256; i++) {
        if (i > ostuThreshold)
            thrMap[i] = 255;
        else
            thrMap[i] = 0;
    }
    for (int32_t Y = 0; Y < Height; Y++) {
        uint8_t *pOutput = Input + (Y * Width);
        for (int32_t X = 0; X < Width; X++) {
            pOutput[X] = thrMap[pOutput[X]];
        }
    }
}


int16_t extractWhichChannels(const uint8_t *input,
                             int32_t width,
                             int32_t height,
                             int32_t channels) {
    uint32_t histogram[256 * 2] = {0};
    memset(histogram, 0, 256 * 2 * sizeof(uint32_t));
    uint32_t *histogramCr = &histogram[0];
    uint32_t *histogramCb = &histogram[256];
    for (int32_t y = 0; y < height; y++) {
        const uint8_t *scanLineIn = input + y * width * channels;
        uint8_t Y = 0;
        uint8_t Cb = 0;
        uint8_t Cr = 0;
        for (int32_t x = 0; x < width; x++) {
            rgb2ycbcr(scanLineIn[0], scanLineIn[1], scanLineIn[2], &Y, &Cb, &Cr);
            histogramCb[Cb]++;
            histogramCr[Cr]++;
            scanLineIn += channels;
        }
    }
    uint64_t sumCb = 0;
    uint64_t sumCr = 0;

    for (int i = 0; i < 256; i++) {
        sumCb += histogramCb[i] * i;
        sumCr += histogramCr[i] * i;
    }
    if (sumCb == sumCr)
        return 1;
    uint64_t meanMax = max(sumCb, sumCr);
    if (meanMax == sumCr)
        return 0;
    return 2;
}

void colorFilterExtract(const uint8_t *input, uint8_t *output, int32_t width, int32_t height, int32_t channels,
                        uint32_t *histogram, int32_t extractChannels) {
    CPUImageExtractOstu(input, output, histogram, width, height, width * channels, extractChannels);
    CPUImageOstuFilter(output, histogram, width, height);
}

int main(int argc, char **argv) {
    printf("Image Processing \n ");
    printf("blog:http://cpuimage.cnblogs.com/ \n ");
    printf("Color Eraser\n ");
    if (argc < 2) {
        printf("usage: \n ");
        printf("%s filename \n ", argv[0]);
        printf("%s image.jpg \n ", argv[0]);
        getchar();
        return 0;
    }
    char *in_file = argv[1];
    if (access(in_file, 0) == -1) {
        printf("load file: %s fail!\n", in_file);
        return -1;
    }
    char drive[3];
    char dir[256];
    char fname[256];
    char ext[256];
    char out_file[1024];
    splitpath(in_file, drive, dir, fname, ext);
    sprintf(out_file, "%s%s%s_out.jpg", drive, dir, fname);

    int32_t width = 0;
    int32_t height = 0;
    int32_t channels = 0;
    uint8_t *input = NULL;
    input = loadImage(in_file, &width, &height, &channels);

    if (input) {
        uint8_t *output = (uint8_t *) malloc(width * channels * height * sizeof(uint8_t));
        if (output) {
            // LocalColor Version
            if (true) {
                uint8_t sampleR = 0;
                uint8_t sampleG = 255;
                uint8_t sampleB = 0;
                int32_t tolerance = 50;
                CPUImageLocalColorFilter(input, output, width, height, channels, sampleR, sampleG, sampleB, tolerance);
                // saveImage(out_file, width, height, channels, output);
                uint32_t histogram[256] = {0};
                int32_t extractChannels = extractWhichChannels(output, width, height, channels); // [0,2] rgb or bgr
                colorFilterExtract(output, output, width, height, channels, histogram, extractChannels);
                saveImage(out_file, width, height, 1, output);
            } else {
                // Simple Version
                int extractChannels = 2;
                uint32_t histogram[256] = {0};
                colorFilterExtract(input, output, width, height, channels, histogram, extractChannels);
                saveImage(out_file, width, height, 1, output);
            }
            free(output);
        }
        free(input);
    } else {
        printf("load file: %s fail!\n", in_file);
    }
    printf("press any key to exit. \n");
    getchar();
    return 0;
}