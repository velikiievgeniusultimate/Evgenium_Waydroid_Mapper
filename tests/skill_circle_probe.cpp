#include "SkillCircleDetector.h"
#include <QCoreApplication>
#include <QLineF>
#include <QPainter>
#include <cstdio>
#include <cstdlib>

static void check(bool pass, const char* message)
{
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    check(argc == 2, "fixture directory required");
    struct Example {
        const char* name;
        QPointF center;
        double radius;
    };
    for (const auto& sample : { Example { "centered-1.png", { 188, 177 }, 116 },
             Example { "centered-2.png", { 242, 196 }, 116 },
             Example { "centered-3.png", { 165, 181 }, 116 },
             Example { "thumb-offset.png", { 169, 139 }, 116 } }) {
        QImage image(QString::fromLocal8Bit(argv[1]) + "/" + sample.name);
        check(!image.isNull(), "fixture could not load");
        auto result = detectSkillCircle(image, { }, sample.center, false);
        check(result.found, "outer circle missing in user screenshot");
        check(QLineF(result.center, sample.center).length() < 6, "wrong centre in user screenshot");
        check(std::abs(result.radius - sample.radius) < 6, "thumb mistaken for outer circle");
        check(!detectSkillCircle(image, image, sample.center, true).found,
            "unchanged HUD accepted as new joystick");
    }
    for(const char *name:{"stationary-1.png","stationary-2.png","centered-1.png","centered-2.png","centered-3.png","thumb-offset.png"}) {
        QImage im(QString::fromLocal8Bit(argv[1])+"/"+name);
        auto result=detectStationarySkillCircle(im,{},QPointF(im.width()*.5,im.height()*.5),true);
        std::printf("Precise %s found=%d center=%.4f,%.4f radius=%.4f thumb=%.4f rms=%.4f coverage=%.3f\n",name,result.found,result.center.x(),result.center.y(),result.radius,result.thumbRadius,result.centerResidual,result.confidence);
    }
    for (const char *name:{"stationary-1.png","stationary-2.png"}) {
        QImage im(QString::fromLocal8Bit(argv[1])+"/"+name);
        const auto result=detectStationarySkillCircle(im,{}, {184,163},true);
        check(result.found && result.centerResidual<.6 && result.thumbRadius>35,
            "stationary screenshot rims not measured accurately");
        check(!detectStationarySkillCircle(im,im,{184,163},true).found,
            "precise fit accepted an unchanged HUD");
    }
    QImage offset(QString::fromLocal8Bit(argv[1])+"/thumb-offset.png");
    check(!detectStationarySkillCircle(offset,{}, {169,139},true).found,
        "displaced thumb accepted as stationary centre");
    QImage onlyOuter(800,600,QImage::Format_RGB32);onlyOuter.fill(QColor(80,100,70));
    {QPainter p(&onlyOuter);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(140,195,245),3));p.setBrush(QColor(100,160,225,95));
        p.drawEllipse(QPointF(520,330),90,90);}
    check(!detectStationarySkillCircle(onlyOuter,{}, {520,330},true).found,
        "missing thumb accepted as a precise centre");
    for (int large:{0,1}) for(int occluded:{0,1}) {
        QImage base(large?4000:800,large?3000:600,QImage::Format_RGB32);
        base.fill(QColor(80,100,70));QImage im=base;
        const QPointF truth=large?QPointF(2720.37,1700.71):QPointF(520.37,330.71);
        const double radius=large?340.3:90.3;
        { QPainter p(&im);p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(140,195,245),large?5:3));p.setBrush(QColor(100,160,225,95));
            p.drawEllipse(truth+QPointF(.5,.5),radius,radius);
            p.drawEllipse(truth+QPointF(.5,.5),radius/3,radius/3);
            if(occluded) p.fillRect(QRectF(truth.x()-radius-5,truth.y()-radius*.25,radius*.85,radius*.5),QColor(100,110,50));
        }
        const auto result=detectStationarySkillCircle(im,base,truth,false);
        std::printf("Synthetic %dx%d occluded=%d found=%d error=%.4f radiusError=%.4f rms=%.4f\n",
            im.width(),im.height(),occluded,result.found,QLineF(truth,result.center).length(),result.radius-radius,result.centerResidual);
        check(result.found && QLineF(truth,result.center).length()<.4,
            "native subpixel centre incorrect on known synthetic geometry");
    }
    QImage twoPairs(800,600,QImage::Format_RGB32);twoPairs.fill(QColor(80,100,70));
    {QPainter p(&twoPairs);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(140,195,245),3));p.setBrush(QColor(100,160,225,95));
        for(auto c:{QPointF(250,330),QPointF(520,330)}) {
            p.drawEllipse(c,90,90);p.drawEllipse(c,30,30);
        }}
    check(!detectStationarySkillCircle(twoPairs,{}, {400,330},true).found,
        "two verified joystick pairs accepted as unambiguous");
    QImage liveBefore(QString::fromLocal8Bit(argv[1])+"/live-hud-before.png");
    QImage liveHeld(QString::fromLocal8Bit(argv[1])+"/live-hud-held.png");
    check(!liveBefore.isNull() && !liveHeld.isNull(),"live frames missing");
    const auto live=detectStationarySkillCircle(liveHeld,liveBefore,{3074,2720},false);
    std::printf("Live HUD pair found=%d center=%.4f,%.4f radius=%.4f rms=%.4f\n",live.found,live.center.x(),live.center.y(),live.radius,live.centerResidual);
    check(live.found && QLineF(live.center,{3073.5,2719.5}).length()<3,
        "neighbouring round HUD button blocks verified joystick pair");
    QImage uranus(QString::fromLocal8Bit(argv[1])+"/../uranus/center.png");
    auto center = detectUranusCenter(uranus, {}, {106,160}, true);
    check(center.found && QLineF(center.center,{105.81,165.69}).length()<1,
        "Uranus bright centre not found accurately");
    check(!detectUranusCenter(uranus,uranus,{106,160},true).found,
        "unchanged cyan marker accepted");
    QImage upscaled=uranus.scaled(uranus.width()*8,uranus.height()*8);
    auto largeCenter=detectUranusCenter(upscaled,{}, {848,1280},true);
    check(largeCenter.found && QLineF(largeCenter.center,center.center*8+QPointF(3.5,3.5)).length()<6,
        "Uranus full-resolution centre refinement failed");
    QImage justDot(400,300,QImage::Format_RGB32);justDot.fill(QColor(80,100,70));
    {QPainter p(&justDot);p.setBrush(QColor(100,254,254));p.setPen(Qt::NoPen);p.drawEllipse(QPointF(200,150),3,2);}
    check(!detectUranusCenter(justDot,{}, {200,150},true).found,
        "isolated cyan light accepted without floor ellipse");
    QImage before(1280, 720, QImage::Format_RGB32);
    before.fill(QColor(80, 100, 70));
    QImage frame = before;
    {
        QPainter p(&frame);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(140, 195, 245), 3));
        p.setBrush(QColor(100, 160, 225, 95));
        p.drawEllipse(QPointF(1080, 180), 90, 90);
        p.drawEllipse(QPointF(1115, 155), 28, 28);
    }
    check(!detectSkillCircle(frame, before, { 80, 600 }, false).found,
        "local search unexpectedly global");
    auto global = detectSkillCircle(frame, before, { 80, 600 }, true);
    check(global.found && QLineF(global.center, { 1080, 180 }).length() < 7
            && std::abs(global.radius - 90) < 7,
        "global expansion or offset thumb failed");
    check(!detectSkillCircle(before, { }, { 400, 400 }, true).found, "blank frame accepted");
    check(!detectSkillCircle({ }, { }, { }, true).found, "null frame accepted");
    // Verify frame downscaling returns source pixels at Android resolutions.
    auto large = detectSkillCircle(
        frame.scaled(3840, 2160), before.scaled(3840, 2160), { 3240, 540 }, false);
    check(large.found && QLineF(large.center, { 3240, 540 }).length() < 20
            && std::abs(large.radius - 270) < 20,
        "source coordinate scaling failed");
    std::puts("PASS: six screenshots, stationary/displaced thumb, subpixel ground truth with occlusion at 800px and 4K, local/global, unchanged HUD, blank/null, Uranus");
}
