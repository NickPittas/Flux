/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Layer Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxTextPanel.h"

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/FluxStyleUtils.h"
#include "Engine/AppInstance.h"
#include "Engine/Curve.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/TimeLine.h"

#include <algorithm>
#include <cmath>

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

NATRON_NAMESPACE_ENTER

namespace {

KnobIPtr aliasMaster(const KnobIPtr& k)
{
    return k ? k->getAliasMaster() : KnobIPtr();
}

/// Resolve the actual write target for a knob: follow alias to master.
/// Returns the master if present, otherwise the raw knob itself.
KnobIPtr resolveWriteTarget(const KnobIPtr& raw)
{
    if (!raw) { return KnobIPtr(); }
    KnobIPtr master = aliasMaster(raw);
    return master ? master : raw;
}

static const int kKeyBtnSize = 22;

/// Custom QPushButton subclass that paints a diamond shape instead of relying
/// on font glyph coverage. Filled diamond = key exists, outline = no key.
class FluxKeyDiamondButton : public QPushButton
{
public:
    explicit FluxKeyDiamondButton(QWidget* parent = nullptr)
        : QPushButton(parent)
        , _active(false)
    {
        setFixedSize(kKeyBtnSize, kKeyBtnSize);
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setKeyActive(bool active)
    {
        if (_active != active) {
            _active = active;
            update();
        }
    }

    bool isKeyActive() const { return _active; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int w = width();
        const int h = height();
        const qreal cx = w / 2.0;
        const qreal cy = h / 2.0;
        const qreal rx = w * 0.32;
        const qreal ry = h * 0.32;

        QPolygonF diamond;
        diamond << QPointF(cx, cy - ry)
                << QPointF(cx + rx, cy)
                << QPointF(cx, cy + ry)
                << QPointF(cx - rx, cy);

        const QColor border = FluxStyle::mix(FluxStyle::disabledText(this), FluxStyle::window(this), 0.4);
        const QColor fill = _active ? FluxStyle::keyframeColor() : Qt::transparent;
        p.setBrush(fill);
        QPen pen(_active ? FluxStyle::keyframeColor() : border);
        if (underMouse()) {
            pen.setColor(FluxStyle::hoverBorder(this));
        }
        pen.setWidthF(_active ? 1.6 : 1.15);
        p.setPen(pen);
        p.drawPolygon(diamond);
    }

private:
    bool _active;
};

} // namespace

FluxTextPanel::FluxTextPanel(Gui* gui, QWidget* parent)
    : QWidget(parent)
    , PanelWidget(this, gui)
    , _gui(gui)
    , _node()
    , _updating(false)
    , _fillColor(26, 166, 255)
    , _emptyLabel(nullptr)
    , _controls(nullptr)
    , _textEdit(nullptr)
    , _fontButton(nullptr)
    , _fontList(nullptr)
    , _styleCombo(nullptr)
    , _sizeSpin(nullptr)
    , _fillButton(nullptr)
    , _trackingSpin(nullptr)
    , _leadingSpin(nullptr)
    , _alignmentCombo(nullptr)
    , _textKeyBtn(nullptr)
    , _sizeKeyBtn(nullptr)
    , _fillKeyBtn(nullptr)
    , _trackingKeyBtn(nullptr)
    , _leadingKeyBtn(nullptr)
    , _alignmentKeyBtn(nullptr)
{
    setObjectName(QString::fromUtf8("FluxTextPanel"));
    setMinimumWidth(240);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName(QString::fromUtf8("FluxTextPanelScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(scroll, 1);

    QWidget* body = new QWidget(scroll);
    body->setObjectName(QString::fromUtf8("FluxTextPanelContent"));
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 6, 6, 6);
    bodyLayout->setSpacing(8);
    scroll->setWidget(body);

    _emptyLabel = new QLabel(tr("Select a FluxMotionText layer to edit text controls."), body);
    _emptyLabel->setWordWrap(true);
    bodyLayout->addWidget(_emptyLabel);

    _controls = new QWidget(body);
    _controls->setObjectName(QString::fromUtf8("FluxTextPanelControls"));
    QFormLayout* form = new QFormLayout(_controls);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    // --- Text [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QVBoxLayout* vl = new QVBoxLayout(row);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(2);
        _textEdit = new QPlainTextEdit(row);
        _textEdit->setObjectName(QString::fromUtf8("FluxTextTextEdit"));
        _textEdit->setMinimumHeight(80);
        _textKeyBtn = createKeyButton(tr("Toggle keyframe: Text"));
        // Place key button bottom-right under the text editor
        QHBoxLayout* keyRow = new QHBoxLayout();
        keyRow->setContentsMargins(0, 0, 0, 0);
        keyRow->addStretch(1);
        keyRow->addWidget(_textKeyBtn);
        vl->addWidget(_textEdit, 1);
        vl->addLayout(keyRow);
        form->addRow(tr("Text"), row);
    }

    // --- Font (not keyable) ---
    _fontButton = new QPushButton(_controls);
    _fontButton->setObjectName(QString::fromUtf8("FluxTextFontDropdown"));
    _fontButton->setText(tr("Choose font ▾"));
    form->addRow(tr("Font"), _fontButton);

    _fontList = new QListWidget(_controls);
    _fontList->setObjectName(QString::fromUtf8("FluxTextFontDropdownList"));
    _fontList->setMaximumHeight(260);
    _fontList->setVisible(false);
    populateFonts();
    form->addRow(QString(), _fontList);

    // --- Style (not keyable) ---
    _styleCombo = new QComboBox(_controls);
    _styleCombo->setObjectName(QString::fromUtf8("FluxTextStyleCombo"));
    form->addRow(tr("Style"), _styleCombo);

    // --- Size [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        _sizeSpin = new QDoubleSpinBox(row);
        _sizeSpin->setObjectName(QString::fromUtf8("FluxTextSizeSpin"));
        _sizeSpin->setRange(1.0, 2000.0);
        _sizeSpin->setDecimals(1);
        _sizeSpin->setSingleStep(1.0);
        _sizeKeyBtn = createKeyButton(tr("Toggle keyframe: Size"));
        hl->addWidget(_sizeSpin, 1);
        hl->addWidget(_sizeKeyBtn);
        form->addRow(tr("Size"), row);
    }

