/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Text Animator Panel
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXTEXTANIMATORPANEL_H
#define FLUXTEXTANIMATORPANEL_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Engine/EngineFwd.h"
#include "Global/GlobalDefines.h"
#include "Gui/PanelWidget.h"

#include <QColor>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

NATRON_NAMESPACE_ENTER

class Gui;

class FluxTextAnimatorPanel
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:
    explicit FluxTextAnimatorPanel(Gui* gui, QWidget* parent = nullptr);
    ~FluxTextAnimatorPanel();

    void setActiveNode(const NodePtr& node);
    void setSelectedAnimatorId(int animatorId);

private Q_SLOTS:
    void onAddAnimator();
    void onTimelineFrameChanged(SequenceTime time, int reason);

private:
    bool isFluxMotionTextNode(const NodePtr& node) const;
    void rebuild();
    void clearAnimatorWidgets();
    void syncJsonAndRefresh();
    double currentFrameTime() const;

    KnobIPtr knob(const QString& name) const;
    double doubleValue(const QString& name, int dim, double fallback) const;
    int intValue(const QString& name, int fallback) const;
    bool boolValue(const QString& name, bool fallback) const;
    QString stringValue(const QString& name, const QString& fallback) const;
    QColor colorValue(const QString& name, const QColor& fallback) const;

    void setDoubleValue(const QString& name, int dim, double value);
    void setIntValue(const QString& name, int value);
    void setBoolValue(const QString& name, bool value);
    void setStringValue(const QString& name, const QString& value);
    void setColorValue(const QString& name, const QColor& value);
    void toggleKey(const QString& name, int dims);
    bool hasKey(const QString& name, int dim, double time) const;
    QPushButton* keyButton(const QString& name, int dims, const QString& tip);

    QWidget* makeDoubleRow(const QString& label, const QString& knobName, int dim,
                           double min, double max, double step, int keyDims,
                           QWidget* parent, bool withSlider = false);
    QWidget* makeVec2Row(const QString& label, const QString& knobName,
                          double min, double max, double step, QWidget* parent);
    QWidget* makeScaleRow(int animatorId, QWidget* parent);
    QWidget* makeColorRow(const QString& label, const QString& knobName, QWidget* parent);
    QGroupBox* makeAnimatorGroup(int animatorId, int index, int count, QWidget* parent);

private:
    Gui* _gui;
    NodePtr _node;
    bool _updating;
    QLabel* _emptyLabel;
    QWidget* _body;
    QVBoxLayout* _animatorLayout;
    QPushButton* _addButton;
    int _selectedAnimatorId;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXTEXTANIMATORPANEL_H
