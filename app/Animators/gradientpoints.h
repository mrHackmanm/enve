#ifndef GRADIENTPOINTS_H
#define GRADIENTPOINTS_H
#include "Animators/complexanimator.h"
class GradientPoint;
class MovablePoint;
class PathBox;
class QPointFAnimator;
#include "skia/skiaincludes.h"

class GradientPoints : public ComplexAnimator {
    friend class SelfRef;
public:
    void enable();

    void disable();

    void drawGradientPointsSk(SkCanvas *canvas,
                              const SkScalar &invScale);

    MovablePoint *qra_getPointAt(const QPointF &absPos,
                                 const qreal &canvasScaleInv);

    void setColors(const QColor &startColor, const QColor &endColor);

    void setPositions(const QPointF &startPos,
                      const QPointF &endPos);

    QPointF getStartPointAtRelFrame(const int &relFrame);
    QPointF getEndPointAtRelFrame(const int &relFrame);
    QPointF getStartPointAtRelFrameF(const qreal &relFrame);
    QPointF getEndPointAtRelFrameF(const qreal &relFrame);
    void writeProperty(QIODevice * const target) const;
    void readProperty(QIODevice *target);

    bool enabled() const {
        return mEnabled;
    }
protected:
    GradientPoints(PathBox *parentT);

    bool mEnabled;

    qsptr<QPointFAnimator> mStartAnimator;
    qsptr<QPointFAnimator> mEndAnimator;

    stdsptr<GradientPoint> mStartPoint;
    stdsptr<GradientPoint> mEndPoint;

    PathBox* const mParent_k;
};

#endif // GRADIENTPOINTS_H