    // --- Fill [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        _fillButton = new QPushButton(tr("Choose..."), row);
        _fillButton->setObjectName(QString::fromUtf8("FluxTextFillButton"));
        _fillKeyBtn = createKeyButton(tr("Toggle keyframe: Fill Color"));
        hl->addWidget(_fillButton, 1);
        hl->addWidget(_fillKeyBtn);
        form->addRow(tr("Fill"), row);
    }

    // --- Tracking [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        _trackingSpin = new QDoubleSpinBox(row);
        _trackingSpin->setObjectName(QString::fromUtf8("FluxTextTrackingSpin"));
        _trackingSpin->setRange(-500.0, 1000.0);
        _trackingSpin->setDecimals(2);
        _trackingSpin->setSingleStep(1.0);
        _trackingKeyBtn = createKeyButton(tr("Toggle keyframe: Tracking"));
        hl->addWidget(_trackingSpin, 1);
        hl->addWidget(_trackingKeyBtn);
        form->addRow(tr("Tracking"), row);
    }

    // --- Leading [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        _leadingSpin = new QDoubleSpinBox(row);
        _leadingSpin->setObjectName(QString::fromUtf8("FluxTextLeadingSpin"));
        _leadingSpin->setRange(0.0, 5000.0);
        _leadingSpin->setDecimals(2);
        _leadingSpin->setSingleStep(1.0);
        _leadingSpin->setSpecialValueText(tr("Auto"));
        _leadingKeyBtn = createKeyButton(tr("Toggle keyframe: Leading"));
        hl->addWidget(_leadingSpin, 1);
        hl->addWidget(_leadingKeyBtn);
        form->addRow(tr("Leading"), row);
    }

    // --- Align [◆] ---
    {
        QWidget* row = new QWidget(_controls);
        QHBoxLayout* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        _alignmentCombo = new QComboBox(row);
        _alignmentCombo->setObjectName(QString::fromUtf8("FluxTextAlignmentCombo"));
        _alignmentCombo->addItem(tr("Left"));
        _alignmentCombo->addItem(tr("Center"));
        _alignmentCombo->addItem(tr("Right"));
        _alignmentKeyBtn = createKeyButton(tr("Toggle keyframe: Alignment"));
        hl->addWidget(_alignmentCombo, 1);
        hl->addWidget(_alignmentKeyBtn);
        form->addRow(tr("Align"), row);
    }

    bodyLayout->addWidget(_controls);
    bodyLayout->addStretch(1);

    // Value change connections
    connect(_textEdit, &QPlainTextEdit::textChanged, this, &FluxTextPanel::onTextEdited);
    connect(_fontButton, &QPushButton::clicked, this, &FluxTextPanel::onFontDropdownClicked);
    connect(_fontList, &QListWidget::itemClicked, this, &FluxTextPanel::onFontChosen);
    connect(_styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FluxTextPanel::onStyleChanged);
    connect(_sizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &FluxTextPanel::onSizeChanged);
    connect(_fillButton, &QPushButton::clicked, this, &FluxTextPanel::onFillClicked);
    connect(_trackingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &FluxTextPanel::onTrackingChanged);
    connect(_leadingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &FluxTextPanel::onLeadingChanged);
    connect(_alignmentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FluxTextPanel::onAlignmentChanged);

    // Keyframe toggle connections
    connect(_textKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onTextKeyClicked);
    connect(_sizeKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onSizeKeyClicked);
    connect(_fillKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onFillKeyClicked);
    connect(_trackingKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onTrackingKeyClicked);
    connect(_leadingKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onLeadingKeyClicked);
    connect(_alignmentKeyBtn, &QPushButton::clicked, this, &FluxTextPanel::onAlignmentKeyClicked);

    // Timeline playhead — refresh animated values and key states on frame change
    {
        GuiAppInstancePtr app = _gui ? _gui->getApp() : GuiAppInstancePtr();
        if (app) {
            TimeLinePtr tl = app->getTimeLine();
            if (tl) {
                QObject::connect(tl.get(), SIGNAL(frameChanged(SequenceTime, int)),
                                 this, SLOT(onTimelineFrameChanged(SequenceTime, int)));
            }
        }
    }

    setControlsEnabled(false);
}

