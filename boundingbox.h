#ifndef CHILDPARENT_H
#define CHILDPARENT_H
#include <QMatrix>
#include "transformable.h"
#include "fillstrokesettings.h"
#include <QSqlQuery>
#include "transformanimator.h"

#include "animatorscollection.h"

class KeysView;

class UndoRedo;

class Canvas;

class UndoRedoStack;

class MovablePoint;

class PathPoint;

enum CanvasMode : short;

enum BoundingBoxType {
    TYPE_VECTOR_PATH,
    TYPE_CIRCLE,
    TYPE_IMAGE,
    TYPE_RECTANGLE,
    TYPE_TEXT,
    TYPE_GROUP,
    TYPE_CANVAS
};

class BoxesGroup;

class BoxesList;

class CtrlPoint;

class Edge;

class VectorPath;

class BoundingBox : public Transformable
{
public:
    BoundingBox(BoxesGroup *parent, BoundingBoxType type);
    BoundingBox(BoundingBoxType type);

    virtual void setFont(QFont) {}
    virtual void setFontSize(qreal) {}
    virtual void setFontFamilyAndStyle(QString,
                                       QString) {}

    virtual void centerPivotPosition() {}
    virtual bool isContainedIn(QRectF absRect);
    virtual QRectF getBoundingRect() { return QRectF(); }

    virtual void drawPixmap(QPainter *p);
    virtual void render(QPainter *) {}
    virtual void draw(QPainter *) {}
    virtual void drawSelected(QPainter *, CanvasMode) {}

    QMatrix getCombinedTransform();

    void applyTransformation(TransformAnimator *transAnimator);

    void scale(qreal scaleBy, QPointF absOrigin);
    void scale(qreal scaleXBy, qreal scaleYBy, QPointF absOrigin);

    void rotateBy(qreal rot, QPointF absOrigin);

    QPointF getAbsolutePos();

    void updateCombinedTransform();
    void moveBy(QPointF trans);

    void startTransform();
    void finishTransform();


    virtual bool pointInsidePath(QPointF) { return false; }
    virtual MovablePoint *getPointAt(QPointF, CanvasMode) { return NULL; }

    void moveUp();
    void moveDown();
    void bringToFront();
    void bringToEnd();

    void setZListIndex(int z, bool saveUndoRedo = true);

    virtual void selectAndAddContainedPointsToList
                            (QRectF,QList<MovablePoint*> *) {}

    QPointF getPivotAbsPos();
    virtual void select();
    void deselect();
    int getZIndex();
    virtual void drawBoundingRect(QPainter *p);
    void setParent(BoxesGroup *parent, bool saveUndoRedo = true);
    BoxesGroup *getParent();

    bool isGroup();
    virtual BoundingBox *getPathAtFromAllAncestors(QPointF absPos);

    virtual void setStrokeSettings(StrokeSettings, bool saveUndoRedo = true) { Q_UNUSED(saveUndoRedo) }
    virtual void setFillSettings(PaintSettings, bool saveUndoRedo = true) { Q_UNUSED(saveUndoRedo) }

    virtual const PaintSettings *getFillSettings();
    virtual const StrokeSettings *getStrokeSettings();

    virtual qreal getCurrentCanvasScale();

    void setPivotAbsPos(QPointF absPos, bool saveUndoRedo = true, bool pivotChanged = true);

    void setPivotRelPos(QPointF relPos, bool saveUndoRedo = true, bool pivotChanged = true);

    void cancelTransform();
    void scale(qreal scaleXBy, qreal scaleYBy);

    virtual int saveToSql(int parentId);

    virtual PathPoint *createNewPointOnLineNear(QPointF, bool) { return NULL; }
    bool isPath();
    void saveTransformPivot(QPointF absPivot);

    //

    virtual void drawListItem(QPainter *p,
                              qreal drawX, qreal drawY,
                              qreal maxY);
    virtual qreal getListItemHeight();
    void showChildrenListItems();
    void hideChildrenListItems();
    void setName(QString name);
    QString getName();

    void hide();
    void show();
    bool isVisible();
    void setVisibile(bool visible, bool saveUndoRedo = true);
    virtual void handleListItemMousePress(qreal boxesListX, qreal relX, qreal relY,
                                          QMouseEvent *event);
    void setChildrenListItemsVisible(bool bt);
    void lock();
    void unlock();
    void setLocked(bool bt);
    bool isLocked();
    bool isVisibleAndUnlocked();
    void rotateBy(qreal rot);
    void scale(qreal scaleBy);

    void rotateRelativeToSavedPivot(qreal rot);
    void scaleRelativeToSavedPivot(qreal scaleBy);

    virtual void startPosTransform();
    virtual void startRotTransform();
    virtual void startScaleTransform();
    virtual void drawAnimationBar(QPainter *p, qreal drawX, qreal drawY);
    virtual void updateAfterFrameChanged(int currentFrame);
    virtual QMatrix getCombinedRenderTransform();
    virtual QrealKey *getKeyAtPos(qreal relX, qreal relY, qreal);
    virtual void getKeysInRect(QRectF selectionRect,
                               QList<QrealKey *> *keysList);

