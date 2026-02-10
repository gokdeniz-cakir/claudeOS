#include "math.h"

double fabs(double value)
{
    if (value < 0.0) {
        return -value;
    }

    return value;
}