FluxTextPanel::~FluxTextPanel() {}

// ---------- public ----------

void FluxTextPanel::setActiveNode(const NodePtr& node)
{
    _node = isFluxMotionTextNode(node) ? node : NodePtr();
    syncFromNode();
}

// ---------- private helpers ----------

bool FluxTextPanel::isFluxMotionTextNode(const NodePtr& node) const
{
    return node && node->isActivated() && node->getPluginID() == std::string("net.sf.openfx.FluxMotionText");
}

void FluxTextPanel::setControlsEnabled(bool enabled)
{
    _emptyLabel->setVisible(!enabled);
    _controls->setVisible(enabled);
    _controls->setEnabled(enabled);
    if (!enabled && _fontList) {
        _fontList->setVisible(false);
    }
}

QPushButton* FluxTextPanel::createKeyButton(const QString& tooltip)
{
    FluxKeyDiamondButton* btn = new FluxKeyDiamondButton(_controls);
    btn->setProperty("fluxKeyButton", true);
    btn->setToolTip(tooltip);
    return btn;
}

void FluxTextPanel::populateFonts()
{
    QSignalBlocker blocker(_fontList);
    _fontList->clear();

    QFontDatabase db;
    QStringList families = db.families();
    families.removeDuplicates();
    families.sort(Qt::CaseInsensitive);
    if (families.isEmpty()) {
        families << QString::fromUtf8("Sans");
    }

    _fontList->addItems(families);
}

