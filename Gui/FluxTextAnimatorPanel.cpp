/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Animator Panel
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxTextAnimatorPanel.h"

#include "Gui/FluxTextAnimatorModel.h"
#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Engine/AppInstance.h"
#include "Engine/Curve.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/TimeLine.h"

#include <cmath>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSignalBlocker>
#include <QVBoxLayout>

NATRON_NAMESPACE_ENTER

namespace {

KnobIPtr writeTarget(const KnobIPtr& raw)
{
    if (!raw) return KnobIPtr();
    KnobIPtr master = raw->getAliasMaster();
    return master ? master : raw;
}

class FluxAnimatorKeyButton : public QPushButton
{
public:
    explicit FluxAnimatorKeyButton(QWidget* parent = 0) : QPushButton(parent), _active(false)
    {
        setFixedSize(20, 20);
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
    }
    void setActive(bool active) { if (_active != active) { _active = active; update(); } }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF d;
        const qreal cx = width() / 2.0, cy = height() / 2.0, r = width() * 0.34;
        d << QPointF(cx, cy - r) << QPointF(cx + r, cy) << QPointF(cx, cy + r) << QPointF(cx - r, cy);
        p.setPen(QPen(Qt::white, 1.3));
        p.setBrush(_active ? QBrush(Qt::white) : Qt::NoBrush);
        p.drawPolygon(d);
    }
private:
    bool _active;
};

} // namespace

FluxTextAnimatorPanel::FluxTextAnimatorPanel(Gui* gui, QWidget* parent)
    : QWidget(parent)
    , PanelWidget(this, gui)
    , _gui(gui)
    , _node()
    , _updating(false)
    , _emptyLabel(0)
    , _body(0)
    , _animatorLayout(0)
    , _addButton(0)
    , _selectedAnimatorId(0)
{
    setObjectName(QString::fromUtf8("FluxTextAnimatorPanel"));
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    _addButton = new QPushButton(tr("Add Animator"), this);
    main->addWidget(_addButton);
    _emptyLabel = new QLabel(tr("Select a FluxMotionText layer to add text animators."), this);
    _emptyLabel->setWordWrap(true);
    main->addWidget(_emptyLabel);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    main->addWidget(scroll, 1);
    _body = new QWidget(scroll);
    _animatorLayout = new QVBoxLayout(_body);
    _animatorLayout->setContentsMargins(0, 0, 0, 0);
    _animatorLayout->setSpacing(8);
    scroll->setWidget(_body);
    connect(_addButton, &QPushButton::clicked, this, &FluxTextAnimatorPanel::onAddAnimator);

    GuiAppInstancePtr app = _gui ? _gui->getApp() : GuiAppInstancePtr();
    if (app && app->getTimeLine()) {
        QObject::connect(app->getTimeLine().get(), SIGNAL(frameChanged(SequenceTime,int)),
                         this, SLOT(onTimelineFrameChanged(SequenceTime,int)));
    }
    rebuild();
}

FluxTextAnimatorPanel::~FluxTextAnimatorPanel() {}

void FluxTextAnimatorPanel::setActiveNode(const NodePtr& node)
{
    _node = isFluxMotionTextNode(node) ? node : NodePtr();
    if (!_node) {
        _selectedAnimatorId = 0;
    }
    if (_node) {
        FluxTextAnimatorModel::ensureAnimatorCompatibility(_node);
        FluxTextAnimatorModel::syncAnimatorStackToRenderer(_node);
    }
    rebuild();
}

void FluxTextAnimatorPanel::setSelectedAnimatorId(int animatorId)
{
    _selectedAnimatorId = animatorId;
    rebuild();
}

bool FluxTextAnimatorPanel::isFluxMotionTextNode(const NodePtr& node) const
{
    return node && node->isActivated() && node->getPluginID() == std::string("net.sf.openfx.FluxMotionText");
}

void FluxTextAnimatorPanel::onAddAnimator()
{
    if (!_node) return;
    FluxTextAnimatorModel::addAnimator(_node, QString());
    rebuild();
    if (_gui) _gui->renderAllViewers(true);
}

void FluxTextAnimatorPanel::onTimelineFrameChanged(SequenceTime, int)
{
    if (_node) rebuild();
}

