#include "IntegratedView.h"
#include <QFutureWatcher>
#include <QLineF>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

void IntegratedView::beginAutoGeometry(int index)
{
    if (autoGeometryActive() || !editMode_ || !windowVisible_ || !inputSurface_
        || calibrationActive() || waitingForKey_ || centerVision_->visible() || index < 0
        || index >= static_cast<int>(mobaSkills_.size()) || !activeTapPoints_.isEmpty())
        return;
    autoGeometryCharacterSearch_ = false;
    const auto &skill = mobaSkills_[index];
    startAutoGeometryCapture(index, skill.artificialCenterEnabled
        ? QPointF(skill.artificialX, skill.artificialY) : QPointF(skill.x, skill.y));
}

void IntegratedView::beginUranusCenterSearch(double normalizedX, double normalizedY)
{
    if (autoGeometryActive() || !editMode_ || !windowVisible_ || !inputSurface_
        || calibrationActive() || waitingForKey_ || centerVision_->visible()
        || profileManagerVisible_ || !activeTapPoints_.isEmpty()
        || !std::isfinite(normalizedX) || !std::isfinite(normalizedY)) return;
    autoGeometryCharacterSearch_ = true;
    autoGeometrySearchAnchor_ = characterCenter_.enabled
        ? QPointF(characterCenter_.x, characterCenter_.y) : QPointF(.5, .5);
    startAutoGeometryCapture(-2, {std::clamp(normalizedX, 0.0, 1.0),
                                  std::clamp(normalizedY, 0.0, 1.0)});
}

void IntegratedView::startAutoGeometryCapture(int index, const QPointF &downPoint)
{
    autoGeometrySkillIndex_ = index;
    autoGeometryFrames_ = 0;
    autoGeometryBefore_ = {};
    autoGeometrySnapshots_.clear();
    autoGeometryPrevious_ = {};
    autoGeometryDownPoint_ = downPoint;
    autoGeometryStatus_ = QStringLiteral("Снимок до нажатия…");
    const int generation = ++autoGeometryGeneration_;
    emit autoGeometryChanged();
    // Let the settings popup and the physical button release finish first.
    QTimer::singleShot(350, this, [this, generation] {
        if (autoGeometryActive() && generation == autoGeometryGeneration_)
            requestAutoGeometryFrame();
    });
    QTimer::singleShot(7000, this, [this, generation] {
        if (autoGeometryActive() && generation == autoGeometryGeneration_
            && (autoGeometryCharacterSearch_ || autoGeometrySnapshots_.size() < 3))
            finishAutoGeometry(
                QStringLiteral("Указатель не подтверждён. Настройки не изменены."));
    });
}

void IntegratedView::requestAutoGeometryFrame()
{
    if (!autoGeometryActive())
        return;
    const int id = ++autoGeometryFrameId_;
    autoGeometryCapturePending_ = true;
    emit autoGeometryFrameRequested(id);
    QTimer::singleShot(1200, this, [this, id] {
        if (autoGeometryActive() && id == autoGeometryFrameId_ && autoGeometryCapturePending_)
            reportAutoGeometryFrameFailure(id, QStringLiteral("Нет кадра от Android"));
    });
}

void IntegratedView::reportAutoGeometryFrameFailure(int id, const QString& reason)
{
    if (autoGeometryActive() && autoGeometryCapturePending_ && id == autoGeometryFrameId_)
        finishAutoGeometry(QStringLiteral("Не удалось получить кадр игры: ") + reason);
}

