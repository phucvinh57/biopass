#pragma once

#include <vector>

#include "image.h"

// HWC uint8 -> CHW float in [0, 1].
std::vector<float> imageToChw(const ImageRGB& img);

// HWC uint8 -> CHW float, mean/std normalized.
std::vector<float> imageToChwNormalized(const ImageRGB& img, const float mean[3],
                                        const float std_val[3]);
