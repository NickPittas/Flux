/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Animator Model Helpers
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXTEXTANIMATORMODEL_H
#define FLUXTEXTANIMATORMODEL_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Engine/EngineFwd.h"

#include <QList>
#include <QString>
#include <vector>

NATRON_NAMESPACE_ENTER

struct FluxTextAnimatorSummary {
    int id;
    QString name;
    bool enabled;
    int basedOn;
    int shape;

    FluxTextAnimatorSummary()
        : id(0), enabled(true), basedOn(0), shape(1) {}
};

// Forward declarations from FluxTimelineSerialization.h
struct FluxAnimatorSerialization;

namespace FluxTextAnimatorModel {

QString knobName(int animatorId, const QString& suffix);
QList<int> animatorIds(const NodePtr& node);
QList<FluxTextAnimatorSummary> animators(const NodePtr& node);

int addAnimator(const NodePtr& node, const QString& initialTarget);
bool removeAnimator(const NodePtr& node, int animatorId);
bool moveAnimator(const NodePtr& node, int animatorId, int delta);

void ensureAnimatorKnobs(const NodePtr& node, int animatorId, const QString& label);
void ensureAnimatorCompatibility(const NodePtr& node);
void syncAnimatorStackToRenderer(const NodePtr& node);

bool isAnimatorKnobName(const std::string& name);
QString targetLabel(const QString& target);

// Capture all animator data from a text layer's gizmoNode into serializable form
std::vector<FluxAnimatorSerialization> captureAnimators(const NodePtr& gizmoNode);

// Restore animator data from serialization onto a text layer's gizmoNode
void restoreAnimators(const NodePtr& gizmoNode, const std::vector<FluxAnimatorSerialization>& serialized);

} // namespace FluxTextAnimatorModel

NATRON_NAMESPACE_EXIT

#endif // FLUXTEXTANIMATORMODEL_H
