#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

class ToolbarIcons
{
public:
    static QPixmap renderPixmap(int size, const QColor &color, void (*drawFunc)(QPainter &, qreal))
    {
        QPixmap pix(size, size);
        pix.fill(Qt::transparent);

        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Map to 24x24 logical coordinates
        const qreal scale = static_cast<qreal>(size) / 24.0;
        p.scale(scale, scale);

        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        drawFunc(p, 2.0);
        return pix;
    }

    static QIcon makeIcon(const QColor &normalColor, const QColor &activeColor, void (*drawFunc)(QPainter &, qreal))
    {
        QIcon icon;
        for (int s : {20, 24, 32, 48}) {
            icon.addPixmap(renderPixmap(s, normalColor, drawFunc), QIcon::Normal, QIcon::Off);
            icon.addPixmap(renderPixmap(s, activeColor, drawFunc), QIcon::Normal, QIcon::On);
            icon.addPixmap(renderPixmap(s, activeColor, drawFunc), QIcon::Active, QIcon::On);
            icon.addPixmap(renderPixmap(s, activeColor, drawFunc), QIcon::Active, QIcon::Off);
        }
        return icon;
    }

    // --- Draw Functions (24x24 coordinate grid) ---

    // Bold (Lucide bold)
    static void drawBold(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.6);
        p.setPen(pen);

        // Vertical stem
        p.drawLine(QPointF(6, 4), QPointF(6, 20));

        // Top lobe
        QPainterPath top;
        top.moveTo(6, 4);
        top.lineTo(13.5, 4);
        top.cubicTo(17.5, 4, 17.5, 12, 13.5, 12);
        top.lineTo(6, 12);
        p.drawPath(top);

