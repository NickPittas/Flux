/* Env-gated autonomous validation harness for T074.
 * This file is inert unless FLUX_T074_AUTOMATED_PROOF=1 is set.
 */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "FluxT074Harness.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QAbstractItemView>
#include <QTimer>
#include <QTreeWidgetItem>

#include "Engine/Curve.h"
#include "Engine/Knob.h"
#include "Engine/Node.h"

#include "Gui/DopeSheet.h"
#include "Gui/DopeSheetEditor.h"
#include "Gui/DopeSheetHierarchyView.h"
#include "Gui/DopeSheetView.h"
#include "Gui/FluxTimeline.h"
#include "Gui/Gui.h"
#include "Gui/TabWidget.h"

NATRON_NAMESPACE_ENTER

namespace {

static QString
t074DefaultOutputDir()
{
    const QString envDir = qEnvironmentVariable("FLUX_T074_OUT_DIR");
    if (!envDir.isEmpty()) {
        return envDir;
    }
    return QString::fromUtf8("/tmp/opencode/t074-proof-%1").arg( QCoreApplication::applicationPid() );
}

static void
t074ProcessEvents(int rounds = 4)
{
    for (int i = 0; i < rounds; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 100);
    }
}

static TabWidget*
t074FindParentTabWidget(QWidget* widget)
{
    QWidget* parent = widget ? widget->parentWidget() : 0;
    while (parent) {
        TabWidget* tab = dynamic_cast<TabWidget*>(parent);
        if (tab) {
            return tab;
        }
        parent = parent->parentWidget();
    }
    return 0;
}

static QJsonArray
t074KeyTimes(const CurvePtr& curve)
{
    QJsonArray ret;
    if (!curve) {
        return ret;
    }
    KeyFrameSet keys = curve->getKeyFrames_mt_safe();
    for (KeyFrameSet::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        ret.append( it->getTime() );
    }
    return ret;
}

static int
t074DifferentPixelCount(const QImage& image,
                       const QPoint& center,
                       int radius,
                       const QColor& background)
{
    int count = 0;
    for (int y = center.y() - radius; y <= center.y() + radius; ++y) {
        if ( (y < 0) || (y >= image.height()) ) {
            continue;
        }
        for (int x = center.x() - radius; x <= center.x() + radius; ++x) {
            if ( (x < 0) || (x >= image.width()) ) {
                continue;
            }
            QColor c = image.pixelColor(x, y);
            int delta = std::abs( c.red() - background.red() ) +
                        std::abs( c.green() - background.green() ) +
                        std::abs( c.blue() - background.blue() );
            if (delta > 70) {
                ++count;
            }
        }
    }
    return count;
}

static bool
t074SaveJson(const QString& path, const QJsonObject& obj)
{
    QFile f(path);
    if ( !f.open(QIODevice::WriteOnly | QIODevice::Truncate) ) {
        return false;
    }
    f.write( QJsonDocument(obj).toJson(QJsonDocument::Indented) );
    f.close();
    return true;
}

