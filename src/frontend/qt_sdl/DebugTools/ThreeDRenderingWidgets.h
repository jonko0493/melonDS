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

    TexturePreviewer(QWidget* parent, QImage* texture) : QWidget(parent)
    {
        this->texture = texture;
        this->setMinimumSize(texture->width() + 1, texture->height() + 1);
        this->setMaximumSize(texture->width() + 1, texture->height() + 1);
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
    }
};

#endif // Q_THREEDRENDERINGWIDGETS_H