void FluxTextPanel::populateStyles(const QString& family, const QString& preferredStyle)
{
    QSignalBlocker blocker(_styleCombo);
    _styleCombo->clear();
    QFontDatabase db;
    QStringList styles = db.styles(family);
    if (styles.isEmpty()) {
        styles << QString::fromUtf8("Regular");
    }
    _styleCombo->addItems(styles);
    int index = styles.indexOf(preferredStyle);
    if (index < 0) {
        index = 0;
    }
    _styleCombo->setCurrentIndex(index);
}

void FluxTextPanel::syncFromNode()
{
    _updating = true;
    setControlsEnabled(bool(_node));
    if (_node) {
        QSignalBlocker b0(_textEdit), b1(_fontList), b2(_sizeSpin), b3(_trackingSpin), b4(_leadingSpin), b5(_alignmentCombo);
        const QString family = stringValue("font", QString::fromUtf8("Sans"));
        const QString style = stringValue("fontStyle", QString::fromUtf8("Regular"));
        _textEdit->setPlainText(stringValue("text", QString()));
        QList<QListWidgetItem*> matchingFonts = _fontList->findItems(family, Qt::MatchFixedString);
        QListWidgetItem* fontItem = matchingFonts.isEmpty() ? nullptr : matchingFonts.front();
        if (!fontItem && !family.isEmpty()) {
            fontItem = new QListWidgetItem(family);
            _fontList->addItem(fontItem);
        }
        if (fontItem) {
            _fontList->setCurrentItem(fontItem);
        }
        _fontButton->setText(family + QString::fromUtf8(" ▾"));
        _fontList->setVisible(false);
        populateStyles(family, style);
        _sizeSpin->setValue(doubleValue("fontSize", 96.0));
        _trackingSpin->setValue(doubleValue("tracking", 0.0));
        _leadingSpin->setValue(doubleValue("leading", 0.0));
        _alignmentCombo->setCurrentIndex(std::max(0, std::min(2, intValue("alignment", 1))));
        _fillColor = colorValue("fillColor", _fillColor);
    }
    _updating = false;
    refreshKeyButtons();
}

// ---------- knob value access ----------

KnobIPtr FluxTextPanel::knob(const char* name) const
{
    return _node ? _node->getKnobByName(std::string(name)) : KnobIPtr();
}

QString FluxTextPanel::stringValue(const char* name, const QString& fallback) const
{
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(name));
    return k ? QString::fromStdString(k->getValue(0, ViewSpec::current())) : fallback;
}

double FluxTextPanel::doubleValue(const char* name, double fallback) const
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(knob(name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

int FluxTextPanel::intValue(const char* name, int fallback) const
{
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(name));
    return k ? k->getValue(0, ViewSpec::current()) : fallback;
}

QColor FluxTextPanel::colorValue(const char* name, const QColor& fallback) const
{
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(knob(name));
    if (!k) {
        return fallback;
    }
    return QColor::fromRgbF(k->getValue(0, ViewSpec::current()), k->getValue(1, ViewSpec::current()), k->getValue(2, ViewSpec::current()), k->getValue(3, ViewSpec::current()));
}

void FluxTextPanel::setStringValue(const char* name, const QString& value)
{
    KnobIPtr rawKnob = knob(name);
    KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(resolveWriteTarget(rawKnob));
    if (!k) {
        return;
    }
    const std::string text = value.toStdString();
    const double time = currentFrameTime();
    if (time >= 0.0 && k->isAnimated(0)) {
        KeyFrame key;
        k->setValueAtTime(time, text, ViewSpec::current(), 0, eValueChangedReasonNatronGuiEdited, &key);
    } else {
        k->setValue(text, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, nullptr);
    }
    redraw();
}

void FluxTextPanel::setDoubleValue(const char* name, double value)
{
    KnobIPtr rawKnob = knob(name);
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(resolveWriteTarget(rawKnob));
    if (!k) {
        return;
    }
    const double time = currentFrameTime();
    if (time >= 0.0 && k->isAnimated(0)) {
        KeyFrame key;
        k->setValueAtTime(time, value, ViewSpec::current(), 0, eValueChangedReasonNatronGuiEdited, &key);
    } else {
        k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, nullptr);
    }
    redraw();
}

