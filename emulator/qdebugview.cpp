﻿#include "stdafx.h"
#include <QtGui>
#include <QMenu>
#include <QStyleFactory>
#include <QStyleOptionFocusRect>
#include <QToolBar>
#include "main.h"
#include "mainwindow.h"
#include "qdebugview.h"
#include "Emulator.h"
#include "emubase/Emubase.h"


//////////////////////////////////////////////////////////////////////


QDebugView::QDebugView(QWidget *mainWindow) :
    QWidget()
{
    QFont font = Common_GetMonospacedFont();
    QFontMetrics fontmetrics(font);
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    int cyHeight = cyLine * 16 + cyLine / 2;
    this->setMinimumSize(36 + cxChar * 33, cyHeight);
    this->setMaximumHeight(cyHeight);

    m_toolbar = new QToolBar(this);
    m_toolbar->setGeometry(0, 0, 36, cyHeight);
    m_toolbar->setOrientation(Qt::Vertical);
    m_toolbar->setIconSize(QSize(24, 24));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setFocusPolicy(Qt::NoFocus);
    m_toolbar->setStyle(QStyleFactory::create("windows"));  // fix for macOS to remove gradient background

    int x = 36 + 4;
    int cxProc = cxChar * 33;
    m_procCtrl = new QDebugProcessorCtrl(this);
    m_procCtrl->setGeometry(x, 0, cxProc, cyHeight);
    x += cxProc + 4;
    int cxStack = cxChar * 17 + cxChar / 2;
    m_stackCtrl = new QDebugStackCtrl(this);
    m_stackCtrl->setGeometry(x, 0, cxStack, cyHeight);
    x += cxStack + 4;
    int cxPorts = cxChar * 25;
    m_portsCtrl = new QDebugPortsCtrl(this);
    m_portsCtrl->setGeometry(x, 0, cxPorts, cyHeight);
    x += cxPorts + 4;
    int cxBreaks = cxChar * 9;
    m_breaksCtrl = new QDebugBreakpointsCtrl(this);
    m_breaksCtrl->setGeometry(x, 0, cxBreaks, cyHeight);
    x += cxBreaks + 4;
    int cxMemmap = cxChar * 25;
    m_memmapCtrl = new QDebugMemoryMapCtrl(this);
    m_memmapCtrl->setGeometry(x, 0, cxMemmap, cyHeight);

    QAction* actionStepInto = m_toolbar->addAction(QIcon(":/images/iconStepInto.svg"), "");
    QAction* actionStepOver = m_toolbar->addAction(QIcon(":/images/iconStepOver.svg"), "");

    QObject::connect(actionStepInto, SIGNAL(triggered()), mainWindow, SLOT(debugStepInto()));
    QObject::connect(actionStepOver, SIGNAL(triggered()), mainWindow, SLOT(debugStepOver()));

    setFocusPolicy(Qt::ClickFocus);
}

void QDebugView::updateWindowText()
{
    QString buffer = tr("Debug");
    parentWidget()->setWindowTitle(buffer);
}

CProcessor* QDebugView::getCurrentProc() const
{
    return g_pBoard->GetCPU();
}

// Update after Run or Step
void QDebugView::updateData()
{
    m_procCtrl->updateData();
    m_stackCtrl->updateData();
    m_portsCtrl->updateData();
    m_breaksCtrl->updateData();
    m_memmapCtrl->updateData();
}

void QDebugView::focusInEvent(QFocusEvent *)
{
    repaint();  // Need to draw focus rect
}
void QDebugView::focusOutEvent(QFocusEvent *)
{
    repaint();  // Need to draw focus rect
}

void QDebugView::contextMenuEvent(QContextMenuEvent * /*event*/)
{
//    QMenu menu(this);
//    //TODO
//    menu.exec(event->globalPos());
}

void QDebugView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);

    // Draw focus rect
    if (hasFocus())
    {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.state |= QStyle::State_KeyboardFocusChange;
        option.backgroundColor = QColor(Qt::gray);
        option.rect = this->rect();
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
    }
}

//////////////////////////////////////////////////////////////////////

QDebugCtrl::QDebugCtrl(QDebugView *debugView)
{
    m_pDebugView = debugView;
    setParent(debugView);
    memset(&m_lastHitTest, 0, sizeof(m_lastHitTest));
}

DebugCtrlHitTest QDebugCtrl::hitTest(int /*x*/, int y)
{
    QFont font = Common_GetMonospacedFont();
    QFontMetrics fontmetrics(font);
    int cyLine = fontmetrics.height();

    DebugCtrlHitTest hit;
    hit.isValid = false;
    hit.address = hit.value = 0;
    hit.line = (y - cyLine / 3) / cyLine;

    m_lastHitTest = hit;
    return hit;
}

void QDebugCtrl::copyAddressOctal()
{
    if (!m_lastHitTest.isValid)
        return;
    CopyWordOctalToClipboard(m_lastHitTest.address);
}

void QDebugCtrl::copyValueOctal()
{
    if (!m_lastHitTest.isValid)
        return;
    CopyWordOctalToClipboard(m_lastHitTest.value);
}

void QDebugCtrl::copyValueHex()
{
    if (!m_lastHitTest.isValid)
        return;
    CopyWordHexToClipboard(m_lastHitTest.value);
}

void QDebugCtrl::copyValueBinary()
{
    if (!m_lastHitTest.isValid)
        return;
    CopyWordBinaryToClipboard(m_lastHitTest.value);
}

//////////////////////////////////////////////////////////////////////

QDebugProcessorCtrl::QDebugProcessorCtrl(QDebugView *debugView)
    : QDebugCtrl(debugView)
{
    memset(m_wDebugCpuR, 0, sizeof(m_wDebugCpuR));
    memset(m_okDebugCpuRChanged, 0, sizeof(m_okDebugCpuRChanged));
}

