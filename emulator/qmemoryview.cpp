﻿#include "stdafx.h"
#include <QtGui>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionFocusRect>
#include <QToolBar>
#include "main.h"
#include "qmemoryview.h"
#include "Emulator.h"
#include "emubase/Emubase.h"
#include "qdialogs.h"


enum MemoryViewMode
{
    MEMMODE_CPU = 0,   // CPU memory
    MEMMODE_HALT = 1,  // HALT mode memory
    MEMMODE_USER = 2,  // USER mode memory
    MEMMODE_LAST = 2,  // Last mode number
};

static const char * MemoryView_ModeNames[] =
{
    "CPU", "HALT", "USER"
};


QMemoryView::QMemoryView()
{
    m_Mode = Settings_GetDebugMemoryMode();
    if (m_Mode > MEMMODE_LAST) m_Mode = MEMMODE_LAST;
    m_ByteMode = Settings_GetDebugMemoryByte();
    m_wBaseAddress = Settings_GetDebugMemoryAddress();
    m_cyLineMemory = 0;
    m_nPageSize = 0;

    QFont font = Common_GetMonospacedFont();
    QFontMetrics fontmetrics(font);
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();

    m_cyLine = cyLine;

    this->setFont(font);
    this->setMinimumSize(cxChar * 68, cyLine * 9 + cyLine / 2);

    m_scrollbar = new QScrollBar(Qt::Vertical, this);
    m_scrollbar->setRange(0, 65536 - 16);
    m_scrollbar->setSingleStep(16);
    QObject::connect(m_scrollbar, SIGNAL(valueChanged(int)), this, SLOT(scrollValueChanged()));

    m_toolbar = new QToolBar(this);
    m_toolbar->setGeometry(4, 4, 36, 2000);
    m_toolbar->setOrientation(Qt::Vertical);
    m_toolbar->setIconSize(QSize(24, 24));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setFocusPolicy(Qt::NoFocus);
    m_toolbar->setStyle(QStyleFactory::create("windows"));  // fix for macOS to remove gradient background

    QAction* actionGotoAddr = m_toolbar->addAction(QIcon(":/images/iconEditAddress.svg"), "");
    m_toolbar->addSeparator();
    QAction* actionWordByte = m_toolbar->addAction(QIcon(":/images/iconWordByte.svg"), "");

    QObject::connect(actionGotoAddr, SIGNAL(triggered()), this, SLOT(gotoAddress()));
    QObject::connect(actionWordByte, SIGNAL(triggered()), this, SLOT(changeWordByteMode()));

    setFocusPolicy(Qt::ClickFocus);
}

QMemoryView::~QMemoryView()
{
    delete m_scrollbar;
}

void QMemoryView::updateScrollPos()
{
    m_scrollbar->setValue(m_wBaseAddress);
}

static const char * GetMemoryModeName(int mode)
{
    if (mode < 0 || mode > MEMMODE_LAST)
        return "UKWN";  // Unknown mode
    return MemoryView_ModeNames[mode];
}

void QMemoryView::updateWindowText()
{
    QString buffer = tr("Memory - %1").arg(GetMemoryModeName(m_Mode));
    parentWidget()->setWindowTitle(buffer);
}

void QMemoryView::updateData()
{
}

void QMemoryView::focusInEvent(QFocusEvent *)
{
    repaint();  // Need to draw focus rect
}
void QMemoryView::focusOutEvent(QFocusEvent *)
{
    repaint();  // Need to draw focus rect
}

void QMemoryView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(tr("Go to Address..."), this, SLOT(gotoAddress()));
    menu.addSeparator();

    for (int mode = 0; mode <= MEMMODE_LAST; mode++)
    {
        const char * modeName = MemoryView_ModeNames[mode];
        QAction * action = menu.addAction(modeName, this, SLOT(changeMemoryMode()));
        action->setCheckable(true);
        action->setData(mode);
        if (m_Mode == mode)
            action->setChecked(true);
    }

    menu.addSeparator();
    menu.addAction(tr("Words / Bytes"), this, SLOT(changeWordByteMode()));

    menu.exec(event->globalPos());
}

