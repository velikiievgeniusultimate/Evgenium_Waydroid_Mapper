#include "IntegratedView.h"
#include "LiveDevUi.h"
#include <QQuickWindow>
#include <QJsonObject>
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

class AutoGeometryIntegrationTest {
    static void check(bool pass, const char* message)
    {
        if (!pass) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }
    template <class Predicate> static bool wait(Predicate ready, int milliseconds = 8000)
    {
        QElapsedTimer time;
        time.start();
        while (!ready() && time.elapsed() < milliseconds) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        return ready();
    }

public:
    static int run(const QString& clientFile)
    {
        IntegratedView view;
        QTemporaryDir configuration;
        QSettings legacy(configuration.filePath("controls.ini"), QSettings::IniFormat);
        legacy.beginWriteArray("mobaSkills", 7);
        for (int i = 0; i < 7; ++i) {
            legacy.setArrayIndex(i);
            legacy.setValue("cancelReactionLevel", i < 5 ? i + 1 : 1);
            if (i >= 5) legacy.setValue("cancelDurationMs", i == 5 ? 137 : 0);
        }
        legacy.endArray();
        view.loadControls(legacy);
        const int expected[] = {180, 110, 65, 30, 0, 137, 0};
        for (int i = 0; i < 7; ++i)
            check(view.mobaSkills_[i].cancelDurationMs == expected[i], "legacy cancellation migration failed");
        view.saveControls(legacy);
        check(!legacy.contains("mobaSkills/1/cancelReactionLevel"), "legacy preset persisted after saving");
        view.loadControls(legacy);
        for (int i = 0; i < 7; ++i)
            check(view.mobaSkills_[i].cancelDurationMs == expected[i], "custom milliseconds did not round-trip");
        view.mobaSkills_.clear();
        {
            QSettings baggage;
            baggage.beginGroup("baggage");
            baggage.beginWriteArray("items", 1);
            baggage.setArrayIndex(0);
            baggage.setValue("id", "test-cancel-migration");
            baggage.setValue("name", "Skill");
            baggage.setValue("kind", 3);
            baggage.setValue("skillCancelReaction", 4);
            baggage.endArray();baggage.endGroup();baggage.sync();
        }
        view.loadBaggage();
        check(view.baggageItems_[0].skill.cancelDurationMs == 30, "baggage preset migration failed");
        view.baggageItems_[0].skill.cancelDurationMs = 137;
        view.saveBaggage();view.loadBaggage();
        check(view.baggageItems_[0].skill.cancelDurationMs == 137, "baggage custom duration lost");
        view.ensureCompositor();
        view.setReady(true);
        view.setWindowVisible(true);
        QProcess game;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "wayland");
        env.insert("WAYLAND_DISPLAY", "evgenium-wayland-0");
        game.setProcessEnvironment(env);
        game.start("/usr/lib/qt6/bin/qml", { clientFile });
        check(
            wait([&] { return bool(view.inputSurface_); }), "fake Android surface did not arrive");
        view.androidWidth_ = 800;
        view.androidHeight_ = 600;
        view.setEditMode(true);
        view.addMobaSkillAt(.8, .75);
        auto& skill = view.mobaSkills_[0];
        skill.artificialCenterEnabled = true;
        skill.artificialX = .75;
        skill.artificialY = .7;
        skill.calibrationVersion = 1;
        skill.calibrationPoints.assign(view.expectedCalibrationCount(skill), QPointF(.04, 0));
        skill.key = Qt::Key_F;
        view.characterCenter_.enabled = true;
        view.characterCenter_.x = .5;
        view.characterCenter_.y = .5;
        emit view.mobaSkillsChanged();
        emit view.characterCenterChanged();
        auto *window = qobject_cast<QQuickWindow *>(view.integratedWindow());
        auto *overlay = window->findChild<QObject *>("mapperOverlay");
        check(overlay && QMetaObject::invokeMethod(overlay, "openControlSettings",
            Q_ARG(QVariant, QString("skill")), Q_ARG(QVariant, 0)), "settings did not open");
        wait([] { return false; }, 250);
        auto clicked = LiveDevUi::execute(window,
            QJsonObject{{"action", "click"}, {"options", QJsonObject{{"target", "skillCancelDuration"}}}}, true);
        check(clicked.value("ok").toBool(), "duration field is unavailable");
        auto key = [window](int code, Qt::KeyboardModifiers modifiers, const QString &text = {}) {
            QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
            QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
            QCoreApplication::sendEvent(window, &press);QCoreApplication::sendEvent(window, &release);
        };
        key(Qt::Key_A, Qt::ControlModifier);
        for (QChar digit : QString("137")) key(Qt::Key_0 + digit.digitValue(), Qt::NoModifier, QString(digit));
        key(Qt::Key_Return, Qt::NoModifier);
        check(view.mobaSkills_[0].cancelDurationMs == 137, "typing milliseconds into SpinBox did not apply");
        auto *popup = overlay->findChild<QObject *>("skillSettingsPopup");
        check(popup && QMetaObject::invokeMethod(popup, "close"), "settings did not close");
        wait([] { return false; }, 250);
        view.selectMobaSkill(0);
        check(view.dispatchBoundInput(Qt::Key_F, true, false, false, { .7, .6 }, true),
            "skill bind was not handled");
        check(view.activeMobaSkillTouchIds_.contains(0) && !view.activeTapPoints_.isEmpty(),
            "normal skill must press Android immediately on key down");
        view.updateMobaSkills({ .72, .61 });
        view.dispatchBoundInput(Qt::Key_F, false, true, false, {}, false);
        check(wait([&] { return view.activeTapPoints_.isEmpty(); }),
            "normal skill must release after key up");
        view.skillCancel_.enabled = true;
        view.skillCancel_.key = Qt::Key_T;
        view.skillCancel_.x = .1;view.skillCancel_.y = .1;
        view.setSelectedMobaSkillCancelDurationMs(137);
        view.dispatchBoundInput(Qt::Key_F, true, false, false, { .7, .6 }, true);
        QElapsedTimer cancellation;cancellation.start();
        view.cancelActiveMobaSkills();
        check(!view.activeTapPoints_.isEmpty(), "custom cancellation released immediately");
        check(wait([&] { return view.activeTapPoints_.isEmpty(); }), "custom cancellation never released");
        check(cancellation.elapsed() >= 137 && cancellation.elapsed() < 800, "custom cancellation duration ignored");
        view.setSelectedMobaSkillCancelDurationMs(0);
        view.dispatchBoundInput(Qt::Key_F, true, false, false, { .7, .6 }, true);
        view.cancelActiveMobaSkills();
        check(view.activeTapPoints_.isEmpty(), "zero milliseconds must cancel immediately");
        view.setSelectedMobaSkillCancelDurationMs(65);
        const auto original = view.mobaSkills_[0];
        const auto undoSize = view.mapperUndoHistory_.size();
        bool releasedBeforeAnalysis=false;
        const auto analysisConnection=QObject::connect(&view,&IntegratedView::autoGeometryChanged,&view,[&] {
            if(view.autoGeometryStatus().contains(QStringLiteral("Снимки сохранены")))
                releasedBeforeAnalysis=view.autoGeometryActive() && view.autoGeometrySnapshots_.size()==3
                    && view.autoGeometryTouchId_==-1 && view.activeTapPoints_.isEmpty();
        });
        view.beginAutoGeometry(0);
        check(view.autoGeometryActive(), "search did not start");
        check(!view.reloadMapperOverlay().isEmpty(), "reload allowed during search");
        check(wait([&] { return !view.autoGeometryActive(); }), "search hung");
        QObject::disconnect(analysisConnection);
        check(releasedBeforeAnalysis,"finger held during photograph analysis");
        std::fprintf(stderr, "search result: %s\n", qPrintable(view.autoGeometryStatus()));
        check(std::abs(view.mobaSkills_[0].x - .7125) < .0005
                && std::abs(view.mobaSkills_[0].y - .66667) < .0005,
            "wrong centre from actual Wayland frame");
        check(std::abs(view.mobaSkills_[0].radius - .155) < .005,
            "wrong radius from actual Wayland frame");
        check(view.mobaSkills_[0].artificialX == .75 && view.mobaSkills_[0].artificialY == .7,
            "artificial point changed");
        check(view.mobaSkills_[0].calibrationStale
                && view.mobaSkills_[0].calibrationPoints == original.calibrationPoints,
            "calibration discarded instead of marked stale");
        check(view.activeTapPoints_.isEmpty() && view.autoGeometryTouchId_ == -1,
            "finger left held after success");
        check(view.mapperUndoHistory_.size() == undoSize + 1, "geometry is not one undo step");
        view.undoMapperEdit();
        check(view.mobaSkills_[0].x == original.x && view.mobaSkills_[0].radius == original.radius
                && !view.mobaSkills_[0].calibrationStale,
            "undo failed");
        // Cancel after the photographs were taken, while the worker may still
        // be computing. A late worker must neither apply geometry nor add undo.
        bool cancelledAnalysis=false;
        const auto cancelConnection=QObject::connect(&view,&IntegratedView::autoGeometryChanged,&view,[&] {
            if(view.autoGeometryStatus().contains(QStringLiteral("Снимки сохранены")))
                QTimer::singleShot(0,&view,[&] {view.cancelAutoGeometry();cancelledAnalysis=true;});
        });
        view.beginAutoGeometry(0);
        check(wait([&] {return cancelledAnalysis;}),"analysis cancellation was not reached");
        QObject::disconnect(cancelConnection);
        QElapsedTimer drain;drain.start();
        check(wait([&] {return drain.elapsed()>700;}),"worker drain timed out");
        check(!view.autoGeometryActive() && view.activeTapPoints_.isEmpty()
                && view.mobaSkills_[0].x==original.x && view.mobaSkills_[0].radius==original.radius,
            "cancelled analysis applied a late result or left a finger held");
        view.beginAutoGeometry(0);
        check(wait([&] { return view.autoGeometryTouchId_ >= 0; }), "second press failed");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(view.integratedWindow(), &escape);
        check(!view.autoGeometryActive() && view.activeTapPoints_.isEmpty(),
            "cancel left finger held");
        check(view.mobaSkills_[0].x == original.x, "cancel changed geometry");
        // A capture error must release the finger and ignore late callbacks.
        view.beginAutoGeometry(0);
        check(wait([&] { return view.autoGeometryTouchId_ >= 0; }), "third press failed");
        view.autoGeometryCapturePending_ = true;
        view.reportAutoGeometryFrameFailure(view.autoGeometryFrameId_, "test capture failure");
        check(!view.autoGeometryActive() && view.activeTapPoints_.isEmpty(),
            "capture error left touch active");
        check(view.mobaSkills_[0].x == original.x, "capture error changed geometry");
        const auto oldCenter = view.characterCenter_;
        const auto beforeHeroUndo = view.mapperUndoHistory_.size();
        check(QMetaObject::invokeMethod(overlay,"openControlSettings",
            Q_ARG(QVariant,QString("center")),Q_ARG(QVariant,-1)),"centre settings did not open");
        wait([] { return false; },250);
        if (qEnvironmentVariableIsSet("EWM_TEST_SCREENSHOT_DIR"))
            window->grabWindow().save(qEnvironmentVariable("EWM_TEST_SCREENSHOT_DIR") + "/hero-settings.png");
        check(LiveDevUi::execute(window, QJsonObject{{"action","click"},
            {"options",QJsonObject{{"target","uranusSearchButton"}}}},true).value("ok").toBool(),
            "Uranus search button missing");
        check(overlay->property("uranusPicking").toBool(),"ultimate picker did not appear");
        wait([] { return false; },250);
        const double pickX=overlay->property("uranusUltimateX").toDouble();
        check(LiveDevUi::execute(window,QJsonObject{{"action","drag"},
            {"options",QJsonObject{{"target","uranusUltimateBadge"},{"dx",-100},{"dy",0}}}},true).value("ok").toBool(),
            "ultimate marker is not draggable");
        check(overlay->property("uranusUltimateX").toDouble()<pickX,"drag did not move ultimate marker");
        if (qEnvironmentVariableIsSet("EWM_TEST_SCREENSHOT_DIR"))
            window->grabWindow().save(qEnvironmentVariable("EWM_TEST_SCREENSHOT_DIR") + "/ultimate-picker.png");
        overlay->setProperty("uranusUltimateX",.25);
        overlay->setProperty("uranusUltimateY",.7);
        check(LiveDevUi::execute(window,QJsonObject{{"action","click"},
            {"options",QJsonObject{{"target","uranusStartHold"}}}},true).value("ok").toBool(),
            "hold-and-find button missing");
        check(view.autoGeometryActive() && view.autoGeometrySkillIndex_==-2,"Uranus mode did not start");
        check(wait([&]{return !view.autoGeometryActive();}),"Uranus search hung");
        std::fprintf(stderr,"Uranus result: %s\n",qPrintable(view.autoGeometryStatus()));
        check(std::abs(view.characterCenter_.x-.5375)<.004 && std::abs(view.characterCenter_.y-.5025)<.004,
            "Uranus floor marker centre incorrect");
        check(view.mapperUndoHistory_.size()==beforeHeroUndo+1,"hero centre is not a single undo step");
        check(view.activeTapPoints_.isEmpty(),"Uranus search left finger down");
        view.undoMapperEdit();
        check(view.characterCenter_.x==oldCenter.x && view.characterCenter_.y==oldCenter.y,
            "hero centre undo failed");
        QMetaObject::invokeMethod(overlay->findChild<QObject *>("characterCenterSettings"),"close");
        wait([]{return false;},250);
        // Each mapper settings header has an actual working close mark.
        for (const char *type : {"tap","center","movement","cancel"}) {
            if(QString(type)=="tap") view.addTapAt(.4,.4);
            if(QString(type)=="movement") view.addMobaMovementAt(.4,.4);
            if(QString(type)=="cancel") view.addSkillCancelAt(.1,.1);
            QMetaObject::invokeMethod(overlay,"openControlSettings",
                Q_ARG(QVariant,QString(type)),Q_ARG(QVariant,0));
            wait([]{return false;},250);
            const QString name=QString(type)=="tap" ? "bindingSettings" : QString(type)=="center"
                ? "characterCenterSettings" : QString(type)=="movement" ? "movementSettings" : "cancelSettings";
            check(LiveDevUi::execute(window,QJsonObject{{"action","click"},
                {"options",QJsonObject{{"target",name+"Close"}}}},true).value("ok").toBool(),"close mark missing");
            wait([]{return false;},250);
            check(!overlay->findChild<QObject *>(name)->property("opened").toBool(),qPrintable("close mark did not close " + name));
        }
        view.beginUranusCenterSearch(.25,.7);
        check(wait([&]{return view.autoGeometryTouchId_ >= 0;}),"Uranus cancel press failed");
        view.cancelAutoGeometry();
        check(!view.autoGeometryActive() && view.activeTapPoints_.isEmpty(),"Uranus cancel left touch active");
        check(view.characterCenter_.x==oldCenter.x && view.characterCenter_.y==oldCenter.y,
            "Uranus cancel changed centre");
        // Surface teardown during a held Uranus search must invalidate pending work.
        view.beginUranusCenterSearch(.25,.7);
        check(wait([&] { return view.autoGeometryTouchId_ >= 0; }), "fourth press failed");
        game.terminate();
        game.waitForFinished(3000);
        check(wait([&] { return !view.autoGeometryActive(); }), "surface loss left search active");
        check(view.activeTapPoints_.isEmpty(), "surface loss left touch active");
        std::puts("PASS: real Wayland touch/render capture, three native snapshots and release-before-analysis, artificial point, "
                  "calibration, undo, cancel, capture failure, typed duration, migration and timed cancellation");
        return 0;
    }
};
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EWM-AutoGeometry-Test");
    QCoreApplication::setApplicationName("AutoGeometry-Test");
    if (argc != 2)
        return 2;
    return AutoGeometryIntegrationTest::run(QString::fromLocal8Bit(argv[1]));
}