void FluxTextAnimatorPanel::clearAnimatorWidgets()
{
    while (QLayoutItem* item = _animatorLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void FluxTextAnimatorPanel::rebuild()
{
    _updating = true;
    _addButton->setEnabled(bool(_node));
    _emptyLabel->setVisible(!_node);
    clearAnimatorWidgets();
    if (_node) {
        QList<FluxTextAnimatorSummary> list = FluxTextAnimatorModel::animators(_node);
        for (int i = 0; i < list.size(); ++i) {
            _animatorLayout->addWidget(makeAnimatorGroup(list[i].id, i, list.size(), _body));
        }
    }
    _animatorLayout->addStretch(1);
    _updating = false;
}

KnobIPtr FluxTextAnimatorPanel::knob(const QString& name) const { return _node ? _node->getKnobByName(name.toStdString()) : KnobIPtr(); }

double FluxTextAnimatorPanel::currentFrameTime() const
{
    if (!_node || !_node->getApp() || !_node->getApp()->getTimeLine()) return -1.0;
    return _node->getApp()->getTimeLine()->currentFrame();
}

double FluxTextAnimatorPanel::doubleValue(const QString& name, int dim, double fallback) const
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(knob(name));
    return k ? k->getValue(dim, ViewSpec::current()) : fallback;
}

int FluxTextAnimatorPanel::intValue(const QString& name, int fallback) const
{
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

bool FluxTextAnimatorPanel::boolValue(const QString& name, bool fallback) const
{
    KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

QString FluxTextAnimatorPanel::stringValue(const QString& name, const QString& fallback) const
{
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(name));
    return k ? QString::fromStdString(k->getValue(0, ViewSpec::current())) : fallback;
}

QColor FluxTextAnimatorPanel::colorValue(const QString& name, const QColor& fallback) const
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(knob(name));
    if (!k || k->getDimension() < 4) return fallback;
    return QColor::fromRgbF(k->getValue(0, ViewSpec::current()), k->getValue(1, ViewSpec::current()), k->getValue(2, ViewSpec::current()), k->getValue(3, ViewSpec::current()));
}

void FluxTextAnimatorPanel::setDoubleValue(const QString& name, int dim, double value)
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(writeTarget(knob(name)));
    if (!k) return;
    double t = currentFrameTime();
    if (t >= 0.0 && k->isAnimated(dim)) { KeyFrame key; k->setValueAtTime(t, value, ViewSpec::current(), dim, eValueChangedReasonNatronGuiEdited, &key); }
    else { k->setValue(value, ViewSpec::all(), dim, eValueChangedReasonNatronGuiEdited, 0); }
    syncJsonAndRefresh();
}

void FluxTextAnimatorPanel::setIntValue(const QString& name, int value)
{
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(writeTarget(knob(name)));
    if (!k) return;
    double t = currentFrameTime();
    if (t >= 0.0 && k->isAnimated(0)) { KeyFrame key; k->setValueAtTime(t, value, ViewSpec::current(), 0, eValueChangedReasonNatronGuiEdited, &key); }
    else { k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0); }
    syncJsonAndRefresh();
}

void FluxTextAnimatorPanel::setBoolValue(const QString& name, bool value)
{
    KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(name));
    if (k) { k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0); syncJsonAndRefresh(); }
}

void FluxTextAnimatorPanel::setStringValue(const QString& name, const QString& value)
{
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(name));
    if (k) { k->setValue(value.toStdString(), ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0); syncJsonAndRefresh(); }
}

void FluxTextAnimatorPanel::setColorValue(const QString& name, const QColor& value)
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(writeTarget(knob(name)));
    if (!k) return;
    const double rgba[4] = {value.redF(), value.greenF(), value.blueF(), value.alphaF()};
    const double t = currentFrameTime();
    for (int d = 0; d < 4; ++d) {
        if (t >= 0.0 && k->isAnimated(d)) { KeyFrame key; k->setValueAtTime(t, rgba[d], ViewSpec::current(), d, eValueChangedReasonNatronGuiEdited, &key); }
        else { k->setValue(rgba[d], ViewSpec::all(), d, eValueChangedReasonNatronGuiEdited, 0); }
    }
    syncJsonAndRefresh();
}

