// SPDX-License-Identifier: MIT
#ifndef RADISH_NCNN_ADA_SNAKE_H
#define RADISH_NCNN_ADA_SNAKE_H
#include "net.h"
namespace radish_ncnn {
// Registers RadishAdaSnake (see radish_ada_snake.cpp) on a Net before load_param.
int register_ada_snake(ncnn::Net& net);
}
#endif