void IntegratedView::submitAutoGeometryFrame(int id, const QImage& image)
{
    if (!autoGeometryActive() || !autoGeometryCapturePending_ || id != autoGeometryFrameId_)
        return;
    autoGeometryCapturePending_ = false;
    if (image.isNull() || image.width() < 48 || image.height() < 48) {
        finishAutoGeometry(QStringLiteral("Пустой кадр игры. Геометрия не изменена."));
        return;
    }
    const int generation = autoGeometryGeneration_;
    if (autoGeometryBefore_.isNull()) {
        autoGeometryBefore_ = image;
        const int touch = allocateTouchId();
        if (touch < 0 || !sendTouchPoint(touch, autoGeometryDownPoint_, Qt::TouchPointPressed)) {
            finishAutoGeometry(QStringLiteral("Не удалось удержать палец на кнопке."));
            return;
        }
        autoGeometryTouchId_ = touch;
        trackTouch(touch, autoGeometryDownPoint_);
        autoGeometryStatus_ = autoGeometryCharacterSearch_
            ? QStringLiteral("Палец удерживается · поиск центра Урануса…")
            : QStringLiteral("Палец удерживается · поиск рядом с кнопкой…");
        emit autoGeometryChanged();
        QTimer::singleShot(450, this, [this, generation] {
            if (autoGeometryActive() && generation == autoGeometryGeneration_)
                requestAutoGeometryFrame();
        });
        return;
    }
    if (!autoGeometryCharacterSearch_) {
        autoGeometrySnapshots_.push_back(image);
        if (autoGeometrySnapshots_.size() < 3) {
            autoGeometryStatus_ = QStringLiteral("Палец неподвижен · снимок %1/3…")
                .arg(autoGeometrySnapshots_.size());
            emit autoGeometryChanged();
            QTimer::singleShot(180, this, [this, generation] {
                if (autoGeometryActive() && generation == autoGeometryGeneration_)
                    requestAutoGeometryFrame();
            });
            return;
        }
        // The analysis uses immutable native-resolution photographs. Release
        // before any expensive search: calculation time cannot prolong the cast.
        releaseAutoGeometryTouch();
        autoGeometryStatus_ = QStringLiteral("Снимки сохранены, палец отпущен · точное измерение центра…");
        emit autoGeometryChanged();
        const auto frames = autoGeometrySnapshots_;
        const QImage before = autoGeometryBefore_;
        const QPointF anchor(autoGeometryDownPoint_.x()*image.width(),
                             autoGeometryDownPoint_.y()*image.height());
        auto *watcher = new QFutureWatcher<SkillCircleResult>(this);
        connect(watcher, &QFutureWatcher<SkillCircleResult>::finished, this,
            [this, watcher, generation, image] {
                const auto result = watcher->result();watcher->deleteLater();
                if (!autoGeometryActive() || generation != autoGeometryGeneration_) return;
                if (!result.found) {
                    finishAutoGeometry(QStringLiteral(
                        "Центр не подтверждён: нужны неподвижный малый круг и внешний обод. Геометрия не изменена."));
                    return;
                }
                log(QString("Precise AutoGeometry native center=%1,%2 measuredRadius=%3 thumbRadius=%4 rimRms=%5 coverage=%6")
                    .arg(result.center.x(),0,'f',4).arg(result.center.y(),0,'f',4)
                    .arg(result.radius,0,'f',4).arg(result.thumbRadius,0,'f',4)
                    .arg(result.centerResidual,0,'f',4).arg(result.confidence,0,'f',3));
                auto normalized=result;
                // Image samples are indexed at pixel centres (0, 1, ...),
                // whereas touch/QML coordinates start at the pixel's edge.
                const QPointF frameCenter=result.center+QPointF(.5,.5);
                normalized.center={frameCenter.x()/image.width(),frameCenter.y()/image.height()};
                normalized.radius=result.radius*1.05/std::min(image.width(),image.height());
                if(normalized.radius<24.0/std::max(1,std::min(androidWidth_,androidHeight_))
                    || normalized.radius>.35) {
                    finishAutoGeometry(QStringLiteral("Размер вне допустимых границ. Геометрия не изменена."));
                    return;
                }
                finishAutoGeometry(QStringLiteral(
                    "Центр подтверждён по малому кругу · диаметр с запасом 5%: %1 пкс. Ctrl+Z — отменить.")
                        .arg(qRound(normalized.radius*2*std::min(androidWidth_,androidHeight_))),normalized);
            });
        watcher->setFuture(QtConcurrent::run([frames,before,anchor] {
            std::vector<SkillCircleResult> results;
            for (const auto &frame:frames) {
                auto found=detectStationarySkillCircle(frame,before,anchor,false);
                if(!found.found) found=detectStationarySkillCircle(frame,before,anchor,true);
                results.push_back(found);
            }
            // Centre is the priority. Radius can grow during a hero animation;
            // do not confuse that growth with an unstable joystick origin.
            SkillCircleResult best;
            size_t count=0;
            for(const auto &seed:results) {
                if(!seed.found) continue;
                SkillCircleResult mean;size_t n=0;
                std::vector<QPointF> agreed;
                for(const auto &r:results) {
                    if(!r.found || QLineF(seed.center,r.center).length()>1.25) continue;
                    if(std::any_of(agreed.begin(),agreed.end(),[&](QPointF c) {
                        return QLineF(c,r.center).length()>1.25;
                    })) continue;
                    mean.center+=r.center;mean.radius=std::max(mean.radius,r.radius);
                    mean.thumbRadius+=r.thumbRadius;mean.centerResidual+=r.centerResidual;
                    mean.confidence+=r.confidence;agreed.push_back(r.center);++n;
                }
                if(n>=2 && n>count) {
                    mean.found=true;mean.center/=double(n);mean.thumbRadius/=n;
                    mean.centerResidual/=n;mean.confidence/=n;best=mean;count=n;
                }
            }
            return best;
        }));
        QTimer::singleShot(60000,this,[this,generation] {
            if(autoGeometryActive() && generation==autoGeometryGeneration_)
                finishAutoGeometry(QStringLiteral("Точное измерение не завершилось. Геометрия не изменена."));
        });
        return;
    }
    ++autoGeometryFrames_;
    const bool full = autoGeometryFrames_ >= 3;
    if (full) {
        autoGeometryStatus_ = QStringLiteral("Палец удерживается · поиск по всему экрану…");
        emit autoGeometryChanged();
    }
    const bool characterSearch = autoGeometryCharacterSearch_;
    const QPointF near = characterSearch ? autoGeometrySearchAnchor_ : autoGeometryDownPoint_;
    const QPointF anchor(near.x() * image.width(), near.y() * image.height());
    const QImage before = autoGeometryBefore_;
    auto* watcher = new QFutureWatcher<SkillCircleResult>(this);
    connect(watcher, &QFutureWatcher<SkillCircleResult>::finished, this,
        [this, watcher, generation, image, characterSearch] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!autoGeometryActive() || generation != autoGeometryGeneration_)
                return;
            // Convert frame coordinates to normalized Android coordinates before
            // comparing separate frames or applying the result.
            SkillCircleResult normalized = result;
            normalized.center
                = QPointF(result.center.x() / image.width(), result.center.y() / image.height());
            normalized.radius = result.radius / std::min(image.width(), image.height());
            const double minimumRadius
                = 24.0 / std::max(1, std::min(androidWidth_, androidHeight_));
            if (!characterSearch && (normalized.radius < minimumRadius || normalized.radius > .35))
                normalized.found = false;
            const QPointF delta = normalized.center - autoGeometryPrevious_.center;
            const bool stable = characterSearch
                ? std::hypot(delta.x()*image.width(), delta.y()*image.height()) < 2.5
                : QLineF(normalized.center, autoGeometryPrevious_.center).length() < .008
                  && std::abs(normalized.radius - autoGeometryPrevious_.radius) < .006;
            if (normalized.found && autoGeometryPrevious_.found && stable) {
                normalized.center = (normalized.center + autoGeometryPrevious_.center) * .5;
                normalized.radius = (normalized.radius + autoGeometryPrevious_.radius) * .5;
                finishAutoGeometry(
                    characterSearch
                        ? QStringLiteral("Автопоиск Урануса: центр установлен. Ctrl+Z — отменить.")
                        : QStringLiteral("АвтоГеометрия: центр и диаметр найдены. Ctrl+Z — отменить."),
                    normalized);
                return;
            }
            autoGeometryPrevious_ = normalized;
            if (autoGeometryFrames_ >= 6) {
                finishAutoGeometry(
                    QStringLiteral("Указатель не подтверждён на двух кадрах. Настройки не изменены."));
                return;
            }
            QTimer::singleShot(180, this, [this, generation] {
                if (autoGeometryActive() && generation == autoGeometryGeneration_)
                    requestAutoGeometryFrame();
            });
        });
    watcher->setFuture(QtConcurrent::run(
        [image, before, anchor, full, characterSearch] {
            return characterSearch ? detectUranusCenter(image, before, anchor, full)
                                   : detectSkillCircle(image, before, anchor, full); }));
}