bool FluxTextAnimatorPanel::hasKey(const QString& name, int dim, double time) const
{
    KnobIPtr k = writeTarget(knob(name));
    if (!k) return false;
    CurvePtr c = k->getCurve(ViewSpec::current(), dim);
    if (!c) return false;
    KeyFrame key;
    return c->getKeyFrameWithTime(time, &key);
}

void FluxTextAnimatorPanel::toggleKey(const QString& name, int dims)
{
    KnobIPtr k = writeTarget(knob(name));
    double t = currentFrameTime();
    if (!k || t < 0.0) return;
    bool any = false;
    for (int d = 0; d < std::min(dims, k->getDimension()); ++d) any = any || hasKey(name, d, t);
    for (int d = 0; d < std::min(dims, k->getDimension()); ++d) {
        if (any) k->onKeyFrameRemoved(t, ViewSpec::current(), d, false);
        else k->onKeyFrameSet(t, ViewSpec::current(), d);
    }
    syncJsonAndRefresh();
    rebuild();
}

QPushButton* FluxTextAnimatorPanel::keyButton(const QString& name, int dims, const QString& tip)
{
    FluxAnimatorKeyButton* b = new FluxAnimatorKeyButton(_body);
    b->setToolTip(tip);
    const double t = currentFrameTime();
    bool active = false;
    if (t >= 0.0) for (int d = 0; d < dims; ++d) active = active || hasKey(name, d, t);
    b->setActive(active);
    connect(b, &QPushButton::clicked, this, [this, name, dims]() { toggleKey(name, dims); });
    return b;
}

QWidget* FluxTextAnimatorPanel::makeDoubleRow(const QString&, const QString& name, int dim, double min, double max, double step, int keyDims, QWidget* parent, bool withSlider)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row); l->setContentsMargins(0,0,0,0); l->setSpacing(4);
    QDoubleSpinBox* s = new QDoubleSpinBox(row); s->setRange(min, max); s->setDecimals(2); s->setSingleStep(step); s->setValue(doubleValue(name, dim, 0.0));
    l->addWidget(s, withSlider ? 0 : 1);
    QSlider* slider = 0;
    const double sliderScale = 100.0;
    if (withSlider) {
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(static_cast<int>(std::floor(min * sliderScale)), static_cast<int>(std::ceil(max * sliderScale)));
        slider->setSingleStep(std::max(1, static_cast<int>(std::round(step * sliderScale))));
        slider->setValue(static_cast<int>(std::round(s->value() * sliderScale)));
        l->addWidget(slider, 1);
    }
    l->addWidget(keyButton(name, keyDims, tr("Toggle keyframe")));
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, name, dim, slider, sliderScale](double v) {
        if (slider) {
            QSignalBlocker block(slider);
            slider->setValue(static_cast<int>(std::round(v * sliderScale)));
        }
        if (!_updating) setDoubleValue(name, dim, v);
    });
    if (slider) {
        connect(slider, &QSlider::valueChanged, this, [s, sliderScale](int v) {
            s->setValue(static_cast<double>(v) / sliderScale);
        });
    }
    return row;
}

QWidget* FluxTextAnimatorPanel::makeVec2Row(const QString&, const QString& name, double min, double max, double step, QWidget* parent)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row); l->setContentsMargins(0,0,0,0); l->setSpacing(4);
    for (int d = 0; d < 2; ++d) {
        QDoubleSpinBox* s = new QDoubleSpinBox(row); s->setRange(min, max); s->setDecimals(2); s->setSingleStep(step); s->setValue(doubleValue(name, d, 0.0));
        l->addWidget(s, 1);
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, name, d](double v) { if (!_updating) setDoubleValue(name, d, v); });
    }
    l->addWidget(keyButton(name, 2, tr("Toggle keyframe")));
    return row;
}

