/* Env-gated autonomous validation harness for T079.
 * This file is inert unless FLUX_T079_AUTOMATED_PROOF=1 is set.
 */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "FluxT079Harness.h"

#include <cstdio>
#include <cstdlib>
#include <list>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "Engine/Curve.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"

#include "Gui/FluxTimeline.h"
#include "Gui/Gui.h"
#include "Gui/ViewerGL.h"
#include "Gui/ViewerTab.h"

NATRON_NAMESPACE_ENTER

namespace {

static QString
t079DefaultOutputDir()
{
    const QString envDir = qEnvironmentVariable("FLUX_T079_OUT_DIR");
    if (!envDir.isEmpty()) {
        return envDir;
    }
    return QString::fromUtf8("/tmp/opencode/t079-gui-proof-%1").arg( QCoreApplication::applicationPid() );
}

static void
t079ProcessEvents(int rounds = 4)
{
    for (int i = 0; i < rounds; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 100);
    }
}

static bool
t079SaveJson(const QString& path,
             const QJsonObject& obj)
{
    QFile f(path);
    if ( !f.open(QIODevice::WriteOnly | QIODevice::Truncate) ) {
        return false;
    }
    f.write( QJsonDocument(obj).toJson(QJsonDocument::Indented) );
    f.close();
    return true;
}

static int
t079VisibleCyanPixelCount(const QImage& image)
{
    if ( image.isNull() ) {
        return 0;
    }
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.blue() > 120 && c.green() > 70 && c.red() < 120) {
                ++count;
            }
        }
    }
    return count;
}

static ViewerTab*
t079FirstViewer(Gui* gui)
{
    if (!gui) {
        return 0;
    }
    ViewerTab* active = gui->getActiveViewer();
    if (active) {
        return active;
    }
    const std::list<ViewerTab*>& viewers = gui->getViewersList();
    if ( !viewers.empty() ) {
        return viewers.front();
    }
    return 0;
}

