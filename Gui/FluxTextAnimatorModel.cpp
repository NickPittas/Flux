/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Animator Model Helpers
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxTextAnimatorModel.h"
#include "Gui/FluxTimelineSerialization.h"

#include "Engine/ChoiceOption.h"
#include "Engine/Curve.h"
#include "Engine/EffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <set>

NATRON_NAMESPACE_ENTER

namespace {

static const char* kPrefix = "fta_";

KnobIPtr knob(const NodePtr& node, const QString& name)
{
    return node ? node->getKnobByName(name.toStdString()) : KnobIPtr();
}

KnobHolder* holder(const NodePtr& node)
{
    return node && node->getEffectInstance() ? node->getEffectInstance().get() : 0;
}

void setupKnob(const KnobIPtr& k, bool animated)
{
    if (!k) {
        return;
    }
    k->setIsPersistent(true);
    k->setAnimationEnabled(animated);
    k->setSecret(false);
}

void setDoubleDefault(const KnobDoubleBasePtr& k, double v0, double v1 = 0.0)
{
    if (!k) {
        return;
    }
    k->setValue(v0, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
    if (k->getDimension() > 1) {
        k->setValue(v1, ViewSpec::all(), 1, eValueChangedReasonPluginEdited, 0);
    }
}

void setColorDefault(const KnobDoubleBasePtr& k, double r, double g, double b, double a)
{
    if (!k) {
        return;
    }
    const double vals[4] = {r, g, b, a};
    for (int i = 0; i < std::min(4, k->getDimension()); ++i) {
        k->setValue(vals[i], ViewSpec::all(), i, eValueChangedReasonPluginEdited, 0);
    }
}

void populateChoice(const KnobChoicePtr& k, const std::vector<ChoiceOption>& options, int value)
{
    if (!k) {
        return;
    }
    k->populateChoices(options);
    k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
}

void populateShapeChoice(const KnobChoicePtr& k, int value)
{
    populateChoice(k,
                   {ChoiceOption("square", "Square", ""),
                    ChoiceOption("linear", "Linear", ""),
                    ChoiceOption("rampUp", "Ramp Up", ""),
                    ChoiceOption("rampDown", "Ramp Down", "")},
                   std::max(0, std::min(3, value)));
}

QString orderString(const QList<int>& ids)
{
    QStringList out;
    for (int id : ids) {
        out << QString::number(id);
    }
    return out.join(QString::fromUtf8(","));
}

QJsonArray keysForKnob(const KnobIPtr& k, int dim)
{
    QJsonArray keys;
    if (!k) {
        return keys;
    }
    CurvePtr curve = k->getCurve(ViewSpec::current(), dim);
    if (!curve) {
        return keys;
    }
    KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();
    for (const KeyFrame& key : keyFrames) {
        QJsonObject obj;
        obj[QString::fromUtf8("t")] = key.getTime();
        obj[QString::fromUtf8("v")] = key.getValue();
        keys.append(obj);
    }
    return keys;
}

QJsonObject numericParam(const NodePtr& node, const QString& name, int dim, double fallback)
{
    QJsonObject obj;
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, name));
    KnobIntBasePtr ik = std::dynamic_pointer_cast<KnobIntBase>(knob(node, name));
    if (dk) {
        obj[QString::fromUtf8("v")] = dk->getValue(dim, ViewSpec::current());
        obj[QString::fromUtf8("keys")] = keysForKnob(dk, dim);
    } else if (ik) {
        obj[QString::fromUtf8("v")] = ik->getValue(dim, ViewSpec::current());
        obj[QString::fromUtf8("keys")] = keysForKnob(ik, dim);
    } else {
        obj[QString::fromUtf8("v")] = fallback;
        obj[QString::fromUtf8("keys")] = QJsonArray();
    }
    return obj;
}

QJsonArray vecParam(const NodePtr& node, const QString& name, int dims, double fallback0, double fallback1 = 0.0)
{
    QJsonArray arr;
    for (int d = 0; d < dims; ++d) {
        arr.append(numericParam(node, name, d, d == 0 ? fallback0 : fallback1));
    }
    return arr;
}

QString stringValue(const NodePtr& node, const QString& name, const QString& fallback)
{
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(node, name));
    return k ? QString::fromStdString(k->getValue(0, ViewSpec::current())) : fallback;
}

