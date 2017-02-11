﻿#ifndef SINGLEWIDGETABSTRACTION_H
#define SINGLEWIDGETABSTRACTION_H

#include <QWidget>
class SingleWidgetTarget;
class SingleWidget;
class ScrollWidgetVisiblePart;

enum SWT_Rule : short;

class SingleWidgetAbstraction {
public:
    SingleWidgetAbstraction(SingleWidgetTarget *target,
                            ScrollWidgetVisiblePart *visiblePart);
    ~SingleWidgetAbstraction();

    bool setSingleWidgetAbstractions(const int &minY, const int &maxY,
                                     int currY, int currX,
                                     QList<SingleWidget*> *widgets,
                                     int *currentWidgetId,
                                     const SWT_Rule &rule,
                                     const bool &parentSatisfiesRule);

    int getHeight(const SWT_Rule &rule,
                  const bool &parentSatisfiesRule);

    void setContentVisible(const bool &bT);

    SingleWidgetTarget *getTarget() {
        return mTarget;
    }

    void addChildAbstractionForTarget(SingleWidgetTarget *target);
    void addChildAbstractionForTargetAt(
            SingleWidgetTarget *target,
            const int &id);
    void addChildAbstraction(SingleWidgetAbstraction *abs);
    void addChildAbstractionAt(SingleWidgetAbstraction *abs,
                             const int &id);

    void removeChildAbstractionForTarget(SingleWidgetTarget *target);
    void removeChildAbstraction(SingleWidgetAbstraction *abs);

    void switchContentVisible();

    bool contentVisible();

    bool isDeletable() {
        return mDeletable;
    }

    void setDeletable(const bool &bT) {
        mDeletable = bT;
    }

    ScrollWidgetVisiblePart *getParentVisiblePartWidget() {
        return mVisiblePartWidget;
    }

    void scheduleWidgetContentUpdateIfIsCurrentRule(
            const SWT_Rule &rule);

private:
    ScrollWidgetVisiblePart *mVisiblePartWidget;

    bool mDeletable = true;
    bool mContentVisible = false;
    SingleWidgetTarget *mTarget;

    QList<SingleWidgetAbstraction*> mChildren;
};

#endif // SINGLEWIDGETABSTRACTION_H
