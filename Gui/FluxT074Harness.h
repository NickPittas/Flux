#ifndef FLUX_T074_HARNESS_H
#define FLUX_T074_HARNESS_H

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

NATRON_NAMESPACE_ENTER

class Gui;

class FluxT074Harness
{
public:
    static void maybeStart(Gui* gui);
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_T074_HARNESS_H