int intValue(const NodePtr& node, const QString& name, int fallback)
{
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(node, name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

bool boolValue(const NodePtr& node, const QString& name, bool fallback)
{
    KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(node, name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

void setString(const NodePtr& node, const QString& name, const QString& value)
{
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(node, name));
    if (k) {
        k->setValue(value.toStdString(), ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
    }
}

void setInt(const NodePtr& node, const QString& name, int value)
{
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(node, name));
    if (k) {
        k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
    }
}

std::set<double> animatorKeyTimes(const NodePtr& node)
{
    std::set<double> times;
    if (!node) {
        return times;
    }

    const std::vector<KnobIPtr>& knobs = node->getKnobs();
    for (const KnobIPtr& k : knobs) {
        if (!k || !k->canAnimate() || !FluxTextAnimatorModel::isAnimatorKnobName(k->getName())) {
            continue;
        }
        for (int dim = 0; dim < k->getDimension(); ++dim) {
            CurvePtr curve = k->getCurve(ViewSpec::current(), dim);
            if (!curve) {
                continue;
            }
            KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();
            for (const KeyFrame& key : keyFrames) {
                times.insert(key.getTime());
            }
        }
    }
    return times;
}

void syncAnimatorTimeDependency(const NodePtr& node)
{
    KnobDoubleBasePtr dependency = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, QString::fromUtf8("animatorTimeDependency")));
    if (!dependency) {
        return;
    }

    dependency->setAnimationEnabled(true);
    dependency->removeAnimation(ViewSpec::all(), 0);
    const std::set<double> times = animatorKeyTimes(node);
    if (times.empty()) {
        dependency->setValue(0.0, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
        return;
    }

    int index = 0;
    for (double time : times) {
        KeyFrame key;
        dependency->setValueAtTime(time, static_cast<double>(index++), ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, &key);
    }
}

void ensureBoolKnob(const NodePtr& node, int animatorId, const QString& suffix, const QString& label, bool fallback)
{
    if (!node || knob(node, FluxTextAnimatorModel::knobName(animatorId, suffix))) {
        return;
    }
    KnobHolder* h = holder(node);
    if (!h) {
        return;
    }
    KnobBoolPtr k = h->createBoolKnob(FluxTextAnimatorModel::knobName(animatorId, suffix).toStdString(), label.toStdString());
    setupKnob(k, false);
    k->setValue(fallback, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
    h->recreateUserKnobs(true);
}

} // namespace

namespace FluxTextAnimatorModel {

QString knobName(int animatorId, const QString& suffix)
{
    return QString::fromUtf8("%1%2_%3").arg(QString::fromUtf8(kPrefix)).arg(animatorId).arg(suffix);
}

bool isAnimatorKnobName(const std::string& name)
{
    return name.find(kPrefix) == 0;
}

QList<int> animatorIds(const NodePtr& node)
{
    QList<int> ids;
    QString order = stringValue(node, QString::fromUtf8("animatorOrder"), QString());
    order.remove(QLatin1Char('['));
    order.remove(QLatin1Char(']'));
    order.remove(QLatin1Char(' '));
    for (const QString& part : order.split(QString::fromUtf8(","), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int id = part.toInt(&ok);
        if (ok && id > 0 && !ids.contains(id)) {
            ids.append(id);
        }
    }
    if (!ids.isEmpty()) {
        return ids;
    }
    QRegularExpression re(QString::fromUtf8("^fta_(\\d+)_name$"));
    const std::vector<KnobIPtr>& ks = node ? node->getKnobs() : std::vector<KnobIPtr>();
    for (const KnobIPtr& k : ks) {
        QRegularExpressionMatch m = re.match(QString::fromStdString(k->getName()));
        if (m.hasMatch()) {
            ids.append(m.captured(1).toInt());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QList<FluxTextAnimatorSummary> animators(const NodePtr& node)
{
    QList<FluxTextAnimatorSummary> out;
    for (int id : animatorIds(node)) {
        FluxTextAnimatorSummary s;
        s.id = id;
        s.name = stringValue(node, knobName(id, QString::fromUtf8("name")), QString::fromUtf8("Animator %1").arg(id));
        s.enabled = boolValue(node, knobName(id, QString::fromUtf8("enabled")), true);
        s.basedOn = intValue(node, knobName(id, QString::fromUtf8("basedOn")), 0);
        s.shape = intValue(node, knobName(id, QString::fromUtf8("shape")), 1);
        out.append(s);
    }
    return out;
}

QString targetLabel(const QString& target)
{
    if (target == QString::fromUtf8("position")) return QString::fromUtf8("Position");
    if (target == QString::fromUtf8("scale")) return QString::fromUtf8("Scale");
    if (target == QString::fromUtf8("rotation")) return QString::fromUtf8("Rotation");
    if (target == QString::fromUtf8("opacity")) return QString::fromUtf8("Opacity");
    if (target == QString::fromUtf8("fill")) return QString::fromUtf8("Fill Color");
    if (target == QString::fromUtf8("tracking")) return QString::fromUtf8("Tracking");
    return QString::fromUtf8("Animator");
}

void ensureAnimatorKnobs(const NodePtr& node, int animatorId, const QString& label)
{
    KnobHolder* h = holder(node);
    if (!h || animatorId <= 0) {
        return;
    }
    const QString prefix = QString::fromUtf8("Animator %1 ").arg(animatorId);
    KnobBoolPtr enabled = h->createBoolKnob(knobName(animatorId, QString::fromUtf8("enabled")).toStdString(), (prefix + QString::fromUtf8("Enabled")).toStdString());
    setupKnob(enabled, false);
    enabled->setValue(true, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
    KnobStringPtr name = h->createStringKnob(knobName(animatorId, QString::fromUtf8("name")).toStdString(), (prefix + QString::fromUtf8("Name")).toStdString());
    setupKnob(name, false);
    name->setValue(label.toStdString(), ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);

    KnobChoicePtr basedOn = h->createChoiceKnob(knobName(animatorId, QString::fromUtf8("basedOn")).toStdString(), (prefix + QString::fromUtf8("Based On")).toStdString());
    setupKnob(basedOn, false);
    populateChoice(basedOn, {ChoiceOption("chars", "Characters", ""), ChoiceOption("charsNoSpaces", "Characters excluding spaces", ""), ChoiceOption("words", "Words", ""), ChoiceOption("lines", "Lines", "")}, 0);
    KnobChoicePtr shape = h->createChoiceKnob(knobName(animatorId, QString::fromUtf8("shape")).toStdString(), (prefix + QString::fromUtf8("Selector Shape")).toStdString());
    setupKnob(shape, false);
    populateShapeChoice(shape, 1);
    KnobChoicePtr anchor = h->createChoiceKnob(knobName(animatorId, QString::fromUtf8("anchor")).toStdString(), (prefix + QString::fromUtf8("Anchor")).toStdString());
    setupKnob(anchor, false);
    populateChoice(anchor, {ChoiceOption("bottomLeft", "Bottom Left", ""), ChoiceOption("center", "Center", ""), ChoiceOption("bottomRight", "Bottom Right", "")}, 1);

    struct D { const char* s; const char* l; double v; } doubles[] = {
        {"start", "Start", 0.0}, {"end", "End", 100.0}, {"offset", "Offset", 0.0}, {"amount", "Strength", 100.0},
        {"rotation", "Rotation", 0.0}, {"opacity", "Opacity", 100.0}, {"tracking", "Tracking", 0.0}
    };
    for (const D& d : doubles) {
        KnobDoublePtr k = h->createDoubleKnob(knobName(animatorId, QString::fromUtf8(d.s)).toStdString(), (prefix + QString::fromUtf8(d.l)).toStdString(), 1);
        setupKnob(k, true);
        setDoubleDefault(k, d.v);
    }
    KnobDoublePtr pos = h->createDoubleKnob(knobName(animatorId, QString::fromUtf8("position")).toStdString(), (prefix + QString::fromUtf8("Position")).toStdString(), 2);
    setupKnob(pos, true); setDoubleDefault(pos, 0.0, 0.0);
    KnobDoublePtr scale = h->createDoubleKnob(knobName(animatorId, QString::fromUtf8("scale")).toStdString(), (prefix + QString::fromUtf8("Scale %")).toStdString(), 2);
    setupKnob(scale, true); setDoubleDefault(scale, 100.0, 100.0);
    KnobBoolPtr scaleSeparated = h->createBoolKnob(knobName(animatorId, QString::fromUtf8("scaleSeparated")).toStdString(), (prefix + QString::fromUtf8("Separate Scale X/Y")).toStdString());
    setupKnob(scaleSeparated, false); scaleSeparated->setValue(false, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
    KnobColorPtr fill = h->createColorKnob(knobName(animatorId, QString::fromUtf8("fillColor")).toStdString(), (prefix + QString::fromUtf8("Fill Color")).toStdString(), 4);
    setupKnob(fill, true); setColorDefault(fill, 0.1, 0.65, 1.0, 1.0);
    h->recreateUserKnobs(true);
}

void ensureAnimatorCompatibility(const NodePtr& node)
{
    for (int id : animatorIds(node)) {
        const QString prefix = QString::fromUtf8("Animator %1 ").arg(id);
        ensureBoolKnob(node, id, QString::fromUtf8("scaleSeparated"), prefix + QString::fromUtf8("Separate Scale X/Y"), false);
        KnobChoicePtr basedOn = std::dynamic_pointer_cast<KnobChoice>(knob(node, knobName(id, QString::fromUtf8("basedOn"))));
        if (basedOn) {
            basedOn->setAnimationEnabled(false);
        }
        if (!knob(node, knobName(id, QString::fromUtf8("anchor")))) {
            KnobHolder* h = holder(node);
            if (h) {
                KnobChoicePtr anchor = h->createChoiceKnob(knobName(id, QString::fromUtf8("anchor")).toStdString(), (prefix + QString::fromUtf8("Anchor")).toStdString());
                setupKnob(anchor, false);
                populateChoice(anchor, {ChoiceOption("bottomLeft", "Bottom Left", ""), ChoiceOption("center", "Center", ""), ChoiceOption("bottomRight", "Bottom Right", "")}, 1);
                h->recreateUserKnobs(true);
            }
        }
        KnobChoicePtr shape = std::dynamic_pointer_cast<KnobChoice>(knob(node, knobName(id, QString::fromUtf8("shape"))));
        if (shape) {
            shape->setAnimationEnabled(false);
            populateShapeChoice(shape, shape->getValue(0, ViewSpec::current()));
        }
        KnobChoicePtr anchor = std::dynamic_pointer_cast<KnobChoice>(knob(node, knobName(id, QString::fromUtf8("anchor"))));
        if (anchor) {
            anchor->setAnimationEnabled(false);
        }
    }
}

int addAnimator(const NodePtr& node, const QString& initialTarget)
{
    if (!node) {
        return 0;
    }
    int nextId = std::max(1, intValue(node, QString::fromUtf8("animatorNextId"), 1));
    QList<int> ids = animatorIds(node);
    while (ids.contains(nextId)) {
        ++nextId;
    }
    const QString label = initialTarget.isEmpty()
        ? QString::fromUtf8("Animator %1").arg(nextId)
        : QString::fromUtf8("%1 Animator %2").arg(targetLabel(initialTarget)).arg(nextId);
    ensureAnimatorKnobs(node, nextId, label);
    if (initialTarget == QString::fromUtf8("position")) {
        setDoubleDefault(std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobName(nextId, QString::fromUtf8("position")))), 0.0, 80.0);
        setInt(node, knobName(nextId, QString::fromUtf8("shape")), 3);
    }
    if (initialTarget == QString::fromUtf8("scale")) setDoubleDefault(std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobName(nextId, QString::fromUtf8("scale")))), 0.0, 0.0);
    if (initialTarget == QString::fromUtf8("rotation")) setDoubleDefault(std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobName(nextId, QString::fromUtf8("rotation")))), 45.0);
    if (initialTarget == QString::fromUtf8("opacity")) setDoubleDefault(std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobName(nextId, QString::fromUtf8("opacity")))), 0.0);
    if (initialTarget == QString::fromUtf8("tracking")) setDoubleDefault(std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobName(nextId, QString::fromUtf8("tracking")))), 20.0);
    ids.append(nextId);
    setString(node, QString::fromUtf8("animatorOrder"), orderString(ids));
    setInt(node, QString::fromUtf8("animatorNextId"), nextId + 1);
    syncAnimatorStackToRenderer(node);
    return nextId;
}