class T079Runner
    : public QObject
{
public:
    explicit T079Runner(Gui* gui)
        : QObject(gui)
        , _gui(gui)
        , _outDir( t079DefaultOutputDir() )
        , _report()
        , _attempts(0)
    {
        QDir().mkpath(_outDir);
        _report.insert(QString::fromUtf8("startedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        _report.insert(QString::fromUtf8("outputDir"), _outDir);
        _report.insert(QString::fromUtf8("harness"), QString::fromUtf8("FluxT079Harness"));
        _report.insert(QString::fromUtf8("launchCommand"), QString::fromUtf8("QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb FLUX_T079_AUTOMATED_PROOF=1 /home/npittas/Flux/build/App/Natron"));
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
        t079SaveJson(reportPath, _report);
        fprintf(stderr, "[T079-HARNESS] report=%s status=%s\n",
                reportPath.toUtf8().constData(),
                _report.value(QString::fromUtf8("status")).toString().toUtf8().constData());
        if ( !qEnvironmentVariableIsSet("FLUX_T079_KEEP_OPEN") ) {
            QTimer::singleShot(250, this, []() {
                fflush(stderr);
                fflush(stdout);
                std::_Exit(0);
            });
        }
    }

    void stepCreateLayer()
    {
        FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : 0;
        if (!timeline) {
            fail(QString::fromUtf8("FluxTimeline missing"));
            return;
        }
        timeline->addTextLayer();
        t079ProcessEvents(10);
        QTimer::singleShot(750, this, [this]() { stepWaitForLayer(); });
    }

    void stepWaitForLayer()
    {
        FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : 0;
        if (!timeline) {
            fail(QString::fromUtf8("FluxTimeline disappeared"));
            return;
        }
        const QList<FluxLayer>& layers = timeline->getLayers();
        if ( layers.empty() || !layers.front().gizmoNode ) {
            if (++_attempts < 12) {
                t079ProcessEvents(8);
                QTimer::singleShot(500, this, [this]() { stepWaitForLayer(); });
                return;
            }
            fail(QString::fromUtf8("Text layer gizmoNode was not created"));
            return;
        }

        _gizmoNode = layers.front().gizmoNode;
        _report.insert(QString::fromUtf8("layerName"), layers.front().name);
        _report.insert(QString::fromUtf8("nodeLabel"), QString::fromUtf8( _gizmoNode->getLabel().c_str() ));
        _report.insert(QString::fromUtf8("pluginID"), QString::fromUtf8( _gizmoNode->getPluginID().c_str() ));
        if (_gizmoNode->getPluginID() != std::string("net.sf.openfx.FluxMotionText")) {
            fail(QString::fromUtf8("Text layer did not use net.sf.openfx.FluxMotionText"));
            return;
        }
        stepSetParams();
    }

    void stepSetParams()
    {
        KnobIPtr text = _gizmoNode->getKnobByName("text");
        KnobStringBasePtr textKnob = std::dynamic_pointer_cast<KnobStringBase>(text);
        if (textKnob) {
            textKnob->setValue("T079 GUI FluxMotionText", ViewSpec::all(), 0);
        }

        KnobIPtr fontSize = _gizmoNode->getKnobByName("fontSize");
        KnobDoubleBasePtr fontSizeKnob = std::dynamic_pointer_cast<KnobDoubleBase>(fontSize);
        if (fontSizeKnob) {
            fontSizeKnob->setValue(128.0, ViewSpec::all(), 0);
        }

        KnobIPtr translate = _gizmoNode->getKnobByName("translate");
        KnobDoubleBasePtr translateKnob = std::dynamic_pointer_cast<KnobDoubleBase>(translate);
        if (!translateKnob) {
            fail(QString::fromUtf8("translate knob missing or wrong type"));
            return;
        }
        translateKnob->setAnimationEnabled(true);
        KeyFrame keyA;
        KeyFrame keyB;
        translateKnob->setValueAtTime(1.0, 0.0, ViewSpec(0), 0, eValueChangedReasonUserEdited, &keyA);
        translateKnob->setValueAtTime(24.0, 120.0, ViewSpec(0), 0, eValueChangedReasonUserEdited, &keyB);
        _report.insert(QString::fromUtf8("translateKeyCount"), translate->getCurve(ViewSpec(0), 0) ? translate->getCurve(ViewSpec(0), 0)->getKeyFramesCount() : -1);

        ViewerTab* viewerTab = t079FirstViewer(_gui);
        if (viewerTab) {
            viewerTab->show();
            viewerTab->raise();
            viewerTab->seek(1);
            viewerTab->refresh(false);
        }
        t079ProcessEvents(20);
        QTimer::singleShot(2500, this, [this]() { stepCapture(); });
    }

    void stepCapture()
    {
        const QString mainPath = _outDir + QString::fromUtf8("/main-window.png");
        const QString viewerPath = _outDir + QString::fromUtf8("/viewer-framebuffer.png");
        _gui->grab().save(mainPath);

        bool viewerDisplaying = false;
        int visibleCyanPixels = 0;
        ViewerTab* viewerTab = t079FirstViewer(_gui);
        ViewerGL* viewer = viewerTab ? viewerTab->getViewer() : 0;
        if (viewer) {
            viewerTab->refresh(false);
            t079ProcessEvents(20);
            viewerDisplaying = viewer->displayingImage();
            QImage viewerImage = viewer->grabFramebuffer();
            viewerImage.save(viewerPath);
            visibleCyanPixels = t079VisibleCyanPixelCount(viewerImage);
        }

        QJsonObject screenshots;
        screenshots.insert(QString::fromUtf8("mainWindow"), mainPath);
        screenshots.insert(QString::fromUtf8("viewerFramebuffer"), viewerPath);
        _report.insert(QString::fromUtf8("screenshots"), screenshots);
        _report.insert(QString::fromUtf8("viewerDisplayingImage"), viewerDisplaying);
        _report.insert(QString::fromUtf8("visibleCyanPixels"), visibleCyanPixels);
        _report.insert(QString::fromUtf8("status"), (viewerDisplaying && visibleCyanPixels > 100 && _report.value(QString::fromUtf8("translateKeyCount")).toInt() >= 2) ? QString::fromUtf8("pass") : QString::fromUtf8("fail"));
        finish();
    }

private:
    Gui* _gui;
    QString _outDir;
    QJsonObject _report;
    int _attempts;
    NodePtr _gizmoNode;
};

} // namespace

void
FluxT079Harness::maybeStart(Gui* gui)
{
    if ( !qEnvironmentVariableIsSet("FLUX_T079_AUTOMATED_PROOF") ) {
        return;
    }
    fprintf(stderr, "[T079-HARNESS] enabled\n");
    new T079Runner(gui);
}

NATRON_NAMESPACE_EXIT
