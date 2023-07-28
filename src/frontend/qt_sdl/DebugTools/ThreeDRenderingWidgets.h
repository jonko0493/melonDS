/*
    Copyright 2016-2022 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef Q_THREEDRENDERINGWIDGETS_H
#define Q_THREEDRENDERINGWIDGETS_H

#include <QImage>
#include <QPainter>
#include <QWidget>

class ColorWidget : public QWidget
{
    Q_OBJECT

public:
    QColor color;

    ColorWidget(QWidget* parent, QColor color) : QWidget(parent)
    {
        this->color = color;
        this->setMinimumSize(32, 32);
        this->setMaximumSize(32, 32);
    }
    
    ~ColorWidget()
    {
    }

protected:
    void paintEvent(QPaintEvent* event)
    {
        QPainter painter(this);
        painter.fillRect(rect(), this->color);
        painter.drawRect(0, 0, this->width() - 1, this->height() - 1);
    }
};

class TexturePreviewer : public QWidget
{
    Q_OBJECT

public:
    QImage* texture;
    QPointF* texCoord = nullptr;
    QVector<QPolygonF>* texPolys = nullptr;

    TexturePreviewer(QWidget* parent, QImage* texture) : QWidget(parent)
    {
        this->texture = texture;
        this->setMinimumSize(texture->width() + 2, texture->height() + 2);
        this->setMaximumSize(texture->width() + 2, texture->height() + 2);
    }
    
    ~TexturePreviewer()
    {
        delete texture;
    }

protected:
    void paintEvent(QPaintEvent* event)
    {
        QPainter painter(this);
        painter.drawImage(1, 1, this->texture[0]);
        painter.drawRect(0, 0, this->width() - 1, this->height() - 1);
        if (texCoord != nullptr)
        {
            texCoord->setX(texCoord->x() + 1);
            texCoord->setY(texCoord->y() + 1);
            painter.setBrush(QBrush(Qt::red));
            painter.drawEllipse(*texCoord, 3.0, 3.0);
        }
        if (texPolys != nullptr)
        {
            painter.setPen(QPen(Qt::red, qreal(2.0)));
            for (int i = 0; i < this->texPolys->size(); i++)
            {
                painter.drawPolyline((*texPolys)[i]);
            }
        }
    }
};

#endif // Q_THREEDRENDERINGWIDGETS_H