// enve - 2D animations software
// Copyright (C) 2016-2020 Maurycy Liebner

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "qrealanimatorvalueslider.h"
#include <QMenu>
#include "Animators/qrealanimator.h"
#include "mainwindow.h"

QrealAnimatorValueSlider::QrealAnimatorValueSlider(qreal minVal, qreal maxVal,
                                                   qreal prefferedStep,
                                                   QWidget *parent) :
    QDoubleSlider(minVal, maxVal, prefferedStep, parent) {

}

QrealAnimatorValueSlider::QrealAnimatorValueSlider(qreal minVal, qreal maxVal,
                                                   qreal prefferedStep,
                                                   QrealAnimator *animator,
                                                   QWidget *parent) :
    QDoubleSlider(minVal, maxVal, prefferedStep, parent) {
    setTarget(animator);
}

QrealAnimatorValueSlider::QrealAnimatorValueSlider(QrealAnimator *animator,
                                                   QWidget *parent) :
    QDoubleSlider(parent) {
    setTarget(animator);
}

QrealAnimatorValueSlider::QrealAnimatorValueSlider(QWidget *parent) :
    QrealAnimatorValueSlider(nullptr, parent) {

}

QrealAnimatorValueSlider::QrealAnimatorValueSlider(QString name,
                                                   qreal minVal, qreal maxVal,
                                                   qreal prefferedStep,
                                                   QWidget *parent) :
    QDoubleSlider(name, minVal, maxVal, prefferedStep, parent) {

}

void QrealAnimatorValueSlider::emitEditingStarted(qreal value) {
    if(mTarget) {
        mBlockAnimatorSignals = true;
        mTarget->prp_startTransform();
    }
    QDoubleSlider::emitEditingStarted(value);
}

void QrealAnimatorValueSlider::emitValueChangedExternal(qreal value) {
    setDisplayedValue(value);
    emitValueChanged(value);
}

#include "Animators/qpointfanimator.h"
QrealAnimator* QrealAnimatorValueSlider::getQPointFAnimatorSibling() {
    if(mTarget->SWT_isQrealAnimator()) {
        const auto parent = mTarget->getParent();
        if(parent && parent->SWT_isQPointFAnimator()) {
            const auto qPA = static_cast<QPointFAnimator*>(parent);
            const bool thisX = qPA->getXAnimator() == mTarget;
            return thisX ? qPA->getYAnimator() :
                           qPA->getXAnimator();
        }
    }
    return nullptr;
}

void QrealAnimatorValueSlider::mouseMoveEvent(QMouseEvent *event) {
    if(event->modifiers() & Qt::ShiftModifier) {
        const auto other = getQPointFAnimatorSibling();
        if(!other) return;
        if(!mMouseMoved) other->prp_startTransform();
        const qreal dValue = (event->x() - mPressX)*0.1*mPrefferedValueStep;
        other->incCurrentBaseValue(dValue);
    }
    QDoubleSlider::mouseMoveEvent(event);
}

bool QrealAnimatorValueSlider::eventFilter(QObject *obj, QEvent *event) {
    const bool keyPress = event->type() == QEvent::KeyPress;
    const bool keyRelease = event->type() == QEvent::KeyRelease;

    if(keyPress || keyRelease) {
        const auto keyEvent = static_cast<QKeyEvent*>(event);
        if(keyEvent->key() == Qt::Key_Shift) {
            const auto other = getQPointFAnimatorSibling();
            if(other) {
                if(keyPress) {
                    if(mMouseMoved) {
                        other->prp_startTransform();
                    }
                } else if(keyRelease) {
                    other->prp_cancelTransform();
                }
            }
        }
    }
    return QDoubleSlider::eventFilter(obj, event);
}

void QrealAnimatorValueSlider::emitValueChanged(qreal value) {
    if(mTarget) {
        if(mTarget->SWT_isQrealAnimator()) {
            const auto da = static_cast<QrealAnimator*>(*mTarget);
            da->setCurrentBaseValue(value);
        }
    }
    QDoubleSlider::emitValueChanged(value);
}