bool removeAnimator(const NodePtr& node, int animatorId)
{
    QList<int> ids = animatorIds(node);
    if (!ids.removeOne(animatorId)) {
        return false;
    }
    setString(node, QString::fromUtf8("animatorOrder"), orderString(ids));
    KnobBoolPtr enabled = std::dynamic_pointer_cast<KnobBool>(knob(node, knobName(animatorId, QString::fromUtf8("enabled"))));
    if (enabled) {
        enabled->setValue(false, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
    }
    syncAnimatorStackToRenderer(node);
    return true;
}

bool moveAnimator(const NodePtr& node, int animatorId, int delta)
{
    QList<int> ids = animatorIds(node);
    int idx = ids.indexOf(animatorId);
    int next = idx + delta;
    if (idx < 0 || next < 0 || next >= ids.size()) {
        return false;
    }
    ids.swapItemsAt(idx, next);
    setString(node, QString::fromUtf8("animatorOrder"), orderString(ids));
    syncAnimatorStackToRenderer(node);
    return true;
}

void syncAnimatorStackToRenderer(const NodePtr& node)
{
    QJsonObject root;
    root[QString::fromUtf8("version")] = 2;
    QJsonArray anims;
    for (const FluxTextAnimatorSummary& a : animators(node)) {
        QJsonObject obj;
        obj[QString::fromUtf8("id")] = a.id;
        obj[QString::fromUtf8("name")] = a.name;
        obj[QString::fromUtf8("enabled")] = a.enabled;
        obj[QString::fromUtf8("basedOn")] = a.basedOn;
        obj[QString::fromUtf8("shape")] = a.shape;
        obj[QString::fromUtf8("anchor")] = intValue(node, knobName(a.id, QString::fromUtf8("anchor")), 1);
        QJsonObject sel;
        for (const QString& s : {QString::fromUtf8("start"), QString::fromUtf8("end"), QString::fromUtf8("offset"), QString::fromUtf8("amount")}) {
            sel[s] = numericParam(node, knobName(a.id, s), 0, s == QString::fromUtf8("end") || s == QString::fromUtf8("amount") ? 100.0 : 0.0);
        }
        obj[QString::fromUtf8("selector")] = sel;
        QJsonObject targets;
        targets[QString::fromUtf8("position")] = vecParam(node, knobName(a.id, QString::fromUtf8("position")), 2, 0.0, 0.0);
        targets[QString::fromUtf8("scale")] = vecParam(node, knobName(a.id, QString::fromUtf8("scale")), 2, 100.0, 100.0);
        targets[QString::fromUtf8("rotation")] = numericParam(node, knobName(a.id, QString::fromUtf8("rotation")), 0, 0.0);
        targets[QString::fromUtf8("opacity")] = numericParam(node, knobName(a.id, QString::fromUtf8("opacity")), 0, 100.0);
        targets[QString::fromUtf8("fillColor")] = vecParam(node, knobName(a.id, QString::fromUtf8("fillColor")), 4, 0.1, 0.65);
        targets[QString::fromUtf8("tracking")] = numericParam(node, knobName(a.id, QString::fromUtf8("tracking")), 0, 0.0);
        obj[QString::fromUtf8("targets")] = targets;
        anims.append(obj);
    }
    root[QString::fromUtf8("animators")] = anims;
    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    setString(node, QString::fromUtf8("animatorStackJson"), json);
    syncAnimatorTimeDependency(node);
}


// --- Capture / Restore for serialization ---

namespace {

FluxAnimatorPropertySerialization captureProperty(const NodePtr& node, const QString& knobFullName, int dim)
{
    FluxAnimatorPropertySerialization prop;
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobFullName));
    if (dk) {
        prop.value = dk->getValue(dim, ViewSpec::current());
        CurvePtr curve = dk->getCurve(ViewSpec::current(), dim);
        if (curve) {
            KeyFrameSet kfSet = curve->getKeyFrames_mt_safe();
            for (const KeyFrame& kf : kfSet) {
                FluxAnimatorKeyframeSerialization ks;
                ks.time = kf.getTime();
                ks.value = kf.getValue();
                ks.leftDerivative = kf.getLeftDerivative();
                ks.rightDerivative = kf.getRightDerivative();
                ks.interpolation = static_cast<int>(kf.getInterpolation());
                prop.keyframes.push_back(ks);
            }
        }
    }
    return prop;
}