QWidget* FluxTextAnimatorPanel::makeScaleRow(int id, QWidget* parent)
{
    const QString scaleName = FluxTextAnimatorModel::knobName(id, QString::fromUtf8("scale"));
    const QString splitName = FluxTextAnimatorModel::knobName(id, QString::fromUtf8("scaleSeparated"));
    const bool separated = boolValue(splitName, false);

    QWidget* row = new QWidget(parent);
    QVBoxLayout* outer = new QVBoxLayout(row);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    QCheckBox* split = new QCheckBox(tr("Separate X/Y"), row);
    split->setChecked(separated);
    outer->addWidget(split);
    connect(split, &QCheckBox::toggled, this, [this, splitName, scaleName](bool checked) {
        if (_updating) return;
        setBoolValue(splitName, checked);
        if (!checked) {
            setDoubleValue(scaleName, 1, doubleValue(scaleName, 0, 100.0));
        }
        rebuild();
    });

    QWidget* controls = new QWidget(row);
    QHBoxLayout* l = new QHBoxLayout(controls);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    if (separated) {
        for (int d = 0; d < 2; ++d) {
            QLabel* label = new QLabel(d == 0 ? tr("X") : tr("Y"), controls);
            QDoubleSpinBox* s = new QDoubleSpinBox(controls);
            QSlider* slider = new QSlider(Qt::Horizontal, controls);
            s->setRange(0.0, 1000.0);
            s->setDecimals(2);
            s->setSingleStep(1.0);
            s->setValue(doubleValue(scaleName, d, 100.0));
            slider->setRange(0, 100000);
            slider->setValue(static_cast<int>(std::round(s->value() * 100.0)));
            l->addWidget(label);
            l->addWidget(s);
            l->addWidget(slider, 1);
            connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, scaleName, d, slider](double v) {
                QSignalBlocker block(slider);
                slider->setValue(static_cast<int>(std::round(v * 100.0)));
                if (!_updating) setDoubleValue(scaleName, d, v);
            });
            connect(slider, &QSlider::valueChanged, this, [s](int v) {
                s->setValue(static_cast<double>(v) / 100.0);
            });
        }
    } else {
        QDoubleSpinBox* s = new QDoubleSpinBox(controls);
        QSlider* slider = new QSlider(Qt::Horizontal, controls);
        s->setRange(0.0, 1000.0);
        s->setDecimals(2);
        s->setSingleStep(1.0);
        s->setValue(doubleValue(scaleName, 0, 100.0));
        slider->setRange(0, 100000);
        slider->setValue(static_cast<int>(std::round(s->value() * 100.0)));
        l->addWidget(s);
        l->addWidget(slider, 1);
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, scaleName, slider](double v) {
            QSignalBlocker block(slider);
            slider->setValue(static_cast<int>(std::round(v * 100.0)));
            if (_updating) return;
            setDoubleValue(scaleName, 0, v);
            setDoubleValue(scaleName, 1, v);
        });
        connect(slider, &QSlider::valueChanged, this, [s](int v) {
            s->setValue(static_cast<double>(v) / 100.0);
        });
    }
    l->addWidget(keyButton(scaleName, 2, tr("Toggle keyframe")));
    outer->addWidget(controls);
    return row;
}