void QrealAnimatorValueSlider::setValueExternal(qreal value) {
    if(mTarget) {
        mBlockAnimatorSignals = true;
        if(mTarget->SWT_isQrealAnimator()) {
            const auto da = static_cast<QrealAnimator*>(*mTarget);
            da->setCurrentBaseValue(value);
        }
        mBlockAnimatorSignals = false;
    }
    setDisplayedValue(value);
}

void QrealAnimatorValueSlider::emitEditingFinished(qreal value) {
    if(mTarget) {
        mTarget->prp_finishTransform();
        mBlockAnimatorSignals = false;
        const auto other = getQPointFAnimatorSibling();
        if(other) other->prp_finishTransform();
    }
    QDoubleSlider::emitEditingFinished(value);
}

void QrealAnimatorValueSlider::emitEditingCanceled() {
    if(mTarget) {
        mBlockAnimatorSignals = false;
        mTarget->prp_cancelTransform();
        const auto other = getQPointFAnimatorSibling();
        if(other) other->prp_cancelTransform();
    }
    QDoubleSlider::emitEditingCanceled();
}

void QrealAnimatorValueSlider::setValueFromAnimator(qreal val) {
    if(mBlockAnimatorSignals) return;
    setDisplayedValue(val);
    emit displayedValueChanged(val);
}

void QrealAnimatorValueSlider::paint(QPainter *p) {
    if(!mTarget) {
        QDoubleSlider::paint(p);
    } else {
        bool rec = false;
        bool key = false;
        if(mTarget->SWT_isAnimator()) {
            const auto aTarget = static_cast<Animator*>(*mTarget);
            rec = aTarget->anim_isRecording();
            key = aTarget->anim_getKeyOnCurrentFrame();
        }
        if(rec) {
            const bool disabled = isTargetDisabled() || !isEnabled();
            QDoubleSlider::paint(p,
                                 disabled ? QColor(200, 180, 180) :
                                            QColor(255, 200, 200),
                                 disabled ? QColor(180, 160, 160) :
                                            QColor(255, 160, 160),
                                 key ? (disabled ? QColor(200, 90, 90) : Qt::red) :
                                       (disabled ? Qt::darkGray : Qt::black),
                                 disabled ? Qt::darkGray : Qt::black);
        } else {
            QDoubleSlider::paint(p, !isTargetDisabled() && isEnabled());
        }
    }
}

void QrealAnimatorValueSlider::setTarget(QrealAnimator * const animator) {
    if(animator == mTarget) return;
    auto& conn = mTarget.assign(animator);
    if(mTarget) {
        setNumberDecimals(animator->getNumberDecimals());
        conn << connect(animator, &QrealAnimator::valueChanged,
                        this, &QrealAnimatorValueSlider::setValueFromAnimator);
        conn << connect(animator, &QrealAnimator::anim_changedKeyOnCurrentFrame,
                        this, qOverload<>(&QrealAnimatorValueSlider::update));

        setValueRange(animator->getMinPossibleValue(),
                      animator->getMaxPossibleValue());
        setPrefferedValueStep(animator->getPrefferedValueStep());

        setDisplayedValue(animator->getCurrentBaseValue());
    }
}

bool QrealAnimatorValueSlider::hasTarget() {
    return mTarget;
}

bool QrealAnimatorValueSlider::isTargetDisabled() {
    if(hasTarget()) return mTarget->SWT_isDisabled();
    return true;
}

void QrealAnimatorValueSlider::openContextMenu(
        const QPoint &globalPos) {
    if(!mTarget) return;
    if(!mTarget->SWT_isAnimator()) return;
    const auto aTarget = static_cast<Animator*>(*mTarget);
    QMenu menu(this);

    if(aTarget->anim_getKeyOnCurrentFrame()) {
        menu.addAction("Delete Key", aTarget,
                       &Animator::anim_deleteCurrentKeyAction);
    } else {
        menu.addAction("Add Key", aTarget,
                       &Animator::anim_saveCurrentValueAsKey);
    }

    menu.addSeparator();

    QAction * const recAct = menu.addAction("Recording");
    recAct->setCheckable(true);
    recAct->setChecked(aTarget->anim_isRecording());
    connect(recAct, &QAction::triggered,
            aTarget, &Animator::anim_setRecording);

    QAction * const selectedAction = menu.exec(globalPos);
    if(!selectedAction) return;
    else Document::sInstance->actionFinished();
}