class T074Runner
    : public QObject
{
public:
    explicit T074Runner(Gui* gui)
        : QObject(gui)
        , _gui(gui)
        , _outDir( t074DefaultOutputDir() )
        , _report()
        , _attempts(0)
    {
        QDir().mkpath(_outDir);
        _report.insert(QString::fromUtf8("startedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        _report.insert(QString::fromUtf8("outputDir"), _outDir);
        _report.insert(QString::fromUtf8("harness"), QString::fromUtf8("FluxT074Harness"));
        QTimer::singleShot(750, this, [this]() { stepCreateLayer(); });
    }

private:
    void fail(const QString& reason)
    {
        _report.insert(QString::fromUtf8("status"), QString::fromUtf8("fail"));
        _report.insert(QString::fromUtf8("reason"), reason);
        finish();
    }

    void finish()
    {
        _report.insert(QString::fromUtf8("finishedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        const QString reportPath = _outDir + QString::fromUtf8("/report.json");
        t074SaveJson(reportPath, _report);
        fprintf(stderr, "[T074-HARNESS] report=%s status=%s\n",
                reportPath.toUtf8().constData(),
                _report.value(QString::fromUtf8("status")).toString().toUtf8().constData());
        if ( !qEnvironmentVariableIsSet("FLUX_T074_KEEP_OPEN") ) {
            QTimer::singleShot(250, this, []() {
                fflush(stderr);
                fflush(stdout);
                std::_Exit(0);
            });
        }
    }

    void stepCreateLayer()
    {
        if (!_gui) {
            fail(QString::fromUtf8("Gui pointer missing"));
            return;
        }
        FluxTimeline* timeline = _gui->getFluxTimeline();
        if (!timeline) {
            fail(QString::fromUtf8("FluxTimeline missing"));
            return;
        }

        _report.insert(QString::fromUtf8("launchCommand"), QString::fromUtf8("QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb FLUX_T074_AUTOMATED_PROOF=1 /home/npittas/Flux/build/App/Natron"));
        timeline->addTextLayer();
        t074ProcessEvents();
        QTimer::singleShot(750, this, [this]() { stepWaitForGizmo(); });
    }

    void stepWaitForGizmo()
    {
        FluxTimeline* timeline = _gui->getFluxTimeline();
        if (!timeline) {
            fail(QString::fromUtf8("FluxTimeline disappeared"));
            return;
        }
        const QList<FluxLayer>& layers = timeline->getLayers();
        if ( layers.empty() || !layers.front().gizmoNode ) {
            if (++_attempts < 12) {
                t074ProcessEvents();
                QTimer::singleShot(500, this, [this]() { stepWaitForGizmo(); });
                return;
            }
            fail(QString::fromUtf8("Text layer gizmoNode was not created"));
            return;
        }

        _layerName = layers.front().name;
        _gizmoNode = layers.front().gizmoNode;
        _report.insert(QString::fromUtf8("layerName"), _layerName);
        _report.insert(QString::fromUtf8("nodeLabel"), QString::fromUtf8( _gizmoNode->getLabel().c_str() ));
        _report.insert(QString::fromUtf8("pluginID"), QString::fromUtf8( _gizmoNode->getPluginID().c_str() ));
        stepSetKeys();
    }

    void stepSetKeys()
    {
        _rotateKnob = _gizmoNode ? _gizmoNode->getKnobByName("Text1rotate") : KnobIPtr();
        if (!_rotateKnob) {
            fail(QString::fromUtf8("Text1rotate knob missing"));
            return;
        }

        _rotateKnob->setAnimationEnabled(true);
        KnobDoubleBasePtr rotateDouble = std::dynamic_pointer_cast<KnobDoubleBase>(_rotateKnob);
        if (!rotateDouble) {
            fail(QString::fromUtf8("Text1rotate is not KnobDoubleBase"));
            return;
        }

        KeyFrame keyA;
        KeyFrame keyB;
        rotateDouble->setValueAtTime(1.0, 0.0, ViewSpec(0), 0, eValueChangedReasonUserEdited, &keyA);
        rotateDouble->setValueAtTime(75.0, 45.0, ViewSpec(0), 0, eValueChangedReasonUserEdited, &keyB);
        t074ProcessEvents(8);

        CurvePtr curve = _rotateKnob->getCurve(ViewSpec(0), 0);
        _report.insert(QString::fromUtf8("knobName"), QString::fromUtf8( _rotateKnob->getName().c_str() ));
        _report.insert(QString::fromUtf8("curveKeyCount"), curve ? curve->getKeyFramesCount() : -1);
        _report.insert(QString::fromUtf8("curveKeyTimes"), t074KeyTimes(curve));

        QTimer::singleShot(500, this, [this]() { stepPrepareDopeSheet(); });
    }

    void stepPrepareDopeSheet()
    {
        DopeSheetEditor* editor = _gui->getDopeSheetEditor();
        if (!editor) {
            fail(QString::fromUtf8("DopeSheetEditor missing"));
            return;
        }
        DopeSheet* model = editor->getModelForT074Proof();
        HierarchyView* hierarchy = editor->getHierarchyView();
        DopeSheetView* view = editor->getDopesheetView();
        if (!model || !hierarchy || !view) {
            fail(QString::fromUtf8("DopeSheet model/view/hierarchy missing"));
            return;
        }

        DSNodePtr dsNode = model->findDSNode( _gizmoNode.get() );
        _targetKnob.reset();
        if (dsNode) {
            const DSTreeItemKnobMap& knobs = dsNode->getItemKnobMap();
            for (DSTreeItemKnobMap::const_iterator it = knobs.begin(); it != knobs.end(); ++it) {
                DSKnobPtr candidate = it->second;
                if (candidate && candidate->getInternalKnob() == _rotateKnob && candidate->getDimension() == 0) {
                    _targetKnob = candidate;
                    break;
                }
            }
        }

        _report.insert(QString::fromUtf8("dopeSheetNodeFound"), dsNode ? true : false);
        _report.insert(QString::fromUtf8("dopeSheetKnobFound"), _targetKnob ? true : false);
        if (!_targetKnob) {
            fail(QString::fromUtf8("DopeSheet target knob row not found"));
            return;
        }

        TabWidget* tab = t074FindParentTabWidget(editor);
        if (tab) {
            tab->setCurrentWidget(editor);
        }
        editor->show();
        editor->raise();
        hierarchy->expandAll();
        hierarchy->scrollToItem(_targetKnob->getTreeItem(), QAbstractItemView::PositionAtCenter);
        editor->centerOn(0.0, 80.0);
        editor->refreshSelectionBboxAndRedrawView();
        view->redraw();
        t074ProcessEvents(10);

        const QRect rowRect = hierarchy->visualItemRect( _targetKnob->getTreeItem() );
        _rowCenterY = rowRect.center().y();
        _report.insert(QString::fromUtf8("dopeSheetActive"), tab ? (tab->currentWidget() == editor) : false);
        _report.insert(QString::fromUtf8("rowText"), _targetKnob->getTreeItem()->text(0));
        _report.insert(QString::fromUtf8("rowHidden"), _targetKnob->getTreeItem()->isHidden());
        _report.insert(QString::fromUtf8("rowVisibleFromOutside"), hierarchy->itemIsVisibleFromOutside(_targetKnob->getTreeItem()));
        QJsonObject row;
        row.insert(QString::fromUtf8("x"), rowRect.x());
        row.insert(QString::fromUtf8("y"), rowRect.y());
        row.insert(QString::fromUtf8("width"), rowRect.width());
        row.insert(QString::fromUtf8("height"), rowRect.height());
        row.insert(QString::fromUtf8("centerY"), _rowCenterY);
        _report.insert(QString::fromUtf8("rowRect"), row);

        QTimer::singleShot(500, this, [this]() { stepCapture(); });
    }

    void stepCapture()
    {
        DopeSheetEditor* editor = _gui->getDopeSheetEditor();
        DopeSheetView* view = editor ? editor->getDopesheetView() : 0;
        if (!editor || !view) {
            fail(QString::fromUtf8("DopeSheet editor/view missing during capture"));
            return;
        }

        t074ProcessEvents(8);
        const QString mainPath = _outDir + QString::fromUtf8("/main-window.png");
        const QString editorPath = _outDir + QString::fromUtf8("/dopesheet-editor.png");
        const QString viewPath = _outDir + QString::fromUtf8("/dopesheet-view.png");
        _gui->grab().save(mainPath);
        editor->grab().save(editorPath);
        QImage viewImage = view->grabFramebuffer();
        viewImage.save(viewPath);

        QJsonObject screenshots;
        screenshots.insert(QString::fromUtf8("mainWindow"), mainPath);
        screenshots.insert(QString::fromUtf8("dopeSheetEditor"), editorPath);
        screenshots.insert(QString::fromUtf8("dopeSheetView"), viewPath);
        _report.insert(QString::fromUtf8("screenshots"), screenshots);

        QJsonArray samples;
        bool imagePass = true;
        const double keyTimes[2] = {1.0, 75.0};
        for (int i = 0; i < 2; ++i) {
            double x = keyTimes[i];
            double y = 0.0;
            view->toWidgetCoordinates(&x, &y);
            const QPoint center((int)std::lround(x), _rowCenterY);
            QPoint bgPoint(5, std::max(0, std::min(viewImage.height() - 1, _rowCenterY)) );
            QColor background = viewImage.isNull() ? QColor() : viewImage.pixelColor(bgPoint);
            int count = viewImage.isNull() ? 0 : t074DifferentPixelCount(viewImage, center, 8, background);
            if (count < 20) {
                imagePass = false;
            }
            QJsonObject sample;
            sample.insert(QString::fromUtf8("time"), keyTimes[i]);
            sample.insert(QString::fromUtf8("x"), center.x());
            sample.insert(QString::fromUtf8("y"), center.y());
            sample.insert(QString::fromUtf8("differentPixelCount"), count);
            samples.append(sample);
        }
        _report.insert(QString::fromUtf8("imageSamples"), samples);
        _report.insert(QString::fromUtf8("imageAssertionPassed"), imagePass);

        bool logicalPass = _report.value(QString::fromUtf8("dopeSheetNodeFound")).toBool() &&
                           _report.value(QString::fromUtf8("dopeSheetKnobFound")).toBool() &&
                           !_report.value(QString::fromUtf8("rowHidden")).toBool() &&
                           _report.value(QString::fromUtf8("rowVisibleFromOutside")).toBool() &&
                           (_report.value(QString::fromUtf8("curveKeyCount")).toInt() >= 2);
        _report.insert(QString::fromUtf8("logicalAssertionPassed"), logicalPass);
        _report.insert(QString::fromUtf8("status"), (logicalPass && imagePass) ? QString::fromUtf8("pass") : QString::fromUtf8("fail"));
        finish();
    }

private:
    Gui* _gui;
    QString _outDir;
    QJsonObject _report;
    int _attempts;
    QString _layerName;
    NodePtr _gizmoNode;
    KnobIPtr _rotateKnob;
    DSKnobPtr _targetKnob;
    int _rowCenterY;
};

} // namespace

void
FluxT074Harness::maybeStart(Gui* gui)
{
    if ( !qEnvironmentVariableIsSet("FLUX_T074_AUTOMATED_PROOF") ) {
        return;
    }
    fprintf(stderr, "[T074-HARNESS] enabled\n");
    new T074Runner(gui);
}

NATRON_NAMESPACE_EXIT
