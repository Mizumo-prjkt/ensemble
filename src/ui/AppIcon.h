#pragma once

#include <QIcon>
#include <QPixmap>

class AppIcon
{
public:
    static QIcon icon();
    static QPixmap pixmap(int width = 128, int height = 128);
};
