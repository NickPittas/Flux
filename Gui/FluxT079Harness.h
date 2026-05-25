/* Env-gated autonomous validation harness for T079.
 * Inert unless FLUX_T079_AUTOMATED_PROOF=1 is set.
 */

#ifndef FLUX_T079_HARNESS_H
#define FLUX_T079_HARNESS_H

#include "Global/Macros.h"

NATRON_NAMESPACE_ENTER

class Gui;

class FluxT079Harness
{
public:
    static void maybeStart(Gui* gui);
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_T079_HARNESS_H
