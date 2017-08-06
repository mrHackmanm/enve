#include "paintbox.h"
#include "Paint/layer.h"

PaintBox::PaintBox(const ushort &canvasWidthT,
                   const ushort &canvasHeightT) : BoundingBox(TYPE_PAINT),
    CanvasHandler(canvasWidthT, canvasHeightT) {

}

void PaintBox::drawPixmapSk(SkCanvas *canvas, SkPaint *paint) {
    canvas->saveLayer(NULL, paint);
    BoundingBox::drawPixmapSk(canvas, NULL);
    mTemporaryOverlayCont.drawSk(canvas, NULL);
    canvas->restore();
}

void PaintBox::renderDataFinished(BoundingBoxRenderData *renderData) {
    BoundingBox::renderDataFinished(renderData);
    //PaintBoxRenderData *paintData = (PaintBoxRenderData*)renderData;
//    SkPixmap pixm;
//    mTemporaryOverlayCont.getImageSk()->peekPixels(&pixm);
//    pixm.erase(SK_ColorTRANSPARENT);
}

void PaintBox::setupBoundingBoxRenderDataForRelFrame(
        const int &relFrame, BoundingBoxRenderData *data) {
    BoundingBox::setupBoundingBoxRenderDataForRelFrame(relFrame, data);
    PaintBoxRenderData *paintData = (PaintBoxRenderData*)data;
    getTileDrawers(&paintData->tileDrawers);
    foreach(TileSkDrawer *drawer, paintData->tileDrawers) {
        if(!drawer->finished()) {
            drawer->addDependent(paintData);
        }
    }
}

void PaintBox::updateDrawRenderContainerTransform() {
    BoundingBox::updateDrawRenderContainerTransform();
    mTemporaryOverlayCont.updatePaintTransformGivenNewCombinedTransform(
                mTransformAnimator->getCombinedTransform());
}

void PaintBox::mapToPaintCanvasHandler(qreal *x_t, qreal *y_t) {
    QPointF relPos = mapAbsPosToRel(QPointF(*x_t, *y_t));
    *x_t = relPos.x();
    *y_t = relPos.y();
}

BoundingBoxRenderData *PaintBox::createRenderData() {
    return new PaintBoxRenderData(this);
}

void PaintBoxRenderData::drawSk(SkCanvas *canvas) {
    SkPaint paint;
    //paint.setFilterQuality(kHigh_SkFilterQuality);
    foreach(TileSkDrawer *tile, tileDrawers) {
        tile->drawSk(canvas, &paint);
    }
}

void PaintBoxRenderData::updateRelBoundingRect() {
    int widthT = 0;
    int heightT = 0;
    foreach(TileSkDrawer *tile, tileDrawers) {
        widthT = qMax(widthT, tile->posX + TILE_DIM);
        heightT = qMax(heightT, tile->posY + TILE_DIM);
    }
    relBoundingRect = QRectF(0., 0.,
                             widthT, heightT);
}