std::vector<FluxAnimatorPropertySerialization> captureVecProperty(const NodePtr& node, const QString& knobFullName, int dims)
{
    std::vector<FluxAnimatorPropertySerialization> result;
    for (int d = 0; d < dims; ++d) {
        result.push_back(captureProperty(node, knobFullName, d));
    }
    return result;
}

void restoreProperty(const NodePtr& node, const QString& knobFullName,
                     int dim, const FluxAnimatorPropertySerialization& prop)
{
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobFullName));
    if (!dk) return;

    dk->removeAnimation(ViewSpec::all(), dim);
    dk->setValue(prop.value, ViewSpec::all(), dim, eValueChangedReasonPluginEdited, 0);

    for (const FluxAnimatorKeyframeSerialization& ks : prop.keyframes) {
        KeyFrame kf(ks.time,
                    ks.value,
                    ks.leftDerivative,
                    ks.rightDerivative,
                    static_cast<KeyframeTypeEnum>(ks.interpolation));
        dk->setKeyFrame(kf, ViewSpec::all(), dim, eValueChangedReasonNatronInternalEdited);
    }
}

void restoreVecProperty(const NodePtr& node, const QString& knobFullName,
                        const std::vector<FluxAnimatorPropertySerialization>& props)
{
    for (size_t d = 0; d < props.size(); ++d) {
        restoreProperty(node, knobFullName, static_cast<int>(d), props[d]);
    }
}

} // namespace