void FluxTextPanel::setIntValue(const char* name, int value)
{
    KnobIPtr rawKnob = knob(name);
    KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(resolveWriteTarget(rawKnob));
    if (!k) {
        return;
    }
    const double time = currentFrameTime();
    if (time >= 0.0 && k->isAnimated(0)) {
        KeyFrame key;
        k->setValueAtTime(time, value, ViewSpec::current(), 0, eValueChangedReasonNatronGuiEdited, &key);
    } else {
        k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, nullptr);
    }
    redraw();
}

void FluxTextPanel::setColorValue(const char* name, const QColor& value)
{
    KnobIPtr rawKnob = knob(name);
    KnobDoubleBasePtr k = std::dynamic_pointer_cast<KnobDoubleBase>(resolveWriteTarget(rawKnob));
    if (!k) {
        return;
    }
    const double time = currentFrameTime();
    const bool canUseTime = (time >= 0.0);
    const double rgba[4] = { value.redF(), value.greenF(), value.blueF(), value.alphaF() };
    for (int d = 0; d < 4; ++d) {
        if (canUseTime && k->isAnimated(d)) {
            KeyFrame key;
            k->setValueAtTime(time, rgba[d], ViewSpec::current(), d, eValueChangedReasonNatronGuiEdited, &key);
        } else {
            k->setValue(rgba[d], ViewSpec::all(), d, eValueChangedReasonNatronGuiEdited, nullptr);
        }
    }
    redraw();
}

void FluxTextPanel::redraw()
{
    if (_gui) {
        _gui->redrawAllViewers();
    }
}

// ---------- keyframe infrastructure ----------

double FluxTextPanel::currentFrameTime() const
{
    if (!_node) {
        return -1.0;
    }
    AppInstancePtr app = _node->getApp();
    if (!app) {
        return -1.0;
    }
    TimeLinePtr tl = app->getTimeLine();
    if (!tl) {
        return -1.0;
    }
    return static_cast<double>(tl->currentFrame());
}

bool FluxTextPanel::knobHasKeyAtTime(const char* knobName, int dimension, double time) const
{
    KnobIPtr rawKnob = knob(knobName);
    if (!rawKnob) {
        return false;
    }
    // Check the alias master if present — keys live on the master knob.
    KnobIPtr resolved = aliasMaster(rawKnob);
    if (!resolved) {
        resolved = rawKnob;
    }
    CurvePtr curve = resolved->getCurve(ViewSpec::current(), dimension);
    if (!curve) {
        return false;
    }
    KeyFrame kf;
    return curve->getKeyFrameWithTime(time, &kf);
}

void FluxTextPanel::toggleKeyOnKnob(const char* knobName)
{
    if (!_node || _updating) {
        return;
    }
    const double time = currentFrameTime();
    if (time < 0.0) {
        return;
    }

    KnobIPtr rawKnob = knob(knobName);
    if (!rawKnob) {
        return;
    }
    // Resolve alias — keys live on the master knob.
    KnobIPtr resolved = aliasMaster(rawKnob);
    if (!resolved) {
        resolved = rawKnob;
    }

    AppInstancePtr app = _node->getApp();

    // Check if ANY dimension of this knob has a key at the current time.
    // If so, remove keys on all dimensions. Otherwise set keys on all.
    bool hasKey = false;
    const int dim = resolved->getDimension();
    for (int d = 0; d < dim; ++d) {
        if (knobHasKeyAtTime(knobName, d, time)) {
            hasKey = true;
            break;
        }
    }

    if (hasKey) {
        // Remove keys on all dimensions at this time
        for (int d = 0; d < dim; ++d) {
            resolved->onKeyFrameRemoved(time, ViewSpec::current(), d, false);
        }
        if (app) {
            app->removeKeyFrameIndicator(static_cast<SequenceTime>(std::round(time)));
        }
    } else {
        // Set keys on all dimensions at their current values
        for (int d = 0; d < dim; ++d) {
            resolved->onKeyFrameSet(time, ViewSpec::current(), d);
        }
        if (app && !resolved->getIsSecret() && resolved->isDeclaredByPlugin()) {
            app->addKeyframeIndicator(static_cast<SequenceTime>(std::round(time)));
        }
    }

    refreshKeyButtons();
    redraw();
}