    virtual void startAllPointsTransform() {}
    virtual void finishAllPointsTransform() {}

    void addActiveAnimator(QrealAnimator *animator);
    void removeActiveAnimator(QrealAnimator *animator);

    virtual void setFillGradient(Gradient* gradient, bool finish) { Q_UNUSED(gradient); Q_UNUSED(finish); }
    virtual void setStrokeGradient(Gradient* gradient, bool finish) { Q_UNUSED(gradient); Q_UNUSED(finish); }
    virtual void setFillFlatColor(Color color, bool finish) { Q_UNUSED(color); Q_UNUSED(finish); }
    virtual void setStrokeFlatColor(Color color, bool finish) { Q_UNUSED(color); Q_UNUSED(finish); }
    virtual void setFillPaintType(PaintType paintType, Color color,
                                  Gradient* gradient) { Q_UNUSED(paintType); Q_UNUSED(color); Q_UNUSED(gradient); }
    virtual void setStrokePaintType(PaintType paintType, Color color,
                                    Gradient* gradient) { Q_UNUSED(paintType); Q_UNUSED(color); Q_UNUSED(gradient); }
    virtual void setStrokeCapStyle(Qt::PenCapStyle capStyle) { Q_UNUSED(capStyle); }
    virtual void setStrokeJoinStyle(Qt::PenJoinStyle joinStyle) { Q_UNUSED(joinStyle); }
    virtual void setStrokeWidth(qreal strokeWidth, bool finish) { Q_UNUSED(strokeWidth); Q_UNUSED(finish); }

    virtual void startStrokeWidthTransform() {}
    virtual void startStrokeColorTransform() {}
    virtual void startFillColorTransform() {}

    virtual Edge *getEgde(QPointF absPos) {
        Q_UNUSED(absPos);
        return NULL;
    }
    void setAbsolutePos(QPointF pos, bool saveUndoRedo);
    void setRelativePos(QPointF relPos, bool saveUndoRedo);
    virtual void setRenderCombinedTransform();

    virtual void showContextMenu(QPoint globalPos) { Q_UNUSED(globalPos); }
    virtual void drawKeysView(QPainter *p, qreal drawY, qreal maxY,
                              qreal pixelsPerFrame, int startFrame, int endFrame);
    virtual void drawKeys(QPainter *p, qreal pixelsPerFrame,
                          qreal drawY, int startFrame, int endFrame);
    void scaleRelativeToSavedPivot(qreal scaleXBy, qreal scaleYBy);
    void resetScale();
    void resetTranslation();
    void resetRotation();
    bool isCircle();
    bool isRect();
    bool isText();
    TransformAnimator *getTransformAnimator();
    void disablePivotAutoAdjust();
    void enablePivotAutoAdjust();
    void copyTransformationTo(BoundingBox *box);

    virtual VectorPath *objectToPath() { return NULL; }
    virtual void loadFromSql(int boundingBoxId);

    virtual void updatePrettyPixmap();
    void setAwaitingUpdate(bool bT);
    void awaitUpdate();
    QRectF getBoundingRectClippedToView();
    void saveOldPixmap();

    void saveUglyPaintTransform();
    void drawAsBoundingRect(QPainter *p, QRectF rect);
    virtual void updateUpdateTransform();
    void updateUglyPaintTransform();
    void redoUpdate();
    bool shouldRedoUpdate();
    void setRedoUpdateToFalse();

    virtual void afterSuccessfulUpdate() {}

    void updateAllLowQualityPixmap();
    void updateRelativeTransform();
protected:
    virtual void updateAfterCombinedTransformationChanged() {}

    QMatrix mRelativeTransformMatrix;

    QMatrix mUpdateTransform;
    QMatrix mOldTransform;
    QPixmap mNewPixmap;
    QPixmap mOldPixmap;
    QPixmap mAllLowQualityPixmap;
    QRectF mOldPixBoundingRect;

    bool mRedoUpdate = false;
    bool mAwaitingUpdate = false;
    QMatrix mUglyPaintTransform;

    QRectF mBoundingRect;
    QRectF mBoundingRectClippedToView;

    BoxesList *mBoxesList;
    KeysView *mKeysView;

    bool mScheduledForRemove = false;
    BoundingBoxType mType;
    BoxesGroup *mParent = NULL;

    AnimatorsCollection mAnimatorsCollection;

    TransformAnimator mTransformAnimator;

    QMatrix mCombinedTransformMatrix;
    int mZListIndex = 0;
    bool mPivotChanged = false;

    bool mVisible = true;
    bool mLocked = false;
    //

    bool mBoxListItemDetailsVisible = false;
    QString mName = "";
    QList<QrealAnimator*> mActiveAnimators;
};


#endif // CHILDPARENT_H
