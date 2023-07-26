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
        this->setMinimumSize(128, 128);
        this->setMaximumSize(128, 128);
    }
    
    ~ColorWidget()
    {
    }

protected:
    void paintEvent(QPaintEvent* event)
    {
        QPainter painter(this);
        painter.fillRect(rect(), this->color);
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
        this->setMinimumSize(texture->width(), texture->height());
        this->setMaximumSize(texture->width(), texture->height());
    }
    
    ~TexturePreviewer()
    {
        delete texture;
    }

protected:
    void paintEvent(QPaintEvent* event)
    {
        QPainter painter(this);
        painter.drawImage(0, 0, this->texture[0]);
    }
};

#endif // Q_THREEDRENDERINGWIDGETS_H