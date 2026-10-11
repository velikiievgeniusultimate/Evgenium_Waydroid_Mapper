#include "IntegratedView.h"
#include <QtWaylandCompositor/QWaylandSurface>
#include "LiveDevUi.h"
#include <QApplication>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QThread>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cmath>
#include <cstdio>
#include <cstdlib>

class ExperimentalCalibrationTest {
    static constexpr double Tau=6.283185307179586;
    static void check(bool pass,const char *message) {
        if (!pass) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
    }
    template<class P> static bool wait(P ready,int milliseconds=4000) {
        QElapsedTimer timer;timer.start();
        while(!ready()&&timer.elapsed()<milliseconds) {QCoreApplication::processEvents();QThread::msleep(10);}
        return ready();
    }
    static void click(QQuickWindow *window,const char *target) {
        auto result=LiveDevUi::execute(window,QJsonObject{{"action","click"},
            {"options",QJsonObject{{"target",target}}}},true);
        check(result.value("ok").toBool(),target);
    }
    static void settings(QObject *overlay) {
        check(QMetaObject::invokeMethod(overlay,"openControlSettings",Q_ARG(QVariant,QString("skill")),
            Q_ARG(QVariant,0)),"open settings");
        wait([]{return false;},250);
    }
    static void arrow(IntegratedView &view,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier,bool repeat=false) {
        QKeyEvent press(QEvent::KeyPress,key,modifiers,QString(),repeat,1);
        QKeyEvent release(QEvent::KeyRelease,key,modifiers);
        QCoreApplication::sendEvent(view.integratedWindow(),&press);
        QCoreApplication::sendEvent(view.integratedWindow(),&release);
    }
    static void complete(IntegratedView &view,bool angle) {
        const int count=angle?64:12;
        check(view.calibrationTotal()==count,"wrong calibration count");
        const QPointF trueCenter(view.characterCenter_.x,view.characterCenter_.y);
        const int heldTouch=view.calibrationTouchId_;
        arrow(view,Qt::Key_Right);
        check(std::abs(view.characterCenter_.x-trueCenter.x()-1./800)<1e-12,"arrow did not move by one Android pixel");
        arrow(view,Qt::Key_Down,Qt::ShiftModifier);
        check(std::abs(view.characterCenter_.y-trueCenter.y()-10./600)<1e-12,"Shift arrow did not move by ten pixels");
        arrow(view,Qt::Key_Up,Qt::ShiftModifier);
        for(int i=0;i<count;++i) {
            QPointF input=angle?view.directionalCalibrationVector(i):view.experimentalCalibrationVector(i);
            view.calibrationPointReady_=true;
            view.recordMobaSkillCalibrationPoint(trueCenter.x()+input.x()*200/800,
                trueCenter.y()+input.y()*130/600);
            if(i==0) {
                const auto measured=view.mobaSkills_[0].calibrationPoints;
                const int step=view.calibrationStep_;
                arrow(view,Qt::Key_Left);
                arrow(view,Qt::Key_Right,Qt::NoModifier,true);
                arrow(view,Qt::Key_Left,Qt::NoModifier,true);
                check(std::abs(view.characterCenter_.x-trueCenter.x())<1e-12,"repeat arrow or mid-calibration correction failed");
                check(view.calibrationStep_==step&&view.mobaSkills_[0].calibrationPoints==measured,
                    "centre correction moved recorded screen points or changed step");
                check(view.calibrationTouchId_==heldTouch&&view.activeTapPoints_.size()==1,
                    "centre correction interrupted held finger");
            }
        }
        check(!view.calibrationActive(),"calibration did not finish");
        check(view.activeTapPoints_.isEmpty(),"wizard left touch down");
    }
public:
    static int run(const QString &gameFile) {
        QSettings config;config.setValue("session/lastWidth",800);config.setValue("session/lastHeight",600);config.sync();
        IntegratedView view;view.ensureCompositor();view.setReady(true);view.setWindowVisible(true);
        QProcess game;auto env=QProcessEnvironment::systemEnvironment();env.insert("QT_QPA_PLATFORM","wayland");
        env.insert("WAYLAND_DISPLAY","evgenium-wayland-0");game.setProcessEnvironment(env);
        game.start("/usr/lib/qt6/bin/qml",{gameFile});
        check(wait([&]{return bool(view.inputSurface_);}),"client surface missing");
        view.androidWidth_=800;view.androidHeight_=600;view.loadGlobalCalibration();
        view.setEditMode(true);view.addCharacterCenterAt(.5,.5);view.addMobaSkillAt(.8,.75);
        view.mobaSkills_[0].key=Qt::Key_F;
        view.mobaSkills_[0].calibrationVersion=3;
        for(int i=0;i<64;++i) {auto p=view.directionalCalibrationVector(i);
            view.mobaSkills_[0].calibrationPoints.push_back({.5+p.x()*.25,.5+p.y()*130/600});}
        const auto original=view.mobaSkills_[0].calibrationPoints;
        emit view.mobaSkillsChanged();emit view.selectedMobaSkillChanged();
        auto *window=qobject_cast<QQuickWindow *>(view.integratedWindow());
        auto *overlay=window->findChild<QObject *>("mapperOverlay");check(overlay,"overlay missing");
        settings(overlay);click(window,"experimentalCalibrationTab");
        check(view.mobaSkills_[0].experimental,"UI did not enable experimental mode");
        check(!view.isSkillCalibrated(view.mobaSkills_[0]),"missing global angle accepted");
        if(qEnvironmentVariableIsSet("EWM_TEST_SCREENSHOT_DIR")) {
            wait([]{return false;},150);
            window->grabWindow().save(qEnvironmentVariable("EWM_TEST_SCREENSHOT_DIR")+"/experimental-settings.png");
        }
        const int undoBefore=view.mapperUndoHistory_.size();
        click(window,"experimentalAngleButton");
        check(wait([&]{return view.calibrationTouchId_>=0;}),"angle touch did not start");
        complete(view,true);
        check(view.globalDirections_.size()==64,"global angle missing");
        check(view.mobaSkills_[0].calibrationPoints==original,"original calibration overwritten");
        check(view.mapperUndoHistory_.size()==undoBefore+1,"angle is not one undo step");
        auto savedDirections=view.globalDirections_;
        view.undoMapperEdit();check(view.globalDirections_.empty(),"angle undo failed");
        view.loadGlobalCalibration();check(view.globalDirections_.empty(),"angle undo did not persist");
        view.globalDirections_=savedDirections;view.saveGlobalCalibration();emit view.calibrationChanged();
        wait([]{return false;},250);click(window,"calibrationCompleteClose");wait([]{return false;},250);
        settings(overlay);click(window,"directionSkillTab");
        check(view.mobaSkills_[0].directionalOnly&&view.isSkillCalibrated(view.mobaSkills_[0]),"direction skill requires range");
        const double a=7*Tau/64;
        for(double radius:{20.,100.,250.}) {
            auto result=view.mobaSkillVectorForPointer(0,{.5+std::cos(a)*radius/800,.5+std::sin(a)*radius*.65/600});
            check(std::hypot(result.x()-std::cos(a),result.y()-std::sin(a))<1e-6,"shared angle depends on distance");
        }
        click(window,"rangeSkillTab");check(!view.isSkillCalibrated(view.mobaSkills_[0]),"range-less skill accepted");
        click(window,"experimentalRangeButton");
        check(wait([&]{return view.calibrationTouchId_>=0;}),"range touch did not start");
        complete(view,false);
        check(view.experimentalRangeReady(view.mobaSkills_[0]),"range not ready");
        auto result=view.mobaSkillVectorForPointer(0,{.5+100./800,.5});
        check(std::abs(result.x()-.5)<1e-6&&std::abs(result.y())<1e-6,"range inverse at half distance wrong");
        result=view.mobaSkillVectorForPointer(0,{.5,.5+32.5/600});
        check(std::abs(result.y()-.25)<1e-6,"range inverse at quarter distance wrong");
        result=view.mobaSkillVectorForPointer(0,{.99,.5});
        check(std::abs(result.x()-1)<1e-6,"range does not clamp at maximum");
        check(view.mobaSkillVectorForPointer(0,{.5,.5}).isNull(),"range skill deadzone is not zero");
        check(view.mobaSkills_[0].calibrationPoints==original,"range overwrote original calibration");
        const auto ranges=view.mobaSkills_[0].rangePoints;
        const auto measuredRadius=view.mobaSkills_[0].radius;
        view.mobaSkills_[0].radius*=1.5;
        check(!view.experimentalRangeReady(view.mobaSkills_[0]),"range stayed ready after joystick radius change");
        view.mobaSkills_[0].radius=measuredRadius;
        QTemporaryDir temp;QSettings controls(temp.filePath("controls.ini"),QSettings::IniFormat);
        view.saveControls(controls);view.mobaSkills_.clear();view.loadControls(controls);
        check(view.mobaSkills_[0].experimental&&view.mobaSkills_[0].rangePoints==ranges
            &&view.mobaSkills_[0].calibrationPoints==original,"profile round trip loses experimental/original data");
        // Two incompatible legacy profile centres must never replace the shared centre.
        for(const char *id:{"fixture-a","fixture-b"}) {
            config.beginGroup(QString("profiles/")+id+"/variants/800x600");
            config.setValue("variantSchema",1);
            view.characterCenter_.x=.1;
            view.mobaSkills_[0].rangePoints=ranges;
            if (QString(id)=="fixture-b") for (auto &point:view.mobaSkills_[0].rangePoints) point*=1.5;
            const auto profileRange=view.mobaSkills_[0].rangePoints;
            view.saveControls(config);config.endGroup();config.sync();
            check(view.loadProfileVariant(id,"800x600"),"profile load failed");
            check(view.characterCenter_.x==.5&&view.globalDirections_.size()==64,"profile replaced global calibration");
            check(view.mobaSkills_[0].rangePoints==profileRange,"profile lost its own range");
        }
        check(view.loadProfileVariant("fixture-a","800x600"),"return to first profile failed");
        check(view.mobaSkills_[0].rangePoints==ranges,"second profile overwrote first profile range");
        view.androidWidth_=1024;view.androidHeight_=768;view.loadGlobalCalibration();
        check(!view.characterCenter_.enabled&&view.globalDirections_.empty(),"calibration leaked to another resolution");
        check(!view.experimentalRangeReady(view.mobaSkills_[0]),"old resolution range accepted");
        view.addCharacterCenterAt(.6,.55);
        view.androidWidth_=800;view.androidHeight_=600;view.loadGlobalCalibration();
        check(view.characterCenter_.x==.5&&view.globalDirections_.size()==64,"resolution calibration not restored");
        check(view.experimentalRangeReady(view.mobaSkills_[0]),"original resolution range not restored");
        wait([]{return false;},250);click(window,"calibrationCompleteClose");wait([]{return false;},250);
        view.selectMobaSkill(0);view.beginExperimentalCalibration(0,true);
        check(wait([&]{return view.calibrationTouchId_>=0;}),"cancel test did not press");
        view.cancelMobaSkillCalibration();
        check(view.globalDirections_==savedDirections&&view.activeTapPoints_.isEmpty(),"cancel changed shared angle or kept touch");
        view.beginExperimentalCalibration(0,false);
        for(int i=0;i<12;++i) {view.calibrationPointReady_=true;view.recordMobaSkillCalibrationPoint(.6,.5);}
        check(!view.calibrationActive()&&view.mobaSkills_[0].rangePoints==ranges,"invalid range replaced accepted calibration");
        QFile journal(view.globalCalibration().value("measurementsPath").toString());
        check(journal.open(QIODevice::ReadOnly),"measurement journal missing");
        int count=0;while(!journal.atEnd()) {auto record=QJsonDocument::fromJson(journal.readLine()).object();
            check(record.value("resolution")=="800x600","journal resolution incorrect");
            check(record.value("screenPoints").toArray().size()==(count==0?64:12),"raw measurements missing");
            check(record.value("globalDirections").toArray().size()==64,"angle snapshot missing from journal");++count;}
        check(count==2,"cancelled/invalid calibration was journalled as accepted");
        // A long downward skill: full and half travel are outside the viewport.
        // Their placeholder centres must not be accepted as measured endpoints.
        const auto beforeClipped=view.mobaSkills_[0];
        view.beginExperimentalCalibration(0,false);
        check(view.rangeCalibrationActive(),"range-only HUD unavailable");
        for(int i=0;i<12;++i) {
            view.calibrationPointReady_=true;
            emit view.calibrationChanged();
            if(i==1 || i==5) {
                if(i==1) {
                    if(qEnvironmentVariableIsSet("EWM_TEST_SCREENSHOT_DIR"))
                        window->grabWindow().save(qEnvironmentVariable("EWM_TEST_SCREENSHOT_DIR")+"/offscreen-calibration.png");
                    click(window,"offscreenRangeButton");
                    check(view.calibrationStep_==2,"UI skip recorded a screen click instead");
                    view.undoMobaSkillCalibrationPoint();
                    check(view.calibrationStep_==1 && view.rangeCalibrationMissingMask_==0,
                        "back did not remove skipped flag");
                    view.calibrationPointReady_=true;
                }
                view.skipOffscreenRangePoint();
            } else {
                const auto input=view.experimentalCalibrationVector(i);
                view.recordMobaSkillCalibrationPoint(.5+input.x()*200/800,
                    .5+input.y()*(i%4==1 ? 800 : 130)/600);
            }
        }
        check(view.experimentalRangeReady(view.mobaSkills_[0]),"clipped range rejected");
        check(view.mobaSkills_[0].rangeMissingMask==((1<<1)|(1<<5)),"clip flags lost");
        auto down=[&](double pixels) {return view.experimentalSkillVector(0,{.5,.5+pixels/600}).y();};
        check(std::abs(down(100)-.125)<1e-6,"interior accuracy changed by edge acceleration");
        check(std::abs(down(200)-.25)<1e-6,"last visible point was distorted");
        check(down(250)>.3125 && down(250)<1,"missing edge acceleration");
        check(std::abs(down(300)-1)<1e-6,"screen edge cannot reach full range");
        double previous=0;
        for(int d=6;d<=300;++d) {double current=down(d);
            check(current+1e-9>=previous,"edge response is not monotonic");previous=current;}
        result=view.experimentalSkillVector(0,{.5+100./800,.5});
        check(std::abs(result.x()-.5)<1e-6,"visible neighbouring direction changed");
        view.setSelectedSkillEdgeZone(12);
        check(view.mobaSkills_[0].edgeZonePercent==12,"edge zone setting unavailable");
        view.saveControls(controls);view.mobaSkills_.clear();view.loadControls(controls);
        check(view.mobaSkills_[0].rangeMissingMask==34 && view.mobaSkills_[0].edgeZonePercent==12,
            "profile round trip lost clipping or edge zone");
        // Even 25% may be offscreen. Retry that same step at smaller travel.
        view.beginExperimentalCalibration(0,false);
        for(int i=0;i<12;++i) {
            view.calibrationPointReady_=true;
            if(i==1 || i==5) view.skipOffscreenRangePoint();
            else {
                if(i==9) for(int retry=0;retry<2;++retry) {
                    view.skipOffscreenRangePoint();
                    check(view.calibrationStep_==9,"small-range retry advanced the point");
                    view.calibrationPointReady_=true;
                }
                auto input=view.experimentalCalibrationVector(i);
                view.recordMobaSkillCalibrationPoint(.5+input.x()*200/800,
                    .5+input.y()*(i%4==1 ? 3200 : 130)/600);
            }
        }
        check(view.experimentalRangeReady(view.mobaSkills_[0])
            && view.mobaSkills_[0].rangeMinimumFractions[1]==.0625,"small visible anchor lost");
        check(std::abs(down(200)-.0625)<1e-6 && std::abs(down(300)-1)<1e-6,
            "small anchor mapping or full edge range wrong");
        view.saveControls(controls);view.mobaSkills_.clear();view.loadControls(controls);
        check(view.mobaSkills_[0].rangeMinimumFractions[1]==.0625,"retry fraction not persisted");
        view.beginExperimentalCalibration(0,false);
        view.calibrationPointReady_=true;view.skipOffscreenRangePoint();
        view.cancelMobaSkillCalibration();
        check(view.mobaSkills_[0].rangeMinimumFractions[1]==.0625
            && view.mobaSkills_[0].rangeMissingMask==34,"cancel damaged clipped calibration");
        QFile clippedJournal(view.globalCalibration().value("measurementsPath").toString());
        check(clippedJournal.open(QIODevice::ReadOnly),"clipped journal missing");
        QJsonObject last;
        while(!clippedJournal.atEnd()) last=QJsonDocument::fromJson(clippedJournal.readLine()).object();
        check(last.value("missingPointMask").toInt()==34
            && last.value("screenPoints").toArray()[1].isNull()
            && last.value("screenPoints").toArray()[9].isArray(),"journal calls inferred points measured");
        check(std::abs(last.value("inputVectors").toArray()[9].toArray()[1].toDouble()-.0625)<1e-8,
            "journal lost actual finger travel after retry");
        view.mobaSkills_[0]=beforeClipped;
        for(int version:{1,2,3}) {
            const auto centre=view.characterCenter_;
            view.beginMobaSkillCalibration(0,version);
            check(wait([&]{return view.calibrationTouchId_>=0;}),"original calibration touch missing");
            view.calibrationPointReady_=true;view.recordMobaSkillCalibrationPoint(.7,.5);
            const auto points=view.mobaSkills_[0].calibrationPoints;
            const auto step=view.calibrationStep_;
            arrow(view,Qt::Key_Right);arrow(view,Qt::Key_Up,Qt::ShiftModifier);
            check(std::abs(view.characterCenter_.x-centre.x-1./800)<1e-12
                &&std::abs(view.characterCenter_.y-centre.y+10./600)<1e-12,
                "original calibration does not support centre arrows");
            check(view.calibrationStep_==step&&view.mobaSkills_[0].calibrationPoints==points,
                "original centre correction changed existing samples");
            view.moveCalibrationCharacterCenter(0,0);
            arrow(view,Qt::Key_Left);arrow(view,Qt::Key_Up,Qt::ShiftModifier);
            check(view.characterCenter_.x==0&&view.characterCenter_.y==0,"centre escaped screen boundary");
            view.cancelMobaSkillCalibration();
            check(view.characterCenter_.x==centre.x&&view.characterCenter_.y==centre.y,
                "cancel did not restore centre");
        }
        // Denser range contours must measure diagonals independently of a
        // clipped downward ray. Both legacy 12-point and new formats coexist.
        const auto oldDenseSkill=view.mobaSkills_[0];
        const auto oldDenseDirections=view.globalDirections_;
        view.globalDirections_.clear();
        for(int j=0;j<64;++j) view.globalDirections_.push_back({std::cos(j*Tau/64),std::sin(j*Tau/64)});
        view.characterCenter_.x=.5;view.characterCenter_.y=.5;
        view.selectMobaSkill(0);
        settings(overlay);
        auto *density=window->findChild<QObject *>("experimentalRangeDensity");
        check(density,"density selector missing");
        density->setProperty("currentIndex",1);
        check(QMetaObject::invokeMethod(density,"activated",Q_ARG(int,1)),"density UI signal missing");
        check(view.mobaSkills_[0].rangeSampleCount==24 && view.experimentalRangeReady(view.mobaSkills_[0]),
            "density choice discarded previous 12-point calibration");
        view.undoMapperEdit();check(view.mobaSkills_[0].rangeSampleCount==12,"density preference undo failed");
        click(window,"skillSettingsClose");
        for(int samples:{24,48}) {
            view.selectMobaSkill(0);
            view.setSelectedSkillRangeSamples(samples);
            const int n=samples/3;
            view.beginExperimentalCalibration(0,false);
            check(view.calibrationTotal()==samples,"dense wizard count wrong");
            check(view.calibrationInstruction().contains(QString("1/%1").arg(n)),"HUD has wrong direction count");
            for(int j=0;j<samples;++j) {
                view.calibrationPointReady_=true;
                const int d=j%n, ring=j/n;
                const bool clipped=d==n/4 || (n==16 && d==15);
                if(clipped && ring<2) view.skipOffscreenRangePoint();
                else {
                    auto input=view.experimentalCalibrationVector(j);
                    const double reach=clipped?1000:300;
                    view.recordMobaSkillCalibrationPoint(.5+input.x()*reach/800,.5+input.y()*reach/600);
                }
            }
            check(!view.calibrationActive() && view.experimentalRangeReady(view.mobaSkills_[0])
                && view.mobaSkills_[0].rangePoints.size()==size_t(samples),"dense calibration failed");
            const double diagonal=250/std::sqrt(2.0);
            auto v=view.experimentalSkillVector(0,{.5+diagonal/800,.5+diagonal/600});
            check(std::abs(std::hypot(v.x(),v.y())-250./300)<1e-7,
                "clipped downward sample distorted visible lower-right diagonal");
            v=view.experimentalSkillVector(0,{.5-diagonal/800,.5+diagonal/600});
            check(std::abs(std::hypot(v.x(),v.y())-250./300)<1e-7,
                "clipped downward sample distorted visible lower-left diagonal");
            v=view.experimentalSkillVector(0,{.5,1});
            check(std::abs(v.y()-1)<1e-7,"dense clipped ray cannot reach full range");
            const auto mask=view.mobaSkills_[0].rangeMissingMask;
            if(n==16) check(mask & (quint64(1)<<31),"48-point skip mask truncated high bit");
            const auto densePoints=view.mobaSkills_[0].rangePoints;
            view.saveControls(controls);view.mobaSkills_.clear();view.loadControls(controls);
            check(view.mobaSkills_[0].rangeSampleCount==samples
                && view.mobaSkills_[0].rangePoints==densePoints
                && view.mobaSkills_[0].rangeMissingMask==mask
                && view.experimentalRangeReady(view.mobaSkills_[0]),"dense calibration persistence failed");
            view.beginExperimentalCalibration(0,false);
            view.calibrationPointReady_=true;view.skipOffscreenRangePoint();
            view.undoMobaSkillCalibrationPoint();
            check(view.calibrationStep_==0 && !view.rangeCalibrationMissingMask_,"dense skip undo failed");
            view.cancelMobaSkillCalibration();
            check(view.mobaSkills_[0].rangePoints==densePoints,"dense cancel lost accepted points");
        }
        view.storeControlInBaggage("skill",0,"dense skill");
        view.baggageItems_.clear();view.loadBaggage();
        check(view.baggageItems_.back().skill.rangePoints.size()==48
            && view.baggageItems_.back().skill.rangeMinimumFractions.size()==16
            && (view.baggageItems_.back().skill.rangeMissingMask & (quint64(1)<<31)),
            "baggage lost dense range data");
        view.mobaSkills_[0]=oldDenseSkill;view.globalDirections_=oldDenseDirections;
        // Movement uses the same inverse angle, without skill range rules.
        view.addMobaMovementAt(.2,.75);
        check(QMetaObject::invokeMethod(overlay,"openControlSettings",Q_ARG(QVariant,QString("movement")),
            Q_ARG(QVariant,0)),"open movement settings");
        wait([]{return false;},200);
        click(window,"experimentalMovementTab");
        check(view.mobaMovement_.experimental,"experimental movement UI failed");
        if(qEnvironmentVariableIsSet("EWM_TEST_SCREENSHOT_DIR")) {
            wait([]{return false;},100);
            window->grabWindow().save(qEnvironmentVariable("EWM_TEST_SCREENSHOT_DIR")+"/movement-settings.png");
        }
        view.undoMapperEdit();check(!view.mobaMovement_.experimental,"movement mode undo failed");
        view.setMobaMovementExperimental(true);
        view.globalDirections_.clear();
        view.beginMobaMovement({.7,.5});
        check(!view.mobaMovementActive_,"movement started without shared angle");
        view.wasdKeys_.insert(Qt::Key_W);view.updateWasdMovement();
        check(!view.mobaMovementActive_,"WASD started without shared angle");view.wasdKeys_.clear();
        for(int n=0;n<64;++n) {double a=n*Tau/64+Tau/4;
            view.globalDirections_.push_back({std::cos(a),std::sin(a)});}
        view.characterCenter_.x=.5;view.characterCenter_.y=.5;
        view.beginMobaMovement({.7,.5});
        const double radius=view.mobaMovement_.radius*600;
        check(view.mobaMovementActive_ && std::abs(view.mobaLastTouch_.x()-.2)<1e-8
            && std::abs(view.mobaLastTouch_.y()-(.75-radius/600))<1e-8,
            "experimental mouse movement ignored global rotation");
        view.endMobaMovement();view.wasdKeys_.insert(Qt::Key_W);view.updateWasdMovement();
        check(std::abs(view.mobaLastTouch_.x()-(.2-radius/800))<1e-8
            && std::abs(view.mobaLastTouch_.y()-.75)<1e-8,"WASD ignored shared rotation");
        view.wasdKeys_.clear();view.updateWasdMovement();
        view.saveControls(controls);view.mobaMovement_.experimental=false;view.loadControls(controls);
        check(view.mobaMovement_.experimental,"profile lost experimental movement");
        view.storeControlInBaggage("movement",0,"rotated movement");
        view.baggageItems_.clear();view.loadBaggage();
        check(!view.baggageItems_.empty() && view.baggageItems_.back().movement.experimental,
            "baggage lost experimental movement");
        view.setMobaMovementExperimental(false);view.beginMobaMovement({.7,.5});
        check(std::abs(view.mobaLastTouch_.x()-(.2+radius/800))<1e-8
            && std::abs(view.mobaLastTouch_.y()-.75)<1e-8,"original movement changed");
        view.endMobaMovement();
        click(window,"movementSettingsClose");
        view.setEditMode(false);view.bindings_.clear();
        view.mobaMovement_.inputMode=0;view.mobaMovement_.mouseButton=Qt::RightButton;
        view.beginMobaMovementPress({.7,.5});
        view.finishMobaMovementPress({.7,.5});
        check(view.mobaMovementAutoActive_,"short RMB click did not start route");
        const int routeId=view.mobaMovementTouchId_;
        const auto routeTouch=view.mobaLastTouch_;
        auto mouse=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons,QPointF point) {
            const QRectF surface=view.androidSurfaceRect(window);
            QPointF local(surface.x()+point.x()*surface.width(),surface.y()+point.y()*surface.height());
            QMouseEvent event(type,local,local,button,buttons,Qt::NoModifier);
            check(view.eventFilter(window,&event),"movement pointer event leaked to compositor");
        };
        mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton,{.1,.1});
        const int lmbId=view.movementPointerTouchId_;
        check(lmbId>=0 && lmbId!=routeId && view.activeTapPoints_.size()==2,"LMB not independent from route touch");
        wait([]{return false;},60);
        check(view.movementPointerTouchId_==lmbId && view.activeTapPoints_.contains(lmbId),"LMB became a short tap");
        mouse(QEvent::MouseMove,Qt::NoButton,Qt::LeftButton,{.1,.3});
        check(std::abs(view.activeTapPoints_[lmbId].y()-.3)<1e-8
            && view.mobaMovementTouchId_==routeId && view.mobaLastTouch_==routeTouch,
            "minimap drag changed movement or did not move LMB");
        view.cancelMobaMovementGesture();
        check(view.activeTapPoints_.contains(lmbId),"ending route released held LMB");
        mouse(QEvent::MouseMove,Qt::NoButton,Qt::LeftButton,{.1,.4});
        mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton,{.1,.4});
        check(view.movementPointerTouchId_<0 && view.activeTapPoints_.isEmpty(),"LMB release left touch down");
        view.beginMobaMovementPress({.7,.5});
        mouse(QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton|Qt::RightButton,{.1,.1});
        check(view.movementPointerTouchId_>=0,"second click did not start independent LMB touch");
        const auto heldDirection=view.mobaLastTouch_;
        mouse(QEvent::MouseMove,Qt::NoButton,Qt::LeftButton|Qt::RightButton,{.1,.3});
        check(view.mobaLastTouch_==heldDirection,"dragging LMB redirected held RMB");
        view.releaseAllTapTouches();
        check(view.movementPointerTouchId_<0 && view.activeTapPoints_.isEmpty(),"global cleanup left mouse touch down");
        game.terminate();game.waitForFinished(3000);
        std::puts("PASS: experimental UI, held calibration, shared angle, radius inverse, original preservation, undo, profile and resolution isolation, cancellation, validation, durable measurement journal, offscreen skips, small anchors, monotonic edge acceleration and persistence, shared movement angle, held independent LMB and drag");
        return 0;
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);QApplication::setApplicationName("EWMExperimentalTest");
    QApplication::setOrganizationName("EWMTest");
    return ExperimentalCalibrationTest::run(QString::fromLocal8Bit(argv[1]));
}
