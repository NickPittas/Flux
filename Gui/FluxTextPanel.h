/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Layer Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXTEXTPANEL_H
#define FLUXTEXTPANEL_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"
#include "Global/GlobalDefines.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QColor>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/EngineFwd.h"
#include "Gui/PanelWidget.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

NATRON_NAMESPACE_ENTER

class Gui;

class FluxTextPanel
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:
    explicit FluxTextPanel(Gui* gui, QWidget* parent = nullptr);
    virtual ~FluxTextPanel();

    void setActiveNode(const NodePtr& node);

private Q_SLOTS:
    void onTextEdited();
    void onFontDropdownClicked();
    void onFontChosen(QListWidgetItem* item);
    void onStyleChanged(int index);
    void onSizeChanged(double value);
    void onFillClicked();
    void onTrackingChanged(double value);
    void onLeadingChanged(double value);
    void onAlignmentChanged(int index);

    /// Keyframe toggle buttons — one per animatable control.
    void onTextKeyClicked();
    void onSizeKeyClicked();
    void onFillKeyClicked();
    void onTrackingKeyClicked();
    void onLeadingKeyClicked();
    void onAlignmentKeyClicked();

    /// Refresh panel when the timeline playhead moves.
    void onTimelineFrameChanged(SequenceTime time, int reason);

private:
    bool isFluxMotionTextNode(const NodePtr& node) const;
    void setControlsEnabled(bool enabled);
    void populateFonts();
    void populateStyles(const QString& family, const QString& preferredStyle);
    void syncFromNode();
    void redraw();

    KnobIPtr knob(const char* name) const;
    QString stringValue(const char* name, const QString& fallback) const;
    double doubleValue(const char* name, double fallback) const;
    int intValue(const char* name, int fallback) const;
    QColor colorValue(const char* name, const QColor& fallback) const;
    void setStringValue(const char* name, const QString& value);
    void setDoubleValue(const char* name, double value);
    void setIntValue(const char* name, int value);
    void setColorValue(const char* name, const QColor& value);

    /// Returns the current timeline frame as a double, or -1 if unavailable.
    double currentFrameTime() const;

    /// Toggle a keyframe on all dimensions of the named knob at the current time.
    /// If a key exists on any dimension at this time, removes keys on all dimensions.
    /// Otherwise sets keys on all dimensions at their current values.
    void toggleKeyOnKnob(const char* knobName);

    /// Returns true if the named knob has a keyframe at the current time on the given dimension.
    bool knobHasKeyAtTime(const char* knobName, int dimension, double time) const;

    /// Refresh the keyframe indicator buttons to reflect current state.
    void refreshKeyButtons();

    /// Create a compact keyframe-toggle button.
    QPushButton* createKeyButton(const QString& tooltip);

private:
    Gui* _gui;
    NodePtr _node;
    bool _updating;
    QColor _fillColor;

    QLabel* _emptyLabel;
    QWidget* _controls;
    QPlainTextEdit* _textEdit;
    QPushButton* _fontButton;
    QListWidget* _fontList;
    QComboBox* _styleCombo;
    QDoubleSpinBox* _sizeSpin;
    QPushButton* _fillButton;
    QDoubleSpinBox* _trackingSpin;
    QDoubleSpinBox* _leadingSpin;
    QComboBox* _alignmentCombo;

    // Keyframe toggle buttons — one per animatable control.
    QPushButton* _textKeyBtn;
    QPushButton* _sizeKeyBtn;
    QPushButton* _fillKeyBtn;
    QPushButton* _trackingKeyBtn;
    QPushButton* _leadingKeyBtn;
    QPushButton* _alignmentKeyBtn;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXTEXTPANEL_H