void QDebugProcessorCtrl::paintEvent(QPaintEvent * /*event*/)
{
    const CProcessor* pProc = getProc();

    quint16* arrR = m_wDebugCpuR;
    bool* arrRChanged = m_okDebugCpuRChanged;

    QColor colorBackground = palette().color(QPalette::Base);
    QPainter painter(this);
    painter.fillRect(0, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics = painter.fontMetrics();
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    QColor colorText = palette().color(QPalette::Text);
    QColor colorChanged = Common_GetColorShifted(palette(), COLOR_VALUECHANGED);

    int x = cxChar * 1, y = 0;

    // Registers
    for (int r = 0; r < 8; r++)
    {
        painter.setPen(QColor(arrRChanged[r] ? colorChanged : colorText));

        const char * strRegName = REGISTER_NAME[r];
        painter.drawText(x, y + (1 + r) * cyLine, strRegName);

        quint16 value = arrR[r]; //pProc->GetReg(r);
        DrawOctalValue(painter, x + cxChar * 3, y + (1 + r) * cyLine, value);
        DrawHexValue(painter, x + cxChar * 10, y + (1 + r) * cyLine, value);
        DrawBinaryValue(painter, x + cxChar * 15, y + (1 + r) * cyLine, value);
    }
    painter.setPen(colorText);

    // CPC value
    painter.drawText(x, y + 9 * cyLine, "PC'");
    quint16 cpc = pProc->GetCPC();
    DrawOctalValue(painter, x + cxChar * 3, y + 9 * cyLine, cpc);
    DrawHexValue(painter, x + cxChar * 10, y + 9 * cyLine, cpc);
    DrawBinaryValue(painter, x + cxChar * 15, y + 9 * cyLine, cpc);

    // PSW value
    painter.setPen(QColor(arrRChanged[8] ? colorChanged : colorText));
    painter.drawText(x, y + 11 * cyLine, "PS");
    quint16 psw = arrR[8]; // pProc->GetPSW();
    DrawOctalValue(painter, x + cxChar * 3, y + 11 * cyLine, psw);
    //DrawHexValue(painter, x + cxChar * 10, y + 11 * cyLine, psw);
    painter.drawText(x + cxChar * 15, y + 10 * cyLine, "       HP  TNZVC");
    DrawBinaryValue(painter, x + cxChar * 15, y + 11 * cyLine, psw);

    painter.setPen(colorText);

    // CPSW value
    painter.drawText(x, y + 12 * cyLine, "PS'");
    quint16 cpsw = pProc->GetCPSW();
    DrawOctalValue(painter, x + cxChar * 3, y + 12 * cyLine, cpsw);
    DrawBinaryValue(painter, x + cxChar * 15, y + 12 * cyLine, cpsw);

    // Processor mode - HALT or USER
    bool okHaltMode = pProc->IsHaltMode();
    painter.drawText(x, y + 14 * cyLine, okHaltMode ? "HALT" : "USER");

    // "Stopped" flag
    bool okStopped = pProc->IsStopped();
    if (okStopped)
        painter.drawText(x + 6 * cxChar, y + 14 * cyLine, "STOP");
}

void QDebugProcessorCtrl::updateData()
{
    const CProcessor* pCPU = g_pBoard->GetCPU();
    ASSERT(pCPU != nullptr);

    // Get new register values and set change flags
    for (int r = 0; r < 8; r++)
    {
        quint16 value = pCPU->GetReg(r);
        m_okDebugCpuRChanged[r] = (m_wDebugCpuR[r] != value);
        m_wDebugCpuR[r] = value;
    }
    quint16 pswCPU = pCPU->GetPSW();
    m_okDebugCpuRChanged[8] = (m_wDebugCpuR[8] != pswCPU);
    m_wDebugCpuR[8] = pswCPU;
}

DebugCtrlHitTest QDebugProcessorCtrl::hitTest(int x, int y)
{
    DebugCtrlHitTest hit = QDebugCtrl::hitTest(x, y);
    if (hit.line < 0 || hit.line == 9 || hit.line > 11)
        return hit;  // Invalid line number
    hit.isValid = true;

    const CProcessor* pProc = getProc();
    if (hit.line < 8)
        hit.value = pProc->GetReg(hit.line);
    else if (hit.line == 8)
        hit.value = pProc->GetCPC();
    else if (hit.line == 10)
        hit.value = pProc->GetPSW();
    else if (hit.line == 11)
        hit.value = pProc->GetCPSW();

    return hit;
}

void QDebugProcessorCtrl::contextMenuEvent(QContextMenuEvent *event)
{
    DebugCtrlHitTest hit = hitTest(event->x(), event->y());
    if (!hit.isValid)
        return;
    m_lastHitTest = hit;

    char buffer[7], bufferHex[5], bufferBin[17];
    PrintOctalValue(buffer, hit.value);
    PrintHexValue(bufferHex, hit.value);
    PrintBinaryValue(bufferBin, hit.value);

    QMenu menu(this);
    menu.addAction(tr("Copy Value %1").arg(buffer), this, SLOT(copyValueOctal()));
    if (hit.line < 10)
        menu.addAction(tr("Copy Value %1").arg(bufferHex), this, SLOT(copyValueHex()));
    menu.addAction(tr("Copy Value %1").arg(bufferBin), this, SLOT(copyValueBinary()));
    menu.exec(event->globalPos());
}

//////////////////////////////////////////////////////////////////////

QDebugStackCtrl::QDebugStackCtrl(QDebugView *debugView)
    : QDebugCtrl(debugView)
{
    m_wDebugCpuR6Old = 0;
}

void QDebugStackCtrl::paintEvent(QPaintEvent * /*event*/)
{
    QColor colorBackground = palette().color(QPalette::Base);
    QPainter painter(this);
    painter.fillRect(0, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics = painter.fontMetrics();
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    QColor colorText = palette().color(QPalette::Text);
    QColor colorChanged = Common_GetColorShifted(palette(), COLOR_VALUECHANGED);
    QColor colorPrev = Common_GetColorShifted(palette(), COLOR_PREVIOUS);

    int x = cxChar / 2, y = cyLine;

    CProcessor* pProc = getProc();
    int reg = 6;
    quint16 oldValue = m_wDebugCpuR6Old;

    quint16 current = pProc->GetReg(reg);
    quint16 previous = oldValue;
    bool okExec = false; //(reg == 7);

    // Reading from memory into the buffer
    quint16 memory[16];
    int addrtype[16];
    for (int idx = 0; idx < 16; idx++)
    {
        memory[idx] = g_pBoard->GetWordView(
                current + idx * 2 - 16, pProc->IsHaltMode(), okExec, addrtype + idx);
    }

    quint16 address = current - 16;
    for (int index = 0; index < 16; index++)    // Draw strings
    {
        // Address
        painter.setPen(colorText);
        DrawOctalValue(painter, x + 3 * cxChar, y, address);

        // Value at the address
        quint16 value = memory[index];
        quint16 wChanged = Emulator_GetChangeRamStatus(address);
        painter.setPen(wChanged != 0 ? colorChanged : colorText);
        DrawOctalValue(painter, x + 10 * cxChar, y, value);

        // Current position
        if (address == current)
        {
            painter.setPen(colorText);
            painter.drawText(x + 2 * cxChar, y, ">");
            painter.setPen(current != previous ? colorChanged : colorText);
            painter.drawText(x, y, REGISTER_NAME[reg]);
        }
        else if (address == previous)
        {
            painter.setPen(colorPrev);
            painter.drawText(x + 2 * cxChar, y, ">");
        }

        address += 2;
        y += cyLine;
    }

    painter.setPen(colorText);
}

void QDebugStackCtrl::updateData()
{
    m_wDebugCpuR6Old = g_pBoard->GetCPU()->GetSP();
}

DebugCtrlHitTest QDebugStackCtrl::hitTest(int x, int y)
{
    DebugCtrlHitTest hit = QDebugCtrl::hitTest(x, y);
    if (hit.line < 0 || hit.line > 15)
        return hit;  // Invalid line number
    hit.isValid = true;

    const CProcessor* pProc = getProc();

    hit.address = pProc->GetSP() - 16 + hit.line * 2;

    int addrtype;
    hit.value = g_pBoard->GetWordView(hit.address, pProc->IsHaltMode(), false, &addrtype);

    return hit;
}

void QDebugStackCtrl::contextMenuEvent(QContextMenuEvent *event)
{
    DebugCtrlHitTest hit = hitTest(event->x(), event->y());
    if (!hit.isValid)
        return;
    m_lastHitTest = hit;

    char bufaddr[7];
    PrintOctalValue(bufaddr, hit.address);
    char bufval[7];
    PrintOctalValue(bufval, hit.value);

    QMenu menu(this);
    menu.addAction(tr("Copy Address %1").arg(bufaddr), this, SLOT(copyAddressOctal()));
    menu.addAction(tr("Copy Value %1").arg(bufval), this, SLOT(copyValueOctal()));
    menu.exec(event->globalPos());
}

//////////////////////////////////////////////////////////////////////

QDebugPortsCtrl::QDebugPortsCtrl(QDebugView *debugView)
    : QDebugCtrl(debugView)
{
}

void QDebugPortsCtrl::paintEvent(QPaintEvent * /*event*/)
{
    QColor colorBackground = palette().color(QPalette::Base);
    QPainter painter(this);
    painter.fillRect(0, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics = painter.fontMetrics();
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();

    int x = cxChar, y = cyLine;

    painter.drawText(x, y, tr("Ports"));

    quint16 value176640 = g_pBoard->GetPortView(0176640);
    DrawOctalValue(painter, x + 0 * cxChar, y + 1 * cyLine, 0176640);
    DrawOctalValue(painter, x + 7 * cxChar, y + 1 * cyLine, value176640);
    quint16 value176642 = g_pBoard->GetPortView(0176642);
    DrawOctalValue(painter, x + 0 * cxChar, y + 2 * cyLine, 0176642);
    DrawOctalValue(painter, x + 7 * cxChar, y + 2 * cyLine, value176642);
}

//////////////////////////////////////////////////////////////////////

QDebugBreakpointsCtrl::QDebugBreakpointsCtrl(QDebugView *debugView)
    : QDebugCtrl(debugView)
{
}

void QDebugBreakpointsCtrl::paintEvent(QPaintEvent * /*event*/)
{
    QColor colorBackground = palette().color(QPalette::Base);
    QPainter painter(this);
    painter.fillRect(0, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics = painter.fontMetrics();
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();

    int x = cxChar / 2, y = cyLine;

    painter.drawText(x, y, tr("Breakpts"));

    const quint16* pbps = Emulator_GetCPUBreakpointList();
    if (*pbps == 0177777)
        return;

    y += cyLine;
    while (*pbps != 0177777)
    {
        DrawOctalValue(painter, x + cxChar, y, *pbps);
        y += cyLine;
        pbps++;
    }
}

void QDebugBreakpointsCtrl::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(tr("Remove All Breakpoints"), this, SLOT(removeAllBreakpoints()));
    menu.exec(event->globalPos());
}

void QDebugBreakpointsCtrl::removeAllBreakpoints()
{
    Global_getMainWindow()->debugRemoveAllBreakpoints();
}

//////////////////////////////////////////////////////////////////////

QDebugMemoryMapCtrl::QDebugMemoryMapCtrl(QDebugView *debugView)
    : QDebugCtrl(debugView)
{
}

void QDebugMemoryMapCtrl::paintEvent(QPaintEvent * /*event*/)
{
    const CProcessor* pProc = getProc();

    QColor colorBackground = palette().color(QPalette::Base);
    QPainter painter(this);
    painter.fillRect(0, 0, this->width(), this->height(), colorBackground);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics = painter.fontMetrics();
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();

    int x = cxChar, y = 0;
    int x1 = x + cxChar * 7 - cxChar / 2;
    int y1 = y + cxChar / 2;
    int x2 = x1 + cxChar * 13;
    int y2 = y1 + cyLine * 16;
    int xtype = x1 + cxChar * 3;
    int ybase = y + cyLine * 16;

    painter.drawRect(x1, y1, x2 - x1, y2 - y1);

    bool okHaltMode = pProc->IsHaltMode();
    painter.drawText(x, y + cyLine, okHaltMode ? "HALT" : "USER");

    uint16_t portBaseAddr = pProc->IsHaltMode() ? 0161200 : 0161220;
    for (int i = 0; i < 8; i++)
    {
        int yp = y2 - i * cyLine * 2;
        painter.drawLine(x1, yp, x2, yp);
        quint16 addr = (quint16)i * 020000;
        DrawOctalValue(painter, x, yp + cyLine / 4, addr);
    }

    for (int i = 0; i < 7; i++)
    {
        int ytype = ybase - cyLine * i * 2 - cyLine / 3;
        if (i < 2 && pProc->IsHaltMode())
            painter.drawText(xtype, ytype, tr("ROM"));
        else
        {
            uint16_t value = g_pBoard->GetPortView((uint16_t)(portBaseAddr + 2 * i));
            TCHAR buffer[7];
            PrintOctalValue(buffer, (value & 0037760) >> 4);
            painter.drawText(xtype, ytype, buffer + 2);
            if ((value & 4) != 0)
                painter.drawText(xtype + cxChar * 5, ytype, tr("OFF"));
            else
                painter.drawText(xtype + cxChar * 5, ytype, tr("ON"));
        }
    }

    painter.drawText(xtype, ybase - cyLine * 14 - cyLine / 3, tr("I/O"));

    quint16 sp = getProc()->GetSP();
    int ysp = y2 - ((y2 - y1) * sp / 65536);
    painter.drawLine(x2, ysp, x2 + cxChar, ysp);
    painter.drawText(x2 + cxChar, ysp + cyLine / 4, "SP");

    quint16 pc = getProc()->GetPC();
    int ypc = y2 - ((y2 - y1) * pc / 65536);
    painter.drawLine(x2, ypc, x2 + cxChar, ypc);
    painter.drawText(x2 + cxChar, ypc + cyLine / 4, "PC");
}

//////////////////////////////////////////////////////////////////////