std::vector<FluxAnimatorSerialization> captureAnimators(const NodePtr& node)
{
    std::vector<FluxAnimatorSerialization> result;
    if (!node) return result;

    QList<int> ids = animatorIds(node);
    for (int id : ids) {
        FluxAnimatorSerialization as;
        as.animatorId = id;
        as.name = stringValue(node, knobName(id, QString::fromUtf8("name")), QString::fromUtf8("Animator %1").arg(id)).toStdString();
        as.enabled = boolValue(node, knobName(id, QString::fromUtf8("enabled")), true);
        as.basedOn = intValue(node, knobName(id, QString::fromUtf8("basedOn")), 0);
        as.shape = intValue(node, knobName(id, QString::fromUtf8("shape")), 1);
        as.anchor = intValue(node, knobName(id, QString::fromUtf8("anchor")), 1);

        as.start = captureProperty(node, knobName(id, QString::fromUtf8("start")), 0);
        as.end = captureProperty(node, knobName(id, QString::fromUtf8("end")), 0);
        as.offset = captureProperty(node, knobName(id, QString::fromUtf8("offset")), 0);
        as.amount = captureProperty(node, knobName(id, QString::fromUtf8("amount")), 0);

        as.rotation = captureProperty(node, knobName(id, QString::fromUtf8("rotation")), 0);
        as.opacity = captureProperty(node, knobName(id, QString::fromUtf8("opacity")), 0);
        as.tracking = captureProperty(node, knobName(id, QString::fromUtf8("tracking")), 0);
        as.position = captureVecProperty(node, knobName(id, QString::fromUtf8("position")), 2);
        as.scale = captureVecProperty(node, knobName(id, QString::fromUtf8("scale")), 2);
        as.fillColor = captureVecProperty(node, knobName(id, QString::fromUtf8("fillColor")), 4);
        as.scaleSeparated = boolValue(node, knobName(id, QString::fromUtf8("scaleSeparated")), false);

        result.push_back(as);
    }
    return result;
}

