#pragma once
#include <QImage>
#include <QPointF>

struct SkillCircleResult {
    bool found = false;
    QPointF center; // Original-frame sample coordinates: pixel centres at 0, 1, ...
    double radius = 0;
    double confidence = 0;
    double centerResidual = 0; // RMS radial residual, source pixels (not an accuracy guarantee).
    double thumbRadius = 0;
};

// Detect the translucent blue joystick, not the small skill icon inside it.
// A pre-press frame excludes permanent circular HUD controls.
SkillCircleResult detectSkillCircle(
    const QImage& frame, const QImage& before, QPointF anchor, bool fullScreen);

// Only for a DOWN-only capture: reject a missing or displaced thumb. Search
// broadly at reduced resolution, fit both rims independently at native resolution.
SkillCircleResult detectStationarySkillCircle(
    const QImage& frame, const QImage& before, QPointF anchor, bool fullScreen);

// Uranus' floor marker is a perspective ellipse. Its small bright cyan dot,
// validated by the surrounding rim, gives the hero centre directly.
SkillCircleResult detectUranusCenter(const QImage &frame, const QImage &before,
                                    QPointF approximateCenter, bool fullScreen);