QWidget* FluxTextAnimatorPanel::makeColorRow(const QString&, const QString& name, QWidget* parent)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row); l->setContentsMargins(0,0,0,0); l->setSpacing(4);
    QPushButton* choose = new QPushButton(tr("Choose..."), row);
    l->addWidget(choose, 1); l->addWidget(keyButton(name, 4, tr("Toggle keyframe")));
    connect(choose, &QPushButton::clicked, this, [this, name]() {
        QColor c = QColorDialog::getColor(colorValue(name, QColor(26,166,255)), this, tr("Animator Fill Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) setColorValue(name, c);
    });
    return row;
}

QGroupBox* FluxTextAnimatorPanel::makeAnimatorGroup(int id, int index, int count, QWidget* parent)
{
    QString p = QString::fromUtf8("Animator %1").arg(id);
    QGroupBox* g = new QGroupBox(stringValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("name")), p), parent);
    if (id == _selectedAnimatorId) {
        g->setStyleSheet(QString::fromUtf8("QGroupBox { border: 2px solid #62a8ff; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"));
    }
    QVBoxLayout* outer = new QVBoxLayout(g);
    QWidget* head = new QWidget(g); QHBoxLayout* hl = new QHBoxLayout(head); hl->setContentsMargins(0,0,0,0);
    QCheckBox* enabled = new QCheckBox(tr("Enabled"), head); enabled->setChecked(boolValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("enabled")), true));
    QLineEdit* name = new QLineEdit(g->title(), head);
    QPushButton* up = new QPushButton(tr("↑"), head); QPushButton* down = new QPushButton(tr("↓"), head); QPushButton* remove = new QPushButton(tr("Remove"), head);
    up->setEnabled(index > 0); down->setEnabled(index + 1 < count);
    hl->addWidget(enabled); hl->addWidget(name, 1); hl->addWidget(up); hl->addWidget(down); hl->addWidget(remove); outer->addWidget(head);
    connect(enabled, &QCheckBox::toggled, this, [this, id](bool v) { setBoolValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("enabled")), v); });
    connect(name, &QLineEdit::editingFinished, this, [this, id, name]() { setStringValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("name")), name->text()); rebuild(); });
    connect(up, &QPushButton::clicked, this, [this, id]() { FluxTextAnimatorModel::moveAnimator(_node, id, -1); rebuild(); });
    connect(down, &QPushButton::clicked, this, [this, id]() { FluxTextAnimatorModel::moveAnimator(_node, id, 1); rebuild(); });
    connect(remove, &QPushButton::clicked, this, [this, id]() { FluxTextAnimatorModel::removeAnimator(_node, id); rebuild(); });

    QFormLayout* f = new QFormLayout(); f->setContentsMargins(0,0,0,0); f->setSpacing(5); outer->addLayout(f);
    QComboBox* based = new QComboBox(g); based->addItems(QStringList() << tr("Characters") << tr("Characters excluding spaces") << tr("Words") << tr("Lines")); based->setCurrentIndex(intValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("basedOn")), 0));
    f->addRow(tr("Based On"), based); connect(based, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, id](int v) { if (!_updating) setIntValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("basedOn")), v); });
    QComboBox* shape = new QComboBox(g); shape->addItems(QStringList() << tr("Square") << tr("Linear") << tr("Ramp Up") << tr("Ramp Down")); shape->setCurrentIndex(intValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("shape")), 1));
    f->addRow(tr("Shape"), shape); connect(shape, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, id](int v) { if (!_updating) setIntValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("shape")), v); });
    QComboBox* anchor = new QComboBox(g); anchor->addItems(QStringList() << tr("Bottom Left") << tr("Center") << tr("Bottom Right")); anchor->setCurrentIndex(intValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("anchor")), 1));
    f->addRow(tr("Anchor"), anchor); connect(anchor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, id](int v) { if (!_updating) setIntValue(FluxTextAnimatorModel::knobName(id, QString::fromUtf8("anchor")), v); });

    f->addRow(tr("Start %"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("start")), 0, 0.0, 100.0, 1.0, 1, g, true));
    f->addRow(tr("End %"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("end")), 0, 0.0, 100.0, 1.0, 1, g, true));
    f->addRow(tr("Offset %"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("offset")), 0, -100.0, 100.0, 1.0, 1, g, true));
    f->addRow(tr("Strength %"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("amount")), 0, 0.0, 100.0, 1.0, 1, g, true));
    f->addRow(tr("Position"), makeVec2Row(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("position")), -5000, 5000, 1, g));
    f->addRow(tr("Scale %"), makeScaleRow(id, g));
    f->addRow(tr("Rotation"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("rotation")), 0, -3600, 3600, 1, 1, g));
    f->addRow(tr("Opacity %"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("opacity")), 0, 0, 100, 1, 1, g));
    f->addRow(tr("Fill Color"), makeColorRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("fillColor")), g));
    f->addRow(tr("Tracking"), makeDoubleRow(QString(), FluxTextAnimatorModel::knobName(id, QString::fromUtf8("tracking")), 0, -500, 1000, 1, 1, g));
    return g;
}

void FluxTextAnimatorPanel::syncJsonAndRefresh()
{
    if (_node) FluxTextAnimatorModel::syncAnimatorStackToRenderer(_node);
    if (_gui) _gui->renderAllViewers(true);
}

NATRON_NAMESPACE_EXIT