void restoreAnimators(const NodePtr& node, const std::vector<FluxAnimatorSerialization>& serialized)
{
    if (!node) return;

    for (const FluxAnimatorSerialization& as : serialized) {
        if (!knob(node, knobName(as.animatorId, QString::fromUtf8("enabled")))) {
            ensureAnimatorKnobs(node, as.animatorId, QString::fromStdString(as.name));
        }

        auto setBool = [&](const QString& suffix, bool val) {
            KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(node, knobName(as.animatorId, suffix)));
            if (k) k->setValue(val, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
        };
        auto setInt = [&](const QString& suffix, int val) {
            KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(node, knobName(as.animatorId, suffix)));
            if (k) k->setValue(val, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
        };

        setBool(QString::fromUtf8("enabled"), as.enabled);
        setInt(QString::fromUtf8("basedOn"), as.basedOn);
        setInt(QString::fromUtf8("shape"), as.shape);
        setInt(QString::fromUtf8("anchor"), as.anchor);
        setBool(QString::fromUtf8("scaleSeparated"), as.scaleSeparated);

        KnobStringBasePtr nameK = std::dynamic_pointer_cast<KnobStringBase>(
            knob(node, knobName(as.animatorId, QString::fromUtf8("name"))));
        if (nameK) nameK->setValue(as.name, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);

        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("start")), 0, as.start);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("end")), 0, as.end);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("offset")), 0, as.offset);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("amount")), 0, as.amount);

        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("rotation")), 0, as.rotation);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("opacity")), 0, as.opacity);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("tracking")), 0, as.tracking);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("position")), as.position);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("scale")), as.scale);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("fillColor")), as.fillColor);
    }

    syncAnimatorStackToRenderer(node);
}

} // namespace FluxTextAnimatorModel

NATRON_NAMESPACE_EXIT