void FluxTextPanel::refreshKeyButtons()
{
    const double time = currentFrameTime();
    const bool valid = (time >= 0.0 && _node);

    auto setBtn = [&](QPushButton* btn, const char* knobName, int dim) {
        // All key buttons are created as FluxKeyDiamondButton by createKeyButton()
        auto* dbtn = static_cast<FluxKeyDiamondButton*>(btn);
        dbtn->setKeyActive(valid && knobHasKeyAtTime(knobName, dim, time));
    };

    setBtn(_textKeyBtn,     "text",      0);
    setBtn(_sizeKeyBtn,     "fontSize",  0);
    setBtn(_fillKeyBtn,     "fillColor", 0);
    setBtn(_trackingKeyBtn, "tracking",  0);
    setBtn(_leadingKeyBtn,  "leading",   0);
    setBtn(_alignmentKeyBtn,"alignment", 0);
}

// ---------- value change slots ----------

void FluxTextPanel::onTextEdited() { if (!_updating) setStringValue("text", _textEdit->toPlainText()); }
void FluxTextPanel::onSizeChanged(double value) { if (!_updating) setDoubleValue("fontSize", value); }
void FluxTextPanel::onTrackingChanged(double value) { if (!_updating) setDoubleValue("tracking", value); }
void FluxTextPanel::onLeadingChanged(double value) { if (!_updating) setDoubleValue("leading", value); }
void FluxTextPanel::onAlignmentChanged(int index) { if (!_updating) setIntValue("alignment", index); }

void FluxTextPanel::onFontDropdownClicked()
{
    if (_updating) {
        return;
    }
    _fontList->setVisible(!_fontList->isVisible());
}

void FluxTextPanel::onFontChosen(QListWidgetItem* item)
{
    if (_updating || !item) {
        return;
    }
    const QString family = item->text();
    if (family.isEmpty()) {
        return;
    }
    _fontButton->setText(family + QString::fromUtf8(" ▾"));
    _fontList->setVisible(false);
    populateStyles(family, QString::fromUtf8("Regular"));
    setStringValue("font", family);
    setStringValue("fontStyle", _styleCombo->currentText());
}

void FluxTextPanel::onStyleChanged(int)
{
    if (!_updating) {
        setStringValue("fontStyle", _styleCombo->currentText());
    }
}

void FluxTextPanel::onFillClicked()
{
    if (_updating || !_node) {
        return;
    }
    QColor chosen = QColorDialog::getColor(_fillColor, this, tr("Text Fill Color"), QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        _fillColor = chosen;
        setColorValue("fillColor", chosen);
    }
}

// ---------- keyframe toggle slots ----------

void FluxTextPanel::onTextKeyClicked() { toggleKeyOnKnob("text"); }
void FluxTextPanel::onSizeKeyClicked() { toggleKeyOnKnob("fontSize"); }
void FluxTextPanel::onFillKeyClicked() { toggleKeyOnKnob("fillColor"); }
void FluxTextPanel::onTrackingKeyClicked() { toggleKeyOnKnob("tracking"); }
void FluxTextPanel::onLeadingKeyClicked() { toggleKeyOnKnob("leading"); }
void FluxTextPanel::onAlignmentKeyClicked() { toggleKeyOnKnob("alignment"); }

void FluxTextPanel::onTimelineFrameChanged(SequenceTime, int)
{
    if (_node && isFluxMotionTextNode(_node)) {
        syncFromNode();
    } else {
        refreshKeyButtons();
    }
}

NATRON_NAMESPACE_EXIT