void IntegratedView::finishAutoGeometry(const QString& message, const SkillCircleResult& result)
{
    if (!autoGeometryActive())
        return;
    const int index = autoGeometrySkillIndex_;
    const bool characterSearch = autoGeometryCharacterSearch_;
    ++autoGeometryGeneration_;
    releaseAutoGeometryTouch();
    autoGeometrySnapshots_.clear();
    autoGeometryCapturePending_ = false;
    autoGeometrySkillIndex_ = -1;
    autoGeometryBefore_ = { };
    autoGeometryCharacterSearch_ = false;
    const bool apply = result.found && editMode_ && (characterSearch
        || (index >= 0 && index < static_cast<int>(mobaSkills_.size())));
    if (apply && characterSearch) {
        if (characterCenter_.enabled) moveCharacterCenter(result.center.x(), result.center.y());
        else addCharacterCenterAt(result.center.x(), result.center.y());
    } else if (apply) {
        recordMapperUndo();
        auto& skill = mobaSkills_[index];
        markMobaSkillCalibrationStale(
            skill, QStringLiteral("Геометрия изменена — проверьте калибровку."));
        skill.x = std::clamp(result.center.x(), 0.0, 1.0);
        skill.y = std::clamp(result.center.y(), 0.0, 1.0);
        skill.radius = result.radius;
        log(QString("AutoGeometry confirmed center=%1,%2 radius=%3 confidence=%4")
                .arg(skill.x)
                .arg(skill.y)
                .arg(skill.radius)
                .arg(result.confidence));
        // The artificial touch-down point stays where the user placed it.
        emit mobaSkillsChanged();
        emit selectedMobaSkillChanged();
    }
    autoGeometryStatus_ = message;
    setEditorMessage(message);
    emit autoGeometryChanged();
    emit autoGeometryFinished(index, apply);
    log(QString("AutoGeometry index=%1 applied=%2: %3").arg(index).arg(apply).arg(message));
}

void IntegratedView::releaseAutoGeometryTouch()
{
    const int index = autoGeometrySkillIndex_;
    const bool characterSearch = autoGeometryCharacterSearch_;
    if (autoGeometryTouchId_ >= 0) {
        QPointF up = autoGeometryDownPoint_;
        // Use the game's configured cancellation target when possible.
        if (skillCancel_.enabled && (characterSearch
            || (index >= 0 && index < static_cast<int>(mobaSkills_.size()) && mobaSkills_[index].cancellable))) {
            up = QPointF(skillCancel_.x, skillCancel_.y);
            sendTouchPoint(autoGeometryTouchId_, up, Qt::TouchPointMoved);
        }
        sendTouchPoint(autoGeometryTouchId_, up, Qt::TouchPointReleased);
        forgetTouch(autoGeometryTouchId_);
    }
    autoGeometryTouchId_ = -1;
}

void IntegratedView::cancelAutoGeometry()
{
    finishAutoGeometry(QStringLiteral("Поиск отменён. Настройки не изменены."));
}