void QMemoryView::changeMemoryMode()
{
    QAction * action = qobject_cast<QAction*>(sender());
    if (action == nullptr) return;
    int mode = action->data().toInt();
    if (mode < 0 || mode > MEMMODE_LAST) return;

    m_Mode = mode;
    Settings_SetDebugMemoryMode(m_Mode);

    repaint();
    updateWindowText();
}

void QMemoryView::changeWordByteMode()
{
    m_ByteMode = !m_ByteMode;
    Settings_SetDebugMemoryByte(m_ByteMode);

    repaint();
}

void QMemoryView::scrollBy(qint16 delta)
{
    if (delta == 0) return;

    m_wBaseAddress = (quint16)(m_wBaseAddress + delta);
    m_wBaseAddress = m_wBaseAddress & ((quint16)~15);
    Settings_SetDebugMemoryAddress(m_wBaseAddress);

    repaint();
    updateScrollPos();
}

void QMemoryView::gotoAddress()
{
    quint16 value = m_wBaseAddress;
    QInputOctalDialog dialog(this, tr("Go To Address"), tr("Address (octal):"), &value);
    if (dialog.exec() == QDialog::Rejected) return;

    // Scroll to the address
    m_wBaseAddress = value & ((quint16)~15);
    Settings_SetDebugMemoryAddress(m_wBaseAddress);

    repaint();
    updateScrollPos();
}

void QMemoryView::resizeEvent(QResizeEvent *)
{
    int cxScroll = this->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    m_scrollbar->setGeometry(this->width() - cxScroll, 0, cxScroll, this->height());
    m_scrollbar->setPageStep((this->height() / m_cyLine - 2) * 16);
}

void QMemoryView::scrollValueChanged()
{
    int value = m_scrollbar->value();
    m_wBaseAddress = (unsigned short)value & ((quint16)~15);
    Settings_SetDebugMemoryAddress(m_wBaseAddress);

    this->repaint();
}

quint16 QMemoryView::getWordFromMemory(quint16 address, bool& okValid, int& addrtype, quint16& wChanged)
{
    bool okHalt;
    switch (m_Mode)
    {
    default:
    case MEMMODE_CPU:
        okHalt = g_pBoard->GetCPU()->IsHaltMode();
        break;
    case MEMMODE_HALT:
        okHalt = true;
        break;
    case MEMMODE_USER:
        okHalt = false;
        break;
    }

    quint32 offset;
    addrtype = g_pBoard->TranslateAddress(address, okHalt, false, &offset);
    okValid = (addrtype != ADDRTYPE_IO) && (addrtype != ADDRTYPE_DENY);
    if (!okValid)
        return 0;

    if (addrtype == ADDRTYPE_ROM)
    {
        wChanged = 0;
        return g_pBoard->GetROMWord((uint16_t)offset);
    }
    else
    {
        wChanged = Emulator_GetChangeRamStatus(offset);
        return g_pBoard->GetRAMWordView(offset);
    }
}