        // Bottom lobe
        QPainterPath bottom;
        bottom.moveTo(6, 12);
        bottom.lineTo(14.5, 12);
        bottom.cubicTo(19, 12, 19, 20, 14.5, 20);
        bottom.lineTo(6, 20);
        p.drawPath(bottom);
    }

    // Italic (Lucide italic)
    static void drawItalic(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.2);
        p.setPen(pen);

        p.drawLine(QPointF(10, 4), QPointF(19, 4));
        p.drawLine(QPointF(5, 20), QPointF(14, 20));
        p.drawLine(QPointF(15, 4), QPointF(9, 20));
    }

    // Underline (Lucide underline)
    static void drawUnderline(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.2);
        p.setPen(pen);

        QPainterPath u;
        u.moveTo(6, 3);
        u.lineTo(6, 11);
        u.cubicTo(6, 17.5, 18, 17.5, 18, 11);
        u.lineTo(18, 3);
        p.drawPath(u);

        p.drawLine(QPointF(4, 21), QPointF(20, 21));
    }

    // Strikethrough (Lucide strikethrough)
    static void drawStrikethrough(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.2);
        p.setPen(pen);

        // S curve
        QPainterPath s;
        s.moveTo(16, 5);
        s.cubicTo(14, 4, 8, 4, 8, 8);
        s.cubicTo(8, 11, 16, 11, 16, 15);
        s.cubicTo(16, 19.5, 9, 20, 7.5, 18);
        p.drawPath(s);

        // Strike bar
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
    }

    // Heading 1 (Lucide heading-1)
    static void drawH1(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // 'H'
        p.drawLine(QPointF(4, 5), QPointF(4, 19));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(4, 12), QPointF(12, 12));

        // '1'
        p.drawLine(QPointF(16, 10), QPointF(18.5, 8));
        p.drawLine(QPointF(18.5, 8), QPointF(18.5, 19));
        p.drawLine(QPointF(16.5, 19), QPointF(20.5, 19));
    }

    // Heading 2 (Lucide heading-2)
    static void drawH2(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // 'H'
        p.drawLine(QPointF(4, 5), QPointF(4, 19));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(4, 12), QPointF(12, 12));

        // '2'
        QPainterPath two;
        two.moveTo(15.5, 10);
        two.cubicTo(16, 7.5, 20.5, 7.5, 20.5, 10.5);
        two.cubicTo(20.5, 13, 16, 15.5, 15.5, 19);
        two.lineTo(21, 19);
        p.drawPath(two);
    }

    // Heading 3 (Lucide heading-3)
    static void drawH3(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // 'H'
        p.drawLine(QPointF(4, 5), QPointF(4, 19));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(4, 12), QPointF(12, 12));

        // '3'
        QPainterPath three;
        three.moveTo(15.5, 8.5);
        three.lineTo(20.5, 8.5);
        three.lineTo(17.5, 13);
        three.cubicTo(20.5, 13, 21, 19, 15.5, 19);
        p.drawPath(three);
    }

    // Paragraph (Pilcrow / P)
    static void drawParagraph(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        QPainterPath pilcrow;
        pilcrow.moveTo(12, 4);
        pilcrow.lineTo(18, 4);
        pilcrow.moveTo(14, 4);
        pilcrow.lineTo(14, 20);
        pilcrow.moveTo(18, 4);
        pilcrow.lineTo(18, 20);
        pilcrow.moveTo(14, 4);
        pilcrow.cubicTo(7, 4, 7, 12, 14, 12);
        p.drawPath(pilcrow);
    }

    // Link (Lucide link)
    static void drawLink(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // Connecting bar
        p.drawLine(QPointF(9, 15), QPointF(15, 9));

        // Bottom-left link loop
        QPainterPath l1;
        l1.moveTo(11, 17);
        l1.lineTo(8.5, 19.5);
        l1.cubicTo(6, 22, 2, 18, 4.5, 15.5);
        l1.lineTo(7, 13);
        p.drawPath(l1);

        // Top-right link loop
        QPainterPath l2;
        l2.moveTo(13, 7);
        l2.lineTo(15.5, 4.5);
        l2.cubicTo(18, 2, 22, 6, 19.5, 8.5);
        l2.lineTo(17, 11);
        p.drawPath(l2);
    }

    // Horizontal Rule (Lucide separator-horizontal)
    static void drawHorizontalRule(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.2);
        p.setPen(pen);

        // Center solid divider line
        p.drawLine(QPointF(3, 12), QPointF(21, 12));

        // Subtle top and bottom indicators
        pen.setWidthF(1.5);
        p.setPen(pen);
        p.drawLine(QPointF(7, 7), QPointF(17, 7));
        p.drawLine(QPointF(7, 17), QPointF(17, 17));
    }

    // Blockquote (Lucide quote)
    static void drawBlockquote(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // Left quote mark
        QPainterPath q1;
        q1.moveTo(4, 11);
        q1.lineTo(8, 11);
        q1.lineTo(8, 7);
        q1.lineTo(4, 7);
        q1.lineTo(4, 11);
        q1.cubicTo(4, 16, 7, 18, 8, 19);
        p.drawPath(q1);

        // Right quote mark
        QPainterPath q2;
        q2.moveTo(13, 11);
        q2.lineTo(17, 11);
        q2.lineTo(17, 7);
        q2.lineTo(13, 7);
        q2.lineTo(13, 11);
        q2.cubicTo(13, 16, 16, 18, 17, 19);
        p.drawPath(q2);
    }

    // Bullet List (Lucide list)
    static void drawBulletList(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        p.drawLine(QPointF(10, 6), QPointF(20, 6));
        p.drawLine(QPointF(10, 12), QPointF(20, 12));
        p.drawLine(QPointF(10, 18), QPointF(20, 18));

        p.setBrush(pen.color());
        p.drawEllipse(QPointF(4.5, 6), 1.5, 1.5);
        p.drawEllipse(QPointF(4.5, 12), 1.5, 1.5);
        p.drawEllipse(QPointF(4.5, 18), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    }

    // Numbered List (Lucide list-ordered)
    static void drawNumberedList(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(1.8);
        p.setPen(pen);

        // '1'
        p.drawLine(QPointF(3.5, 4.5), QPointF(5, 3.5));
        p.drawLine(QPointF(5, 3.5), QPointF(5, 8.5));
        p.drawLine(QPointF(3, 8.5), QPointF(6.5, 8.5));

        // '2'
        QPainterPath two;
        two.moveTo(3.5, 12);
        two.cubicTo(3.5, 10.5, 6.5, 10.5, 6.5, 12.5);
        two.cubicTo(6.5, 14, 3.5, 15, 3.5, 16.5);
        two.lineTo(6.5, 16.5);
        p.drawPath(two);

        // Lines
        pen.setWidthF(2.0);
        p.setPen(pen);
        p.drawLine(QPointF(10, 6), QPointF(20, 6));
        p.drawLine(QPointF(10, 12), QPointF(20, 12));
        p.drawLine(QPointF(10, 18), QPointF(20, 18));
    }

    // Image (Lucide image)
    static void drawImage(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // Outer rounded rect
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 2.5, 2.5);

        // Sun
        p.drawEllipse(QPointF(8.5, 8.5), 1.8, 1.8);

        // Mountains
        QPainterPath m;
        m.moveTo(21, 15);
        m.lineTo(16, 10);
        m.lineTo(12, 14);
        m.lineTo(9, 11);
        m.lineTo(3, 17);
        p.drawPath(m);
    }

    // Code (Lucide code)
    static void drawCode(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.0);
        p.setPen(pen);

        // '<'
        p.drawLine(QPointF(7, 7), QPointF(3, 12));
        p.drawLine(QPointF(3, 12), QPointF(7, 17));

        // '>'
        p.drawLine(QPointF(17, 7), QPointF(21, 12));
        p.drawLine(QPointF(21, 12), QPointF(17, 17));

        // '/'
        p.drawLine(QPointF(13, 5), QPointF(11, 19));
    }

    // Plus (Lucide plus)
    static void drawPlus(QPainter &p, qreal)
    {
        QPen pen = p.pen();
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    }

    // --- High Level Builders ---

    static QIcon bold(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawBold); }
    static QIcon italic(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawItalic); }
    static QIcon underline(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawUnderline); }
    static QIcon strikethrough(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawStrikethrough); }
    static QIcon heading1(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawH1); }
    static QIcon heading2(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawH2); }
    static QIcon heading3(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawH3); }
    static QIcon paragraph(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawParagraph); }
    static QIcon link(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawLink); }
    static QIcon horizontalRule(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawHorizontalRule); }
    static QIcon blockquote(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawBlockquote); }
    static QIcon bulletList(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawBulletList); }
    static QIcon numberedList(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawNumberedList); }
    static QIcon image(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawImage); }
    static QIcon code(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawCode); }
    static QIcon plus(const QColor &normal, const QColor &active) { return makeIcon(normal, active, drawPlus); }
};
