﻿#include "svgimporter.h"
#include "Boxes/containerbox.h"
#include "canvas.h"
#include "GUI/ColorWidgets/colorvaluerect.h"
#include "colorhelpers.h"
#include "pointhelpers.h"
#include "Boxes/circle.h"
#include "Boxes/rectangle.h"
#include "Boxes/textbox.h"
#include "GUI/mainwindow.h"
#include "Animators/transformanimator.h"
#include "paintsettingsapplier.h"
#include "Boxes/smartvectorpath.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/SmartPath/smartpathanimator.h"
#include "MovablePoints/smartnodepoint.h"

struct SvgAttribute {
    SvgAttribute(const QString &nameValueStr) {
        const QStringList nameValueList = nameValueStr.split(":");
        fName = nameValueList.first();
        fValue = nameValueList.last();
    }

    QString getName() const {
        return fName;
    }

    QString getValue() const {
        return fValue;
    }

    QString fName;
    QString fValue;
};

void extractSvgAttributes(const QString &string,
                          QList<SvgAttribute> * const attributesList) {
    const QStringList attributesStrList = string.split(";");
    for(const QString &attributeStr : attributesStrList) {
        attributesList->append(SvgAttribute(attributeStr));
    }
}


bool toColor(const QString &colorStr, QColor &color) {
    QRegExp rx = QRegExp("rgb\\(.*\\)", Qt::CaseInsensitive);
    if(rx.exactMatch(colorStr)) {
        rx = QRegExp("rgb\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)", Qt::CaseInsensitive);
        if(rx.exactMatch(colorStr)) {
            rx.indexIn(colorStr);
            QStringList intRGB = rx.capturedTexts();
            color.setRgb(intRGB.at(1).toInt(),
                         intRGB.at(2).toInt(),
                         intRGB.at(3).toInt());
        } else {
            rx = QRegExp("rgb\\(\\s*(\\d+)\\s*%\\s*,\\s*(\\d+)\\s*%\\s*,\\s*(\\d+)\\s*%\\s*\\)", Qt::CaseInsensitive);
            rx.indexIn(colorStr);
            QStringList intRGB = rx.capturedTexts();
            color.setRgbF(intRGB.at(1).toInt()/100.,
                          intRGB.at(2).toInt()/100.,
                          intRGB.at(3).toInt()/100.);

        }
    } else {
        rx = QRegExp("#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})", Qt::CaseInsensitive);
        if(rx.exactMatch(colorStr)) {
            color = QColor(colorStr);
        } else {
            return false;
        }
    }
    return true;
}

// '0' is 0x30 and '9' is 0x39
static bool isDigit(ushort ch) {
    static quint16 magic = 0x3ff;
    return ((ch >> 4) == 3) && (magic >> (ch & 15));
}

static qreal toDouble(const QChar *&str) {
    const int maxLen = 255;//technically doubles can go til 308+ but whatever
    char temp[maxLen+1];
    int pos = 0;

    if(*str == QLatin1Char('-')) {
        temp[pos++] = '-';
        ++str;
    } else if(*str == QLatin1Char('+')) {
        ++str;
    }
    while(isDigit(str->unicode()) && pos < maxLen) {
        temp[pos++] = str->toLatin1();
        ++str;
    }
    if(*str == QLatin1Char('.') && pos < maxLen) {
        temp[pos++] = '.';
        ++str;
    }
    while(isDigit(str->unicode()) && pos < maxLen) {
        temp[pos++] = str->toLatin1();
        ++str;
    }
    bool exponent = false;
    if((*str == QLatin1Char('e') || *str == QLatin1Char('E')) && pos < maxLen) {
        exponent = true;
        temp[pos++] = 'e';
        ++str;
        if((*str == QLatin1Char('-') || *str == QLatin1Char('+')) && pos < maxLen) {
            temp[pos++] = str->toLatin1();
            ++str;
        }
        while(isDigit(str->unicode()) && pos < maxLen) {
            temp[pos++] = str->toLatin1();
            ++str;
        }
    }

    temp[pos] = '\0';

    qreal val;
    if(!exponent && pos < 10) {
        int ival = 0;
        const char *t = temp;
        bool neg = false;
        if(*t == '-') {
            neg = true;
            ++t;
        }
        while(*t && *t != '.') {
            ival *= 10;
            ival += (*t) - '0';
            ++t;
        }
        if(*t == '.') {
            ++t;
            int div = 1;
            while(*t) {
                ival *= 10;
                ival += (*t) - '0';
                div *= 10;
                ++t;
            }
            val = static_cast<qreal>(ival)/div;
        } else {
            val = ival;
        }
        if(neg) val = -val;
    } else {
        val = QByteArray::fromRawData(temp, pos).toDouble();
    }
    return val;
}

static qreal toDouble(const QString &str, bool *ok = nullptr) {
    const QChar *c = str.constData();
    const qreal res = toDouble(c);
    if(ok) *ok = (*c == QLatin1Char('\0'));
    return res;
}

//static qreal toDouble(const QStringRef &str, bool *ok = nullptr) {
//    const QChar *c = str.constData();
//    qreal res = toDouble(c);
//    if(ok) {
//        *ok = (c == (str.constData() + str.length()));
//    }
//    return res;
//}

static void parseNumbersArray(const QChar *&str,
                              QVarLengthArray<qreal, 8> &points) {
    while (str->isSpace())
        ++str;
    while (isDigit(str->unicode()) ||
           *str == QLatin1Char('-') || *str == QLatin1Char('+') ||
           *str == QLatin1Char('.')) {

        points.append(toDouble(str));

        while (str->isSpace())
            ++str;
        if(*str == QLatin1Char(','))
            ++str;

        //eat the rest of space
        while (str->isSpace())
            ++str;
    }
}