void QMemoryView::paintEvent(QPaintEvent * /*event*/)
{
    if (g_pBoard == nullptr) return;

    QColor colorBackground = palette().color(QPalette::Base);

    QPainter painter(this);
    painter.fillRect(36 + 4, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics(font);
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    QColor colorText = palette().color(QPalette::Text);
    QColor colorChanged = Common_GetColorShifted(palette(), COLOR_VALUECHANGED);
    QColor colorMemoryRom = Common_GetColorShifted(palette(), COLOR_MEMORYROM);
    QColor colorMemoryIO = Common_GetColorShifted(palette(), COLOR_MEMORYIO);
    QColor colorMemoryNA = Common_GetColorShifted(palette(), COLOR_MEMORYNA);

    m_cyLineMemory = cyLine;

    char buffer[7];
    const char * ADDRESS_LINE = "   addr";
    painter.drawText(30, cyLine, ADDRESS_LINE);
    for (int j = 0; j < 8; j++)
    {
        _snprintf(buffer, 7, "%d", j * 2);
        painter.drawText(38 + (9 + j * 7) * cxChar, cyLine, buffer);
    }

    // Calculate m_nPageSize
    m_nPageSize = this->height() / cyLine - 1;

    quint16 address = m_wBaseAddress;
    int y = 2 * cyLine;
    for (;;)    // Draw lines
    {
        DrawOctalValue(painter, 38 + 1 * cxChar, y, address);

        int x = 38 + 9 * cxChar;
        ushort wchars[16];

        for (int j = 0; j < 8; j++)    // Draw words as octal value
        {
            int addrtype;
            bool okValid = false;
            quint16 wChanged = 0;
            quint16 word = getWordFromMemory(address, okValid, addrtype, wChanged);

            if (okValid)
            {
                if (addrtype == ADDRTYPE_ROM)
                    painter.setPen(colorMemoryRom);
                else
                    painter.setPen(wChanged != 0 ? colorChanged : colorText);
                if (m_ByteMode)
                {
                    PrintOctalValue(buffer, (word & 0xff));
                    painter.drawText(x, y, buffer + 3);
                    PrintOctalValue(buffer, (word >> 8));
                    painter.drawText(x + 3 * cxChar + cxChar / 2, y, buffer + 3);
                }
                else
                    DrawOctalValue(painter, x, y, word);
            }
            else  // No value
            {
                if (addrtype == ADDRTYPE_IO)
                {
                    painter.setPen(colorMemoryIO);
                    painter.drawText(x, y, "  IO  ");
                }
                else
                {
                    painter.setPen(colorMemoryNA);
                    painter.drawText(x, y, "  NA  ");
                }
            }

            // Prepare characters to draw at right
            quint8 ch1 = (quint8)(word & 0xff);
            ushort wch1 = Translate_KOI8R(ch1);
            if (ch1 < 32) wch1 = 0x00b7;
            wchars[j * 2] = wch1;
            quint8 ch2 = (quint8)((word >> 8) & 0xff);
            ushort wch2 = Translate_KOI8R(ch2);
            if (ch2 < 32) wch2 = 0x00b7;
            wchars[j * 2 + 1] = wch2;

            address += 2;
            x += 7 * cxChar;
        }
        painter.setPen(colorText);

        // Draw characters at right
        int xch = x + cxChar;
        QString wstr = QString::fromUtf16(wchars, 16);
        painter.drawText(xch, y, wstr);

        y += cyLine;
        if (y > this->height()) break;
    }  // Draw lines

    // Draw focus rect
    if (hasFocus())
    {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.state |= QStyle::State_KeyboardFocusChange;
        option.backgroundColor = QColor(Qt::gray);
        option.rect = QRect(38, cyLine + fontmetrics.descent(), 83 * cxChar, cyLine * m_nPageSize);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
    }
}

void QMemoryView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Space:
        if (event->isAutoRepeat()) return;
        event->accept();
        if (m_Mode == MEMMODE_LAST)
            m_Mode = 0;
        else
            m_Mode++;
        Settings_SetDebugMemoryMode(m_Mode);
        this->repaint();
        updateWindowText();
        break;

    case Qt::Key_G:
        event->accept();
        gotoAddress();
        break;
    case Qt::Key_B:
        event->accept();
        changeWordByteMode();
        break;

    case Qt::Key_Up:
        event->accept();
        scrollBy(-16);
        break;
    case Qt::Key_Down:
        event->accept();
        scrollBy(16);
        break;

    case Qt::Key_PageUp:
        event->accept();
        scrollBy(-m_nPageSize * 16);
        break;
    case Qt::Key_PageDown:
        event->accept();
        scrollBy(m_nPageSize * 16);
        break;
    }
}

void QMemoryView::wheelEvent(QWheelEvent * event)
{
    if (event->orientation() == Qt::Horizontal)
        return;
    event->accept();

    int steps = -event->delta() / 60;
    scrollBy(steps * 16);
}


//////////////////////////////////////////////////////////////////////