bool parsePathDataFast(const QString &dataStr,
                       VectorPathSvgAttributes &attributes) {
    qreal x0 = 0, y0 = 0;              // starting point
    qreal x = 0, y = 0;                // current point
    char lastMode = 0;
    QPointF ctrlPt;
    const QChar *str = dataStr.constData();
    const QChar *end = str + dataStr.size();

    SvgSeparatePath *lastPath = nullptr;
    while (str != end) {
        while (str->isSpace())
            ++str;
        QChar pathElem = *str;
        ++str;
        QChar endc = *end;
        *const_cast<QChar *>(end) = 0; // parseNumbersArray requires 0-termination that QStringRef cannot guarantee
        QVarLengthArray<qreal, 8> arg;
        parseNumbersArray(str, arg);
        *const_cast<QChar *>(end) = endc;
        if(pathElem == QLatin1Char('z') || pathElem == QLatin1Char('Z'))
            arg.append(0);//dummy
        const qreal *num = arg.constData();
        int count = arg.count();
        while (count > 0) {
            qreal offsetX = x;        // correction offsets
            qreal offsetY = y;        // for relative commands
            switch (pathElem.unicode()) {
            case 'm': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = x0 = num[0] + offsetX;
                y = y0 = num[1] + offsetY;
                num += 2;
                count -= 2;
                lastPath = attributes.newSeparatePath();
                lastPath->moveTo(QPointF(x0, y0));

                 // As per 1.2  spec 8.3.2 The "moveto" commands
                 // If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
                 // the subsequent pairs shall be treated as implicit 'lineto' commands.
                 pathElem = QLatin1Char('l');
            }
                break;
            case 'M': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = x0 = num[0];
                y = y0 = num[1];
                num += 2;
                count -= 2;
                lastPath = attributes.newSeparatePath();
                lastPath->moveTo(QPointF(x0, y0));

                // As per 1.2  spec 8.3.2 The "moveto" commands
                // If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
                // the subsequent pairs shall be treated as implicit 'lineto' commands.
                pathElem = QLatin1Char('L');
            }
                break;
            case 'z':
            case 'Z': {
                x = x0;
                y = y0;
                count--; // skip dummy
                num++;
                lastPath->closePath();
            }
                break;
            case 'l': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = num[0] + offsetX;
                y = num[1] + offsetY;
                num += 2;
                count -= 2;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'L': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = num[0];
                y = num[1];
                num += 2;
                count -= 2;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'h': {
                x = num[0] + offsetX;
                num++;
                count--;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'H': {
                x = num[0];
                num++;
                count--;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'v': {
                y = num[0] + offsetY;
                num++;
                count--;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'V': {
                y = num[0];
                num++;
                count--;
                lastPath->lineTo(QPointF(x, y));
            }
                break;
            case 'c': {
                if(count < 6) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1(num[0] + offsetX, num[1] + offsetY);
                QPointF c2(num[2] + offsetX, num[3] + offsetY);
                QPointF e(num[4] + offsetX, num[5] + offsetY);
                num += 6;
                count -= 6;
                lastPath->cubicTo(c1, c2, e);

                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'C': {
                if(count < 6) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1(num[0], num[1]);
                QPointF c2(num[2], num[3]);
                QPointF e(num[4], num[5]);
                num += 6;
                count -= 6;
                lastPath->cubicTo(c1, c2, e);

                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 's': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1;
                if(lastMode == 'c' || lastMode == 'C' ||
                    lastMode == 's' || lastMode == 'S')
                    c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c1 = QPointF(x, y);
                QPointF c2(num[0] + offsetX, num[1] + offsetY);
                QPointF e(num[2] + offsetX, num[3] + offsetY);
                num += 4;
                count -= 4;
                lastPath->cubicTo(c1, c2, e);

                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'S': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1;
                if(lastMode == 'c' || lastMode == 'C' ||
                    lastMode == 's' || lastMode == 'S')
                    c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c1 = QPointF(x, y);
                QPointF c2(num[0], num[1]);
                QPointF e(num[2], num[3]);
                num += 4;
                count -= 4;
                lastPath->cubicTo(c1, c2, e);

                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'q': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c(num[0] + offsetX, num[1] + offsetY);
                QPointF e(num[2] + offsetX, num[3] + offsetY);
                num += 4;
                count -= 4;
                lastPath->quadTo(c, e);

                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'Q': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c(num[0], num[1]);
                QPointF e(num[2], num[3]);
                num += 4;
                count -= 4;
                lastPath->quadTo(c, e);

                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 't': {
                if(count < 2) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF e(num[0] + offsetX, num[1] + offsetY);
                num += 2;
                count -= 2;
                QPointF c;
                if(lastMode == 'q' || lastMode == 'Q' ||
                    lastMode == 't' || lastMode == 'T')
                    c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c = QPointF(x, y);
                lastPath->quadTo(c, e);

                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'T': {
                if(count < 2) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF e(num[0], num[1]);
                num += 2;
                count -= 2;
                QPointF c;
                if(lastMode == 'q' || lastMode == 'Q' ||
                    lastMode == 't' || lastMode == 'T')
                    c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c = QPointF(x, y);
                lastPath->quadTo(c, e);

                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'a': {
                if(count < 7) {
                    num += count;
                    count = 0;
                    break;
                }
                qreal rx = (*num++);
                qreal ry = (*num++);
                qreal xAxisRotation = (*num++);
                qreal largeArcFlag  = (*num++);
                qreal sweepFlag = (*num++);
                qreal ex = (*num++) + offsetX;
                qreal ey = (*num++) + offsetY;
                count -= 7;
                qreal curx = x;
                qreal cury = y;
                lastPath->pathArc(rx, ry, xAxisRotation, int(largeArcFlag),
                                  int(sweepFlag), ex, ey, curx, cury);

                x = ex;
                y = ey;
            }
                break;
            case 'A': {
                if(count < 7) {
                    num += count;
                    count = 0;
                    break;
                }
                qreal rx = (*num++);
                qreal ry = (*num++);
                qreal xAxisRotation = (*num++);
                qreal largeArcFlag  = (*num++);
                qreal sweepFlag = (*num++);
                qreal ex = (*num++);
                qreal ey = (*num++);
                count -= 7;
                qreal curx = x;
                qreal cury = y;
                lastPath->pathArc(rx, ry, xAxisRotation, int(largeArcFlag),
                                  int(sweepFlag), ex, ey, curx, cury);

                x = ex;
                y = ey;
            }
                break;
            default:
                return false;
            }
            lastMode = pathElem.toLatin1();
        }
    }
    return true;
}


bool parsePolylineDataFast(const QString &dataStr,
                           VectorPathSvgAttributes &attributes) {
    qreal x0 = 0, y0 = 0;              // starting point
    qreal x = 0, y = 0;                // current point
    const QChar *str = dataStr.constData();
    const QChar *end = str + dataStr.size();

    SvgSeparatePath *lastPath = nullptr;
    while (str != end) {
        while (str->isSpace())
            ++str;
        QChar endc = *end;
        *const_cast<QChar *>(end) = 0; // parseNumbersArray requires 0-termination that QStringRef cannot guarantee
        QVarLengthArray<qreal, 8> arg;
        parseNumbersArray(str, arg);
        *const_cast<QChar *>(end) = endc;
        const qreal *num = arg.constData();
        int count = arg.count();
        bool first = true;
        while (count > 0) {
            x = num[0];
            y = num[1];
            num++;
            num++;
            count -= 2;
            if(count < 0) {
                if(qAbs(x - x0) < 0.001 &&
                   qAbs(y - y0) < 0.001) {
                    lastPath->closePath();
                    return true;
                }
            }
            if(first) {
                x0 = x;
                y0 = y;
                lastPath = attributes.newSeparatePath();
                lastPath->moveTo(QPointF(x0, y0));
                first = false;
            } else {
                lastPath->lineTo(QPointF(x, y));
            }
        }
    }
    return true;
}

qsptr<ContainerBox> loadBoxesGroup(const QDomElement &groupElement,
                           ContainerBox *parentGroup,
                           const BoxSvgAttributes &attributes) {
    const QDomNodeList allRootChildNodes = groupElement.childNodes();
    qsptr<ContainerBox> boxesGroup;
    const bool hasTransform = attributes.hasTransform();
    if(allRootChildNodes.count() > 1 ||
       hasTransform || parentGroup == nullptr) {
        boxesGroup = SPtrCreate(ContainerBox)(TYPE_GROUP);
        boxesGroup->planCenterPivotPosition();
        attributes.apply(boxesGroup.get());
        if(parentGroup) parentGroup->addContainedBox(boxesGroup);
    } else {
        boxesGroup = GetAsSPtr(parentGroup, ContainerBox);
    }

    for(int i = 0; i < allRootChildNodes.count(); i++) {
        const QDomNode iNode = allRootChildNodes.at(i);
        if(iNode.isElement()) {
            loadElement(iNode.toElement(), boxesGroup.get(), attributes);
        }
    }
    return boxesGroup;
}

void loadVectorPath(const QDomElement &pathElement,
                    ContainerBox *parentGroup,
                    VectorPathSvgAttributes& attributes) {
    const auto vectorPath = SPtrCreate(SmartVectorPath)();
    vectorPath->planCenterPivotPosition();
    const QString pathStr = pathElement.attribute("d");
    parsePathDataFast(pathStr, attributes);
    attributes.apply(vectorPath.get());
    parentGroup->addContainedBox(vectorPath);
}

void loadPolyline(const QDomElement &pathElement,
                  ContainerBox *parentGroup,
                  VectorPathSvgAttributes &attributes) {
    auto vectorPath = SPtrCreate(SmartVectorPath)();
    vectorPath->planCenterPivotPosition();
    const QString pathStr = pathElement.attribute("points");
    parsePolylineDataFast(pathStr, attributes);
    attributes.apply(vectorPath.get());
    parentGroup->addContainedBox(vectorPath);
}

void loadCircle(const QDomElement &pathElement,
                ContainerBox *parentGroup,
                const BoxSvgAttributes &attributes) {

    const QString cXstr = pathElement.attribute("cx");
    const QString cYstr = pathElement.attribute("cy");
    const QString rStr = pathElement.attribute("r");
    const QString rXstr = pathElement.attribute("rx");
    const QString rYstr = pathElement.attribute("ry");

    qsptr<Circle> circle;
    if(!rStr.isEmpty()) {
        circle = SPtrCreate(Circle)();
        circle->setRadius(rStr.toDouble());
    } else if(!rXstr.isEmpty() && !rYstr.isEmpty()) {
        circle = SPtrCreate(Circle)();
        circle->setHorizontalRadius(rXstr.toDouble());
        circle->setVerticalRadius(rYstr.toDouble());
    } else if(!rXstr.isEmpty() || !rYstr.isEmpty()) {
        circle = SPtrCreate(Circle)();
        const qreal rad = rXstr.isEmpty() ? rYstr.toDouble() : rXstr.toDouble();
        circle->setHorizontalRadius(rad);
        circle->setVerticalRadius(rad);
    } else return;
    circle->planCenterPivotPosition();

    circle->moveByRel(QPointF(cXstr.toDouble(), cYstr.toDouble()));

    attributes.apply(circle.data());
    parentGroup->addContainedBox(circle);
}

void loadRect(const QDomElement &pathElement,
              ContainerBox *parentGroup,
              const BoxSvgAttributes &attributes) {

    const QString xStr = pathElement.attribute("x");
    const QString yStr = pathElement.attribute("y");
    const QString wStr = pathElement.attribute("width");
    const QString hStr = pathElement.attribute("height");
    const QString rYstr = pathElement.attribute("ry");
    const QString rXstr = pathElement.attribute("rx");

    const auto rect = SPtrCreate(Rectangle)();
    rect->planCenterPivotPosition();

    rect->moveByRel(QPointF(xStr.toDouble(), yStr.toDouble()));
    rect->setTopLeftPos(QPointF(0, 0));
    rect->setBottomRightPos(QPointF(wStr.toDouble(), hStr.toDouble()));
    if(rYstr.isEmpty()) {
        rect->setYRadius(rXstr.toDouble());
        rect->setXRadius(rXstr.toDouble());
    } else if(rXstr.isEmpty()) {
        rect->setYRadius(rYstr.toDouble());
        rect->setXRadius(rYstr.toDouble());
    } else {
        rect->setYRadius(rYstr.toDouble());
        rect->setXRadius(rXstr.toDouble());
    }

    attributes.apply(rect.data());
    parentGroup->addContainedBox(rect);
}


void loadText(const QDomElement &pathElement,
              ContainerBox *parentGroup,
              const BoxSvgAttributes &attributes) {

    const QString xStr = pathElement.attribute("x");
    const QString yStr = pathElement.attribute("y");

    const auto textBox = SPtrCreate(TextBox)();
    textBox->planCenterPivotPosition();

    textBox->moveByRel(QPointF(xStr.toDouble(), yStr.toDouble()));
    textBox->setCurrentValue(pathElement.text());

    attributes.apply(textBox.data());
    parentGroup->addContainedBox(textBox);
}


bool extractTranslation(const QString& str, QMatrix& target) {
    const QRegExp rx1("translate\\("
                      "\\s*(-?\\d+(\\.\\d*)?)"
                      "\\)", Qt::CaseInsensitive);
    if(rx1.exactMatch(str)) {
        rx1.indexIn(str);
        const QStringList capturedTxt = rx1.capturedTexts();
        target.translate(capturedTxt.at(1).toDouble(),
                         capturedTxt.at(1).toDouble());
        return true;
    }

    const QRegExp rx2("translate\\("
                      "\\s*(-?\\d+(\\.\\d*)?),"
                      "\\s*(-?\\d+(\\.\\d*)?)"
                      "\\)", Qt::CaseInsensitive);
    if(rx2.exactMatch(str)) {
        rx2.indexIn(str);
        const QStringList capturedTxt = rx2.capturedTexts();
        target.translate(capturedTxt.at(1).toDouble(),
                         capturedTxt.at(3).toDouble());
        return true;
    }

    return false;
}


bool extractScale(const QString& str, QMatrix& target) {
    const QRegExp rx1("scale\\("
                      "\\s*(-?\\d+(\\.\\d*)?)"
                      "\\)", Qt::CaseInsensitive);
    if(rx1.exactMatch(str)) {
        rx1.indexIn(str);
        const QStringList capturedTxt = rx1.capturedTexts();
        target.scale(capturedTxt.at(1).toDouble(),
                     capturedTxt.at(1).toDouble());
        return true;
    }

    const QRegExp rx2("scale\\("
                      "\\s*(-?\\d+(\\.\\d*)?),"
                      "\\s*(-?\\d+(\\.\\d*)?)"
                      "\\)", Qt::CaseInsensitive);
    if(rx2.exactMatch(str)) {
        rx2.indexIn(str);
        const QStringList capturedTxt = rx2.capturedTexts();
        target.scale(capturedTxt.at(1).toDouble(),
                     capturedTxt.at(3).toDouble());
        return true;
    }

    return false;
}

bool extractRotate(const QString& str, QMatrix& target) {
    const QRegExp rx5("rotate\\("
                      "\\s*(-?\\d+(\\.\\d*)?)"
                      "\\)", Qt::CaseInsensitive);
    if(rx5.exactMatch(str)) {
        rx5.indexIn(str);
        const QStringList capturedTxt = rx5.capturedTexts();
        target.rotate(capturedTxt.at(1).toDouble());
        return true;
    }
    return false;
}

bool extractWholeMatrix(const QString& str, QMatrix& target) {
    const QRegExp rx("matrix\\("
                     "\\s*(-?\\d+(\\.\\d*)?),"
                     "\\s*(-?\\d+(\\.\\d*)?),"
                     "\\s*(-?\\d+(\\.\\d*)?),"
                     "\\s*(-?\\d+(\\.\\d*)?),"
                     "\\s*(-?\\d+(\\.\\d*)?),"
                     "\\s*(-?\\d+(\\.\\d*)?)"
                     "\\)", Qt::CaseInsensitive);
    if(rx.exactMatch(str)) {
        rx.indexIn(str);
        const QStringList capturedTxt = rx.capturedTexts();
        target.setMatrix(capturedTxt.at(1).toDouble(),
                         capturedTxt.at(3).toDouble(),
                         capturedTxt.at(5).toDouble(),
                         capturedTxt.at(7).toDouble(),
                         capturedTxt.at(9).toDouble(),
                         capturedTxt.at(11).toDouble());
        return true;
    }
    return false;
}

QMatrix getMatrixFromString(const QString &str) {
    QMatrix matrix;
    if(str.isEmpty()) return matrix;
    const bool found = str.isEmpty() ||
                       extractWholeMatrix(str, matrix) ||
                       extractTranslation(str, matrix) ||
                       extractScale(str, matrix) ||
                       extractRotate(str, matrix);
    if(!found) qDebug() << "getMatrixFromString - could not extract "
                           "values from string:" << endl << str;
    return matrix;
}

#include "GUI/GradientWidgets/gradientwidget.h"
static QMap<QString, SvgGradient> gGradients;
void loadElement(const QDomElement &element, ContainerBox *parentGroup,
                 const BoxSvgAttributes &parentGroupAttributes) {
    const QString tagName = element.tagName();
    if(tagName == "defs") {
        const QDomNodeList allRootChildNodes = element.childNodes();
        for(int i = 0; i < allRootChildNodes.count(); i++) {
            const QDomNode iNode = allRootChildNodes.at(i);
            if(iNode.isElement()) {
                loadElement(iNode.toElement(), parentGroup, parentGroupAttributes);
            }
        }
        return;
    } else if(tagName == "linearGradient") {
        const QString id = element.attribute("id");
        QString linkId = element.attribute("xlink:href");
        Gradient* gradient = nullptr;
        if(linkId.isEmpty()) {
            gradient = Document::sInstance->createNewGradient();
            const QDomNodeList allRootChildNodes = element.childNodes();
            for(int i = 0; i < allRootChildNodes.count(); i++) {
                const QDomNode iNode = allRootChildNodes.at(i);
                if(!iNode.isElement()) continue;
                const QDomElement elem = iNode.toElement();
                if(elem.tagName() != "stop") continue;
                QColor stopColor;
                const QString stopStyle = elem.attribute("style");
                QList<SvgAttribute> attributesList;
                extractSvgAttributes(stopStyle, &attributesList);
                for(const auto& attr : attributesList) {
                    if(attr.fName == "stop-color") {
                        const qreal alpha = stopColor.alphaF();
                        toColor(attr.fValue, stopColor);
                        stopColor.setAlphaF(alpha);

                    } else if(attr.fName == "stop-opacity") {
                        stopColor.setAlphaF(toDouble(attr.fValue));
                    }
                }
                gradient->addColor(stopColor);
            }
        } else {
            if(linkId.at(0) == "#") linkId.remove(0, 1);
            const auto it = gGradients.find(linkId);
            if(it != gGradients.end()) {
                gradient = it.value().fGradient;
            } else {
                gradient = Document::sInstance->createNewGradient();
            }
        }
        const QString x1 = element.attribute("x1");
        const QString y1 = element.attribute("y1");
        const QString x2 = element.attribute("x2");
        const QString y2 = element.attribute("y2");
        const QString gradTrans = element.attribute("gradientTransform");
        const QMatrix trans = getMatrixFromString(gradTrans);

        gGradients.insert(id, {gradient,
                               toDouble(x1), toDouble(y1),
                               toDouble(x2), toDouble(y2),
                               trans});
    }
    if(tagName == "path" || tagName == "polyline") {
        VectorPathSvgAttributes attributes;
        attributes.setParent(parentGroupAttributes);
        attributes.loadBoundingBoxAttributes(element);
        if(tagName == "path") {
            loadVectorPath(element, parentGroup, attributes);
        } else { // if(tagName == "polyline") {
            loadPolyline(element, parentGroup, attributes);
        }
    } else {
        BoxSvgAttributes attributes;
        attributes.setParent(parentGroupAttributes);
        attributes.loadBoundingBoxAttributes(element);
        if(tagName == "g" || tagName == "text") {
            const auto group = loadBoxesGroup(element, parentGroup, attributes);
            if(group->getContainedBoxesCount() == 0)
                group->removeFromParent_k();
        } else if(tagName == "circle" || tagName == "ellipse") {
            loadCircle(element, parentGroup, attributes);
        } else if(tagName == "rect") {
            loadRect(element, parentGroup, attributes);
        } else if(tagName == "tspan") {
            loadText(element, parentGroup, attributes);
        }
    }
}

bool getUrlId(const QString &urlStr, QString *id) {
    const QRegExp rx = QRegExp("url\\(\\s*#(.*)\\)", Qt::CaseInsensitive);
    if(rx.exactMatch(urlStr)) {
        rx.indexIn(urlStr);
        const QStringList capturedTxt = rx.capturedTexts();
        *id = capturedTxt.at(1);
        return true;
    }

    return false;
}

bool getGradientFromString(const QString &colorStr,
                           FillSvgAttributes * const target) {
    const QRegExp rx = QRegExp("url\\(\\s*(.*)\\s*\\)", Qt::CaseInsensitive);
    if(rx.exactMatch(colorStr)) {
        const QStringList capturedTxt = rx.capturedTexts();
        QString id = capturedTxt.at(1);
        if(id.at(0) == '#') id.remove(0, 1);
        const auto it = gGradients.find(id);
        if(it != gGradients.end()) {
            target->setGradient(it.value());
            return true;
        }
    }
    return false;
}

bool getFlatColorFromString(const QString &colorStr, FillSvgAttributes *target) {
    QColor color;
    if(!toColor(colorStr, color)) return false;
    target->setColor(color);
    target->setPaintType(FLATPAINT);
    return true;
}

qsptr<BoundingBox> loadSVGFile(const QString &filename) {
    QFile file(filename);
    if(file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QDomDocument document;
        if(document.setContent(&file) ) {
            const QDomElement rootElement = document.firstChildElement("svg");
            if(!rootElement.isNull()) {
                BoxSvgAttributes attributes;
                const auto result = loadBoxesGroup(rootElement, nullptr, attributes);
                gGradients.clear();
                if(result->getContainedBoxesCount() == 1) {
                    return result->takeContainedBox_k(0);
                } else if(result->getContainedBoxesCount() == 0) {
                    return nullptr;
                }
                return result;
            } else {
                RuntimeThrow("File does not have svg root element");
            }
        } else {
            RuntimeThrow("Cannot set file as QDomDocument content");
        }
    } else {
        RuntimeThrow("Cannot open file " + filename);
    }
}

void BoxSvgAttributes::setParent(const BoxSvgAttributes &parent) {
    mFillAttributes = parent.getFillAttributes();
    mStrokeAttributes = parent.getStrokeAttributes();
    mTextAttributes = parent.getTextAttributes();
    mFillRule = parent.getFillRule();
}

const Qt::FillRule &BoxSvgAttributes::getFillRule() const {
    return mFillRule;
}

const QMatrix &BoxSvgAttributes::getRelTransform() const {
    return mRelTransform;
}

const FillSvgAttributes &BoxSvgAttributes::getFillAttributes() const {
    return mFillAttributes;
}

const StrokeSvgAttributes &BoxSvgAttributes::getStrokeAttributes() const {
    return mStrokeAttributes;
}

const TextSvgAttributes &BoxSvgAttributes::getTextAttributes() const {
    return mTextAttributes;
}

void BoxSvgAttributes::setFillAttribute(const QString &value) {
    if(value.contains("none")) {
        mFillAttributes.setPaintType(NOPAINT);
    } else if(getFlatColorFromString(value, &mFillAttributes)) {
    } else if(getGradientFromString(value, &mFillAttributes)) {
    } else {
        qDebug() << "setFillAttribute - format not recognised:" <<
                    endl << value;
    }
}

void BoxSvgAttributes::setStrokeAttribute(const QString &value) {
    if(value.contains("none")) {
        mStrokeAttributes.setPaintType(NOPAINT);
    } else if(getFlatColorFromString(value, &mStrokeAttributes)) {
    } else if(getGradientFromString(value, &mStrokeAttributes)) {
    } else {
        qDebug() << "setStrokeAttribute - format not recognised:" <<
                    endl << value;
    }
}

QString stripPx(const QString& val) {
    QString result = val;
    result.remove("px");
    return result;
}

void BoxSvgAttributes::loadBoundingBoxAttributes(const QDomElement &element) {
    QList<SvgAttribute> styleAttributes;
    const QString styleAttributesStr = element.attribute("style");
    extractSvgAttributes(styleAttributesStr, &styleAttributes);
    for(const SvgAttribute &attribute : styleAttributes) {
        const QString name = attribute.getName();
        if(name.isEmpty()) continue;
        const QString value = attribute.getValue();
        if(value == "inherit") continue;
        switch (name.at(0).unicode()) {
        case 'c':
            if(name == "color") {
                mFillAttributes.setPaintType(FLATPAINT);
                QColor color = mFillAttributes.getColor();
                color.setAlphaF(toDouble(value));
                mFillAttributes.setColor(color);
            } else if(name == "color-opacity") {
                QColor color = mFillAttributes.getColor();
                color.setAlphaF(toDouble(value));
                mFillAttributes.setColor(color);
            } else if(name == "comp-op") {
                //compOp = value;
            }
            break;

        case 'd':
            if(name == "display") {
                //display = value;
            }
            break;

        case 'f':
            if(name == "fill") {
                setFillAttribute(value);
            } else if(name == "fill-rule") {
                if(value == "nonzero") {
                    mFillRule = Qt::WindingFill;
                } else { // "evenodd"
                    mFillRule = Qt::OddEvenFill;
                }
            } else if(name == "fill-opacity") {
                mFillAttributes.setColorOpacity(toDouble(value));
            } else if(name == "font-family") {
                QString stripQuotes = value;
                stripQuotes.remove("'");
                mTextAttributes.setFontFamily(stripQuotes);
            } else if(name == "font-size") {
                mTextAttributes.setFontSize(qRound(stripPx(value).toDouble()));
            } else if(name == "font-style") {
                if(value == "normal") {
                    mTextAttributes.setFontStyle(QFont::StyleNormal);
                } else if(value == "italic") {
                    mTextAttributes.setFontStyle(QFont::StyleItalic);
                } else if(value == "oblique") {
                    mTextAttributes.setFontStyle(QFont::StyleOblique);
                }
            } else if(name == "font-weight") {
                if(value == "normal") {
                    mTextAttributes.setFontWeight(QFont::Normal);
                } else if(value == "bold") {
                    mTextAttributes.setFontWeight(QFont::Bold);
                } else if(value == "bolder") {
                    mTextAttributes.setFontWeight(QFont::ExtraBold);
                } else if(value == "lighter") {
                    mTextAttributes.setFontWeight(QFont::ExtraLight);
                } else {
                    bool ok;
                    const int val = value.toInt(&ok);
                    if(ok) mTextAttributes.setFontWeight(val/10);
                    else qDebug() << "Unrecognized font-weight '" + value + "'";
                }
            } else if(name == "font-variant") {
                //fontVariant = value;
            }
            break;

        case 'i':
            if(name == "id") mId = value;
            break;

        case 'o':
            if(name == "opacity") {
                mOpacity = toDouble(value)*100.;
            } else if(name == "offset") {
                //offset = value;
            }
            break;

        case 's':
            if(name.contains("stroke")) {
                if(name == "stroke") {
                    setStrokeAttribute(value);
                } else if(name == "stroke-dasharray") {
                    //strokeDashArray = value;
                } else if(name == "stroke-dashoffset") {
                    //strokeDashOffset = value;
                } else if(name == "stroke-linecap") {
                    if(value == "butt") {
                        mStrokeAttributes.setCapStyle(SkPaint::kButt_Cap);
                    } else if(value == "round") {
                        mStrokeAttributes.setCapStyle(SkPaint::kRound_Cap);
                    } else {
                        mStrokeAttributes.setCapStyle(SkPaint::kSquare_Cap);
                    }
                } else if(name == "stroke-linejoin") {
                    if(value == "miter") {
                        mStrokeAttributes.setJoinStyle(SkPaint::kMiter_Join);
                    } else if(value == "round") {
                        mStrokeAttributes.setJoinStyle(SkPaint::kRound_Join);
                    } else {
                        mStrokeAttributes.setJoinStyle(SkPaint::kBevel_Join);
                    }
                } else if(name == "stroke-miterlimit") {
                    //mStrokeAttributes.setMiterLimit(toDouble(value));
                } else if(name == "stroke-opacity") {
                    mStrokeAttributes.setColorOpacity(toDouble(value));
                } else if(name == "stroke-width") {
                    if(value.contains("%")) {
                        mStrokeAttributes.setLineWidth(
                                    mStrokeAttributes.getLineWidth()*
                                    value.toDouble()/100.);
                    } else {
                        mStrokeAttributes.setLineWidth(
                                    stripPx(value).toDouble());
                    }
                }
            } else if(name == "stop-color") {
                //stopColor = value;
            } else if(name == "stop-opacity") {
                //stopOpacity = value;
            }
            break;
        case 't':
            if(name == "text-anchor") {
                if(value == "end") {
                    mTextAttributes.setFontAlignment(Qt::AlignLeft);
                } else if(value == "middle") {
                    mTextAttributes.setFontAlignment(Qt::AlignHCenter);
                } else if(value == "start") {
                    mTextAttributes.setFontAlignment(Qt::AlignRight);
                }
            } else if(name == "transform") {
                mRelTransform = getMatrixFromString(value)*mRelTransform;
            }
            break;

        case 'v':
            if(name == "vector-effect") {
                //vectorEffect = value;
            } else if(name == "visibility") {
                //visibility = value;
            }
            break;

        case 'x':
            if(name == "xml:id") {
                mId = value;
            }
            break;

        default:
            break;
        }
    }

    const QString fillAttributesStr = element.attribute("fill");
    if(!fillAttributesStr.isEmpty()) setFillAttribute(fillAttributesStr);

    const QString strokeAttributesStr = element.attribute("stroke");
    if(!strokeAttributesStr.isEmpty()) setStrokeAttribute(strokeAttributesStr);


    const QString matrixStr = element.attribute("transform");
    if(!matrixStr.isEmpty()) {
        mRelTransform = getMatrixFromString(matrixStr)*mRelTransform;
    }

    decomposeTransformMatrix();
}

bool BoxSvgAttributes::hasTransform() const {
    return !(isZero4Dec(mRelTransform.dx()) &&
             isZero4Dec(mRelTransform.dy()) &&
             isZero4Dec(mRelTransform.m11() - 1) &&
             isZero4Dec(mRelTransform.m22() - 1) &&
             isZero4Dec(mRelTransform.m12()) &&
             isZero4Dec(mRelTransform.m21()));
}

void BoxSvgAttributes::decomposeTransformMatrix() {
    const qreal m11 = mRelTransform.m11();
    const qreal m12 = mRelTransform.m12();
    const qreal m21 = mRelTransform.m21();
    const qreal m22 = mRelTransform.m22();

    const qreal delta = m11 * m22 - m21 * m12;

    // Apply the QR-like decomposition.
    if(!isZero4Dec(m11) || !isZero4Dec(m21)) {
        const qreal r = sqrt(m11 * m11 + m21 * m21);
        const qreal rotRad = m21 > 0 ? acos(m11 / r) : -acos(m11 / r);
        mRot = rotRad*180/PI;
        mScaleX = r;
        mScaleY = delta/r;
        mShearX = atan((m11 * m12 + m21 * m22) / (r * r));
        mShearY = 0;
    } else if(!isZero4Dec(m12) || !isZero4Dec(m22)) {
        const qreal s = sqrt(m12 * m12 + m22 * m22);
        const qreal rotRad = m22 > 0 ? acos(-m12 / s) : -acos(-m12 / s);
        mRot = 90 - rotRad*180/PI;
        mScaleX = delta/s;
        mScaleY = s;
        mShearX = 0;
        mShearY = atan((m11 * m12 + m21 * m22) / (s * s));
    } else {
        mRot = 0;
        mScaleX = 0;
        mScaleY = 0;
        mShearX = 0;
        mShearY = 0;
    }

    mDx = mRelTransform.dx();
    mDy = mRelTransform.dy();

//    mDx = mRelTransform.dx();
//    mDy = mRelTransform.dy();

//    const qreal sxAbs = qSqrt(m11*m11 + m21*m21);
//    const qreal syAbs = qSqrt(m12*m12 + m22*m22);
//    mScaleX = m11 > 0 ? sxAbs : -sxAbs;
//    mScaleY = m22 > 0 ? syAbs : -syAbs;

//    const qreal nM12oM11 = -m12/m11;
//    const qreal m21oM22 = m21/m22;
//    const bool hasShear = !isZero4Dec(nM12oM11 - m21oM22);

//    if(hasShear) {
//        mHasRemTrans = true;

//        return;
//    }

//    mRot = atan2(-m12, m11)*180/PI;
}

/*

#define QT_INHERIT QLatin1String(qt_inherit_text)

typedef BoundingBox *(*FactoryMethod)(BoxesGroup *, const QXmlStreamAttributes &, QSvgHandler *);


bool QSvgHandler::startElement(const QString &localName,
                               const QXmlStreamAttributes &attributes)
{
    QSvgNode *node = 0;

    pushColorCopy();

    const QStringRef xmlSpace(attributes.value(QLatin1String("xml:space")));
    if(xmlSpace.isNull()) {
        // This element has no xml:space attribute.
        m_whitespaceMode.push(m_whitespaceMode.isEmpty() ? QSvgText::Default : m_whitespaceMode.top());
    } else if(xmlSpace == QLatin1String("preserve")) {
        m_whitespaceMode.push(QSvgText::Preserve);
    } else if(xmlSpace == QLatin1String("default")) {
        m_whitespaceMode.push(QSvgText::Default);
    } else {
        qWarning() << QString::fromLatin1("\"%1\" is an invalid value for attribute xml:space. "
                                          "Valid values are \"preserve\" and \"default\".").arg(xmlSpace.toString());
        m_whitespaceMode.push(QSvgText::Default);
    }

    if(!m_doc && localName != QLatin1String("svg"))
        return false;

    if(FactoryMethod method = findGroupFactory(localName)) {
        //group
        node = method(m_doc ? m_nodes.top() : 0, attributes, this);
        Q_ASSERT(node);
        if(!m_doc) {
            Q_ASSERT(node->type() == QSvgNode::DOC);
            m_doc = static_cast<QSvgTinyDocument*>(node);
        } else {
            switch (m_nodes.top()->type()) {
            case QSvgNode::DOC:
            case QSvgNode::G:
            case QSvgNode::DEFS:
            case QSvgNode::SWITCH:
            {
                QSvgStructureNode *group =
                    static_cast<QSvgStructureNode*>(m_nodes.top());
                group->addChild(node, someId(attributes));
            }
                break;
            default:
                break;
            }
        }
        parseCoreNode(node, attributes);
#ifndef QT_NO_CSSPARSER
        cssStyleLookup(node, this, m_selector);
#endif
        parseStyle(node, attributes, this);
    } else if(FactoryMethod method = findGraphicsFactory(localName)) {
        //rendering element
        Q_ASSERT(!m_nodes.isEmpty());
        node = method(m_nodes.top(), attributes, this);
        if(node) {
            switch (m_nodes.top()->type()) {
            case QSvgNode::DOC:
            case QSvgNode::G:
            case QSvgNode::DEFS:
            case QSvgNode::SWITCH:
            {
                QSvgStructureNode *group =
                    static_cast<QSvgStructureNode*>(m_nodes.top());
                group->addChild(node, someId(attributes));
            }
                break;
            case QSvgNode::TEXT:
            case QSvgNode::TEXTAREA:
                if(node->type() == QSvgNode::TSPAN) {
                    static_cast<QSvgText *>(m_nodes.top())->addTspan(static_cast<QSvgTspan *>(node));
                } else {
                    qWarning("\'text\' or \'textArea\' element contains invalid element type.");
                    delete node;
                    node = 0;
                }
                break;
            default:
                qWarning("Could not add child element to parent element because the types are incorrect.");
                delete node;
                node = 0;
                break;
            }

            if(node) {
                parseCoreNode(node, attributes);
#ifndef QT_NO_CSSPARSER
                cssStyleLookup(node, this, m_selector);
#endif
                parseStyle(node, attributes, this);
                if(node->type() == QSvgNode::TEXT || node->type() == QSvgNode::TEXTAREA) {
                    static_cast<QSvgText *>(node)->setWhitespaceMode(m_whitespaceMode.top());
                } else if(node->type() == QSvgNode::TSPAN) {
                    static_cast<QSvgTspan *>(node)->setWhitespaceMode(m_whitespaceMode.top());
                }
            }
        }
    } else if(ParseMethod method = findUtilFactory(localName)) {
        Q_ASSERT(!m_nodes.isEmpty());
        if(!method(m_nodes.top(), attributes, this)) {
            qWarning("Problem parsing %s", qPrintable(localName));
        }
    } else if(StyleFactoryMethod method = findStyleFactoryMethod(localName)) {
        QSvgStyleProperty *prop = method(m_nodes.top(), attributes, this);
        if(prop) {
            m_style = prop;
            m_nodes.top()->appendStyleProperty(prop, someId(attributes));
        } else {
            qWarning("Could not parse node: %s", qPrintable(localName));
        }
    } else if(StyleParseMethod method = findStyleUtilFactoryMethod(localName)) {
        if(m_style) {
            if(!method(m_style, attributes, this)) {
                qWarning("Problem parsing %s", qPrintable(localName));
            }
        }
    } else {
        //qWarning()<<"Skipping unknown element!"<<namespaceURI<<"::"<<localName;
        m_skipNodes.push(Unknown);
        return true;
    }

    if(node) {
        m_nodes.push(node);
        m_skipNodes.push(Graphics);
    } else {
        //qDebug()<<"Skipping "<<localName;
        m_skipNodes.push(Style);
    }
    return true;
}


static FactoryMethod findGraphicsFactory(const QString &name)
{
    if(name.isEmpty())
        return 0;

    QStringRef ref(&name, 1, name.length() - 1);
    switch (name.at(0).unicode()) {
    case 'a':
        if(ref == QLatin1String("nimation")) return createAnimationNode;
        break;
    case 'c':
        if(ref == QLatin1String("ircle")) return createCircleNode;
        break;
    case 'e':
        if(ref == QLatin1String("llipse")) return createEllipseNode;
        break;
    case 'i':
        if(ref == QLatin1String("mage")) return createImageNode;
        break;
    case 'l':
        if(ref == QLatin1String("ine")) return createLineNode;
        break;
    case 'p':
        if(ref == QLatin1String("ath")) return createPathNode;
        if(ref == QLatin1String("olygon")) return createPolygonNode;
        if(ref == QLatin1String("olyline")) return createPolylineNode;
        break;
    case 'r':
        if(ref == QLatin1String("ect")) return createRectNode;
        break;
    case 't':
        if(ref == QLatin1String("ext")) return createTextNode;
        if(ref == QLatin1String("extArea")) return createTextAreaNode;
        if(ref == QLatin1String("span")) return createTspanNode;
        break;
    case 'u':
        if(ref == QLatin1String("se")) return createUseNode;
        break;
    case 'v':
        if(ref == QLatin1String("ideo")) return createVideoNode;
        break;
    default:
        break;
    }
    return 0;
}


BoundingBox *createPathNode(BoxesGroup *parent,
                                const QXmlStreamAttributes &attributes)
{
    QStringRef data = attributes.value(QLatin1String("d"));

    VectorPath *newPath = new VectorPath(parent);
    parsePathDataFast(data, newPath);
    return newPath;
}

QVector<qreal> parsePercentageList(const QChar *&str)
{
    QVector<qreal> points;
    if(!str)
        return points;

    while (str->isSpace())
        ++str;
    while ((*str >= QLatin1Char('0') && *str <= QLatin1Char('9')) ||
           *str == QLatin1Char('-') || *str == QLatin1Char('+') ||
           *str == QLatin1Char('.')) {

        points.append(toDouble(str));

        while (str->isSpace())
            ++str;
        if(*str == QLatin1Char('%'))
            ++str;
        while (str->isSpace())
            ++str;
        if(*str == QLatin1Char(','))
            ++str;

        //eat the rest of space
        while (str->isSpace())
            ++str;
    }

    return points;
}

bool resolveColor(const QStringRef &colorStr, QColor &color, QSvgHandler *handler)
{
    QStringRef colorStrTr = trimRef(colorStr);
    if(colorStrTr.isEmpty())
        return false;

    switch(colorStrTr.at(0).unicode()) {

        case '#':
            {
                // #rrggbb is very very common, so let's tackle it here
                // rather than falling back to QColor
                QRgb rgb;
                bool ok = qsvg_get_hex_rgb(colorStrTr.unicode(), colorStrTr.length(), &rgb);
                if(ok)
                    color.setRgb(rgb);
                return ok;
            }
            break;

        case 'r':
            {
                // starts with "rgb(", ends with ")" and consists of at least 7 characters "rgb(,,)"
                if(colorStrTr.length() >= 7 && colorStrTr.at(colorStrTr.length() - 1) == QLatin1Char(')')
                    && QStringRef(colorStrTr.string(), colorStrTr.position(), 4) == QLatin1String("rgb(")) {
                    const QChar *s = colorStrTr.constData() + 4;
                    QVector<qreal> compo = parseNumbersList(s);
                    //1 means that it failed after reaching non-parsable
                    //character which is going to be "%"
                    if(compo.size() == 1) {
                        s = colorStrTr.constData() + 4;
                        compo = parsePercentageList(s);
                        for(int i = 0; i < compo.size(); ++i)
                            compo[i] *= (qreal)2.55;
                    }

                    if(compo.size() == 3) {
                        color = QColor(int(compo[0]),
                                       int(compo[1]),
                                       int(compo[2]));
                        return true;
                    }
                    return false;
                }
            }
            break;

        case 'c':
            if(colorStrTr == QLatin1String("currentColor")) {
                color = handler->currentColor();
                return true;
            }
            break;
        case 'i':
            if(colorStrTr == QT_INHERIT)
                return false;
            break;
        default:
            break;
    }

    color = QColor(colorStrTr.toString());
    return color.isValid();
}

bool constructColor(const QStringRef &colorStr, const QStringRef &opacity,
                           QColor &color, QSvgHandler *handler)
{
    if(!resolveColor(colorStr, color, handler))
        return false;
    if(!opacity.isEmpty()) {
        bool ok = true;
        qreal op = qMin(qreal(1.0), qMax(qreal(0.0), toDouble(opacity, &ok)));
        if(!ok)
            op = 1.0;
        color.setAlphaF(op);
    }
    return true;
}

QColor parseColor(const QSvgAttributes &attributes,
                       QSvgHandler *handler)
{
    QColor color;
    if(constructColor(attributes.color, attributes.colorOpacity, color, handler)) {
        handler->popColor();
        handler->pushColor(color);
    }
    return color;
}

bool parsePathDataFast(const QStringRef &dataStr, VectorPath *path)
{
    qreal x0 = 0, y0 = 0;              // starting point
    qreal x = 0, y = 0;                // current point
    char lastMode = 0;
    QPointF ctrlPt;
    const QChar *str = dataStr.constData();
    const QChar *end = str + dataStr.size();

    NodePoint *firstPointAfterM = nullptr;
    NodePoint *lastAddedPoint = nullptr;
    while (str != end) {
        while (str->isSpace())
            ++str;
        QChar pathElem = *str;
        ++str;
        QChar endc = *end;
        *const_cast<QChar *>(end) = 0; // parseNumbersArray requires 0-termination that QStringRef cannot guarantee
        QVarLengthArray<qreal, 8> arg;
        parseNumbersArray(str, arg);
        *const_cast<QChar *>(end) = endc;
        if(pathElem == QLatin1Char('z') || pathElem == QLatin1Char('Z'))
            arg.append(0);//dummy
        const qreal *num = arg.constData();
        int count = arg.count();
        while (count > 0) {
            qreal offsetX = x;        // correction offsets
            qreal offsetY = y;        // for relative commands
            if(!firstPointAfterM) {
                firstPointAfterM = lastAddedPoint;
            }
            switch (pathElem.unicode()) {
            case 'm': {
                firstPointAfterM = nullptr;
                lastAddedPoint = nullptr;
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = x0 = num[0] + offsetX;
                y = y0 = num[1] + offsetY;
                num += 2;
                count -= 2;
                //path.moveTo(x0, y0);

                 // As per 1.2  spec 8.3.2 The "moveto" commands
                 // If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
                 // the subsequent pairs shall be treated as implicit 'lineto' commands.
                 pathElem = QLatin1Char('l');
            }
                break;
            case 'M': {
                firstPointAfterM = nullptr;
                lastAddedPoint = nullptr;
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = x0 = num[0];
                y = y0 = num[1];
                num += 2;
                count -= 2;
                //path.moveTo(x0, y0);

                // As per 1.2  spec 8.3.2 The "moveto" commands
                // If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
                // the subsequent pairs shall be treated as implicit 'lineto' commands.
                pathElem = QLatin1Char('L');
            }
                break;
            case 'z':
            case 'Z': {
                x = x0;
                y = y0;
                count--; // skip dummy
                num++;
                if(firstPointAfterM != nullptr && lastAddedPoint != nullptr) {
                    firstPointAfterM->connectToPoint(lastAddedPoint);
                    firstPointAfterM = nullptr;
                    lastAddedPoint = nullptr;
                }
            }
                break;
            case 'l': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = num[0] + offsetX;
                y = num[1] + offsetY;
                num += 2;
                count -= 2;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'L': {
                if(count < 2) {
                    num++;
                    count--;
                    break;
                }
                x = num[0];
                y = num[1];
                num += 2;
                count -= 2;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'h': {
                x = num[0] + offsetX;
                num++;
                count--;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'H': {
                x = num[0];
                num++;
                count--;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'v': {
                y = num[0] + offsetY;
                num++;
                count--;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'V': {
                y = num[0];
                num++;
                count--;
                //path.lineTo(x, y);
                // !!!
                lastAddedPoint = path->addPointRelPos(QPointF(x, y), 0, 0, lastAddedPoint);
                // !!!
            }
                break;
            case 'c': {
                if(count < 6) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1(num[0] + offsetX, num[1] + offsetY);
                QPointF c2(num[2] + offsetX, num[3] + offsetY);
                QPointF e(num[4] + offsetX, num[5] + offsetY);
                num += 6;
                count -= 6;
                //path.cubicTo(c1, c2, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c2, c1, lastAddedPoint);
                // !!!
                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'C': {
                if(count < 6) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1(num[0], num[1]);
                QPointF c2(num[2], num[3]);
                QPointF e(num[4], num[5]);
                num += 6;
                count -= 6;
                //path.cubicTo(c1, c2, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c2, c1, lastAddedPoint);
                // !!!
                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 's': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1;
                if(lastMode == 'c' || lastMode == 'C' ||
                    lastMode == 's' || lastMode == 'S')
                    c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c1 = QPointF(x, y);
                QPointF c2(num[0] + offsetX, num[1] + offsetY);
                QPointF e(num[2] + offsetX, num[3] + offsetY);
                num += 4;
                count -= 4;
                //path.cubicTo(c1, c2, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c2, c1, lastAddedPoint);
                // !!!
                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'S': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c1;
                if(lastMode == 'c' || lastMode == 'C' ||
                    lastMode == 's' || lastMode == 'S')
                    c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c1 = QPointF(x, y);
                QPointF c2(num[0], num[1]);
                QPointF e(num[2], num[3]);
                num += 4;
                count -= 4;
                //path.cubicTo(c1, c2, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c2, c1, lastAddedPoint);
                // !!!
                ctrlPt = c2;
                x = e.x();
                y = e.y();
                break;
            }
            case 'q': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c(num[0] + offsetX, num[1] + offsetY);
                QPointF e(num[2] + offsetX, num[3] + offsetY);
                num += 4;
                count -= 4;
                //path.quadTo(c, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c,
                                                      QPointF(0.f, 0.f),
                                                      lastAddedPoint);
                // !!!
                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'Q': {
                if(count < 4) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF c(num[0], num[1]);
                QPointF e(num[2], num[3]);
                num += 4;
                count -= 4;
                //path.quadTo(c, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c,
                                                      QPointF(0.f, 0.f),
                                                      lastAddedPoint);
                // !!!
                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 't': {
                if(count < 2) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF e(num[0] + offsetX, num[1] + offsetY);
                num += 2;
                count -= 2;
                QPointF c;
                if(lastMode == 'q' || lastMode == 'Q' ||
                    lastMode == 't' || lastMode == 'T')
                    c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c = QPointF(x, y);
                //path.quadTo(c, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c,
                                                      QPointF(0.f, 0.f),
                                                      lastAddedPoint);
                // !!!
                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'T': {
                if(count < 2) {
                    num += count;
                    count = 0;
                    break;
                }
                QPointF e(num[0], num[1]);
                num += 2;
                count -= 2;
                QPointF c;
                if(lastMode == 'q' || lastMode == 'Q' ||
                    lastMode == 't' || lastMode == 'T')
                    c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
                else
                    c = QPointF(x, y);
                //path.quadTo(c, e);
                // !!!
                lastAddedPoint = path->addPointRelPos(e, c,
                                                      QPointF(0.f, 0.f),
                                                      lastAddedPoint);
                // !!!
                ctrlPt = c;
                x = e.x();
                y = e.y();
                break;
            }
            case 'a': {
                if(count < 7) {
                    num += count;
                    count = 0;
                    break;
                }
                qreal rx = (*num++);
                qreal ry = (*num++);
                qreal xAxisRotation = (*num++);
                qreal largeArcFlag  = (*num++);
                qreal sweepFlag = (*num++);
                qreal ex = (*num++) + offsetX;
                qreal ey = (*num++) + offsetY;
                count -= 7;
                qreal curx = x;
                qreal cury = y;
                //pathArc(path, rx, ry, xAxisRotation, int(largeArcFlag),
                //        int(sweepFlag), ex, ey, curx, cury);

                x = ex;
                y = ey;
            }
                break;
            case 'A': {
                if(count < 7) {
                    num += count;
                    count = 0;
                    break;
                }
                qreal rx = (*num++);
                qreal ry = (*num++);
                qreal xAxisRotation = (*num++);
                qreal largeArcFlag  = (*num++);
                qreal sweepFlag = (*num++);
                qreal ex = (*num++);
                qreal ey = (*num++);
                count -= 7;
                qreal curx = x;
                qreal cury = y;
                //pathArc(path, rx, ry, xAxisRotation, int(largeArcFlag),
                //        int(sweepFlag), ex, ey, curx, cury);

                x = ex;
                y = ey;
            }
                break;
            default:
                return false;
            }
            lastMode = pathElem.toLatin1();
        }
    }
    return true;
}*/
#include "Animators/paintsettingsanimator.h"

void FillSvgAttributes::setColor(const QColor &val) {
    mColor = val;
    setPaintType(FLATPAINT);
}

void FillSvgAttributes::setColorOpacity(const qreal opacity) {
    mColor.setAlphaF(opacity);
}

void FillSvgAttributes::setPaintType(const PaintType &type) {
    mPaintType = type;
}

void FillSvgAttributes::setGradient(const SvgGradient& gradient) {
    mGradient = gradient.fGradient;
    mGradientP1 = gradient.fTrans.map(QPointF{gradient.fX1, gradient.fY1});
    mGradientP2 = gradient.fTrans.map(QPointF{gradient.fX2, gradient.fY2});
    if(!mGradient) return;
    setPaintType(GRADIENTPAINT);
}

const QColor &FillSvgAttributes::getColor() const { return mColor; }

PaintType FillSvgAttributes::getPaintType() const { return mPaintType; }

Gradient *FillSvgAttributes::getGradient() const { return mGradient; }

void FillSvgAttributes::apply(BoundingBox *box) const {
    apply(box, PaintSetting::FILL);
}

void FillSvgAttributes::apply(BoundingBox * const box,
                              const PaintSetting::Target& target) const {
    if(!box->SWT_isPathBox()) return;
    const auto pathBox = GetAsPtr(box, PathBox);
    if(mPaintType == FLATPAINT) {
        ColorSettingApplier colorSetting(RGBMODE, CVR_ALL,
                                  mColor.redF(), mColor.greenF(),
                                  mColor.blueF(), mColor.alphaF(),
                                  CST_CHANGE);
        ColorPaintSetting(target, colorSetting).apply(pathBox);
    } else if(mPaintType == GRADIENTPAINT) {
        GradientPaintSetting(target, mGradient).apply(pathBox);
        GradientPtsPosSetting(target, mGradientP1, mGradientP2).apply(pathBox);
    }
    PaintTypePaintSetting(target, mPaintType).apply(pathBox);
}

qreal StrokeSvgAttributes::getLineWidth() const {
    return mLineWidth;
}

SkPaint::Cap StrokeSvgAttributes::getCapStyle() const {
    return mCapStyle;
}

SkPaint::Join StrokeSvgAttributes::getJoinStyle() const {
    return mJoinStyle;
}

QPainter::CompositionMode StrokeSvgAttributes::getOutlineCompositionMode() const {
    return mOutlineCompositionMode;
}

void StrokeSvgAttributes::setLineWidth(const qreal val) {
    mLineWidth = val;
}

void StrokeSvgAttributes::setCapStyle(const SkPaint::Cap capStyle) {
    mCapStyle = capStyle;
}

void StrokeSvgAttributes::setJoinStyle(const SkPaint::Join joinStyle) {
    mJoinStyle = joinStyle;
}

void StrokeSvgAttributes::setOutlineCompositionMode(const QPainter::CompositionMode compMode) {
    mOutlineCompositionMode = compMode;
}

void StrokeSvgAttributes::apply(BoundingBox *box, const qreal scale) const {
    box->strokeWidthAction(QrealAction::sMakeSet(mLineWidth*scale));
    FillSvgAttributes::apply(box, PaintSetting::OUTLINE);
    //box->setStrokePaintType(mPaintType, mColor, mGradient);
}

void BoxSvgAttributes::apply(BoundingBox *box) const {
    if(box->SWT_isPathBox()) {
        const auto path = GetAsPtr(box, PathBox);
        const qreal m11 = mRelTransform.m11();
        const qreal m12 = mRelTransform.m12();
        const qreal m21 = mRelTransform.m21();
        const qreal m22 = mRelTransform.m22();

        const qreal sxAbs = qSqrt(m11*m11 + m21*m21);
        const qreal syAbs = qSqrt(m12*m12 + m22*m22);
        mStrokeAttributes.apply(path, (sxAbs + syAbs)*0.5);
        mFillAttributes.apply(path);
        if(box->SWT_isTextBox()) {
            const auto text = GetAsPtr(box, TextBox);
            text->setFont(mTextAttributes.getFont());
        }
    }
    const auto transAnim = box->getBoxTransformAnimator();
    transAnim->setOpacity(mOpacity);
    transAnim->translate(mDx, mDy);
    transAnim->setScale(mScaleX, mScaleY);
    transAnim->setRotation(mRot);
    transAnim->setShear(mShearX, mShearY);
}

void VectorPathSvgAttributes::apply(SmartVectorPath * const path) {
    SmartPathCollection* const pathAnimator = path->getPathAnimator();
    for(const auto& separatePath : mSvgSeparatePaths) {
        const auto singlePath = SPtrCreate(SmartPathAnimator)();
        separatePath->apply(singlePath.get());
        pathAnimator->addChild(singlePath);
    }

    BoxSvgAttributes::apply(path);
}

void SvgSeparatePath::apply(SmartPathAnimator * const path) {
    for(const auto& point : mPoints) {
        path->actionAddNewAtEnd(point.toNormalNodeData());
    }
    if(mClosedPath) path->actionClose();
}

void SvgSeparatePath::closePath() {
    auto& firstPt = mPoints.first();
    const qreal distBetweenEndPts = pointToLen(mLastPoint->p1() -
                                               firstPt.p1());
    if(mLastPoint->getC0Enabled() && distBetweenEndPts < 0.1) {
        firstPt.setC0(mLastPoint->c0());
        mPoints.removeLast();
        mLastPoint = &mPoints.last();
    }

    mClosedPath = true;
}

void SvgSeparatePath::moveTo(const QPointF &e) {
    addPoint(SvgNormalNode(e));
}

void SvgSeparatePath::cubicTo(const QPointF &c1,
                              const QPointF &c2,
                              const QPointF &e) {
    mLastPoint->setC2(c1);
    SvgNormalNode newPt(e);
    newPt.setC0(c2);
    addPoint(newPt);
}

void SvgSeparatePath::lineTo(const QPointF &e) {
    addPoint(SvgNormalNode(e));
}

void SvgSeparatePath::quadTo(const QPointF &c, const QPointF &e) {
    const QPointF prev = mLastPoint->p1();
    const QPointF c1((prev.x() + 2*c.x()) / 3, (prev.y() + 2*c.y()) / 3);
    const QPointF c2((e.x() + 2*c.x()) / 3, (e.y() + 2*c.y()) / 3);
    cubicTo(c1, c2, e);
}

void SvgSeparatePath::applyTransfromation(const QMatrix &transformation) {
    for(auto& point : mPoints) {
        point.applyTransfromation(transformation);
    }
}

void SvgSeparatePath::pathArc(qreal rX, qreal rY,
                              const qreal xAxisRotation,
                              const int largeArcFlag, const int sweepFlag,
                              const qreal x, const qreal y,
                              const qreal curX, const qreal curY) {
    rX = qAbs(rX);
    rY = qAbs(rY);

    const qreal sin_th = qSin(xAxisRotation * (M_PI / 180));
    const qreal cos_th = qCos(xAxisRotation * (M_PI / 180));

    const qreal dx = (curX - x) / 2;
    const qreal dy = (curY - y) / 2;
    const qreal dx1 =  cos_th * dx + sin_th * dy;
    const qreal dy1 = -sin_th * dx + cos_th * dy;
    const qreal Pr1 = rX * rX;
    const qreal Pr2 = rY * rY;
    const qreal Px = dx1 * dx1;
    const qreal Py = dy1 * dy1;
    /* Spec : check if radii are large enough */
    const qreal check = Px / Pr1 + Py / Pr2;
    if(check > 1) {
        rX = rX * qSqrt(check);
        rY = rY * qSqrt(check);
    }

    const qreal a00 =  cos_th / rX;
    const qreal a01 =  sin_th / rX;
    const qreal a10 = -sin_th / rY;
    const qreal a11 =  cos_th / rY;
    const qreal x0 = a00 * curX + a01 * curY;
    const qreal y0 = a10 * curX + a11 * curY;
    const qreal x1 = a00 * x + a01 * y;
    const qreal y1 = a10 * x + a11 * y;
    /* (x0, y0) is current point in transformed coordinate space.
           (x1, y1) is new point in transformed coordinate space.

           The arc fits a unit-radius circle in this space.
        */
    const qreal d = (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
    qreal sfactor_sq = 1 / d - 0.25;
    if(sfactor_sq < 0) sfactor_sq = 0;
    qreal sfactor = qSqrt(sfactor_sq);
    if(sweepFlag == largeArcFlag) sfactor = -sfactor;
    const qreal xc = 0.5 * (x0 + x1) - sfactor * (y1 - y0);
    const qreal yc = 0.5 * (y0 + y1) + sfactor * (x1 - x0);
    /* (xc, yc) is center of the circle. */

    const qreal th0 = qAtan2(y0 - yc, x0 - xc);
    const qreal th1 = qAtan2(y1 - yc, x1 - xc);

    qreal th_arc = th1 - th0;
    if(th_arc < 0 && sweepFlag)
        th_arc += 2 * M_PI;
    else if(th_arc > 0 && !sweepFlag)
        th_arc -= 2 * M_PI;

    const int n_segs = qCeil(qAbs(th_arc / (M_PI * 0.5 + 0.001)));

    for(int i = 0; i < n_segs; i++) {
        pathArcSegment(xc, yc,
                       th0 + i * th_arc / n_segs,
                       th0 + (i + 1) * th_arc / n_segs,
                       rX, rY, xAxisRotation);
    }
}

void SvgSeparatePath::pathArcSegment(const qreal xc, const qreal yc,
                                     const qreal th0, const qreal th1,
                                     const qreal rx, const qreal ry,
                                     const qreal xAxisRotation) {
    const qreal sinTh = qSin(xAxisRotation * (M_PI / 180));
    const qreal cosTh = qCos(xAxisRotation * (M_PI / 180));

    const qreal a00 =  cosTh * rx;
    const qreal a01 = -sinTh * ry;
    const qreal a10 =  sinTh * rx;
    const qreal a11 =  cosTh * ry;

    const qreal thHalf = 0.5 * (th1 - th0);
    const qreal sinThHalf05 = qSin(thHalf * 0.5);
    const qreal t = 8 * sinThHalf05 * sinThHalf05 / qSin(thHalf) / 3;
    const qreal cosTh0 = qCos(th0);
    const qreal cosTh1 = qCos(th1);
    const qreal sinTh0 = qSin(th0);
    const qreal sinTh1 = qSin(th1);
    const qreal x1 = xc + cosTh0 - t * sinTh0;
    const qreal y1 = yc + sinTh0 + t * cosTh0;
    const qreal x3 = xc + cosTh1;
    const qreal y3 = yc + sinTh1;
    const qreal x2 = x3 + t * sinTh1;
    const qreal y2 = y3 - t * cosTh1;

    cubicTo(QPointF(a00 * x1 + a01 * y1, a10 * x1 + a11 * y1),
            QPointF(a00 * x2 + a01 * y2, a10 * x2 + a11 * y2),
            QPointF(a00 * x3 + a01 * y3, a10 * x3 + a11 * y3));
}

void SvgSeparatePath::addPoint(const SvgNormalNode &point) {
    mPoints << point;
    mLastPoint = &mPoints.last();
}

void TextSvgAttributes::setFontFamily(const QString &family) {
    mFont.setFamily(family);
}

void TextSvgAttributes::setFontSize(const int size) {
    mFont.setPointSize(size > 0 ? size : 1);
}

void TextSvgAttributes::setFontStyle(const QFont::Style &style) {
    mFont.setStyle(style);
}

void TextSvgAttributes::setFontWeight(const int weight) {
    mFont.setWeight(weight);
}

void TextSvgAttributes::setFontAlignment(const Qt::Alignment &alignment) {
    mAlignment = alignment;
}

SvgNormalNode::SvgNormalNode(const QPointF& p1) {
    mC0 = p1;
    mP1 = p1;
    mC2 = p1;
}

void SvgNormalNode::guessCtrlsMode() {
    if(!mC0Enabled || !mC2Enabled) {
        mCtrlsMode = CTRLS_CORNER;
        return;
    }
    const QLineF startLine(mC0, mP1);
    const QLineF endLine(mP1, mC2);
    qreal angle = startLine.angleTo(endLine);
    while(angle > 90) angle -= 180;
    if(isZero1Dec(angle)) {
        const qreal lenDiff = startLine.length() - endLine.length();
        if(isZero1Dec(lenDiff)) mCtrlsMode = CTRLS_SYMMETRIC;
        else mCtrlsMode = CTRLS_SMOOTH;
    } else mCtrlsMode = CTRLS_CORNER;
}

void SvgNormalNode::setC0(const QPointF &c0) {
    mC0 = c0;
    mC0Enabled = !isZero1Dec(pointToLen(mC0 - mP1));
    if(mC0Enabled && mC2Enabled) guessCtrlsMode();
}

void SvgNormalNode::setC2(const QPointF &c2) {
    mC2 = c2;
    mC2Enabled = !isZero1Dec(pointToLen(mC2 - mP1));
    if(mC0Enabled && mC2Enabled) guessCtrlsMode();
}

CtrlsMode SvgNormalNode::getCtrlsMode() const {
    return mCtrlsMode;
}

QPointF SvgNormalNode::p1() const {
    return mP1;
}

QPointF SvgNormalNode::c0() const {
    return mC0;
}

QPointF SvgNormalNode::c2() const {
    return mC2;
}

bool SvgNormalNode::getC0Enabled() const {
    return mC0Enabled;
}

bool SvgNormalNode::getC2Enabled() const {
    return mC2Enabled;
}

void SvgNormalNode::applyTransfromation(const QMatrix &transformation) {
    mC0 = transformation.map(mC0);
    mP1 = transformation.map(mP1);
    mC2 = transformation.map(mC2);
}

NormalNodeData SvgNormalNode::toNormalNodeData() const {
    return { mC0Enabled, mC2Enabled, mCtrlsMode, mC0, mP1, mC2 };
}