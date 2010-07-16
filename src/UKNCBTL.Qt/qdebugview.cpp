#include "stdafx.h"
#include <QtGui>
#include "qdebugview.h"
#include "Emulator.h"
#include "emubase/Emubase.h"


//////////////////////////////////////////////////////////////////////


QDebugView::QDebugView(QWidget *parent) :
        QWidget(parent)
{
    m_okDebugProcessor = FALSE;

    QFont font = Common_GetMonospacedFont();
    QFontMetrics fontmetrics(font);
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    this->setMinimumSize(cxChar * 55, cyLine * 14 + cyLine / 2);
    this->setMaximumHeight(cyLine * 14 + cyLine / 2);
}

void QDebugView::setCurrentProc(bool okProc)
{
    m_okDebugProcessor = okProc;
    this->updateData();
}

void QDebugView::updateData()
{
    CProcessor* pCPU = g_pBoard->GetCPU();
    ASSERT(pCPU != NULL);

    // Get new register values and set change flags
    for (int r = 0; r < 8; r++) {
        WORD value = pCPU->GetReg(r);
        m_okDebugCpuRChanged[r] = (m_wDebugCpuR[r] != value);
        m_wDebugCpuR[r] = value;
    }
    WORD pswCPU = pCPU->GetPSW();
    m_okDebugCpuRChanged[8] = (m_wDebugCpuR[8] != pswCPU);
    m_wDebugCpuR[8] = pswCPU;

    CProcessor* pPPU = g_pBoard->GetPPU();
    ASSERT(pPPU != NULL);

    // Get new register values and set change flags
    for (int r = 0; r < 8; r++) {
        WORD value = pPPU->GetReg(r);
        m_okDebugPpuRChanged[r] = (m_wDebugPpuR[r] != value);
        m_wDebugPpuR[r] = value;
    }
    WORD pswPPU = pPPU->GetPSW();
    m_okDebugPpuRChanged[8] = (m_wDebugPpuR[8] != pswPPU);
    m_wDebugPpuR[8] = pswPPU;
}

void QDebugView::paintEvent(QPaintEvent *event)
{
    if (g_pBoard == NULL) return;

    QPainter painter(this);
    painter.fillRect(0,0, this->width(), this->height(), Qt::white);

    QFont font = Common_GetMonospacedFont();
    painter.setFont(font);
    QFontMetrics fontmetrics(font);
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();

    CProcessor* pDebugPU = (m_okDebugProcessor) ? g_pBoard->GetCPU() : g_pBoard->GetPPU();
    ASSERT(pDebugPU != NULL);
    WORD* arrR = (m_okDebugProcessor) ? m_wDebugCpuR : m_wDebugPpuR;
    BOOL* arrRChanged = (m_okDebugProcessor) ? m_okDebugCpuRChanged : m_okDebugPpuRChanged;

    LPCTSTR sProcName = pDebugPU->GetName();
    painter.drawText(cxChar * 1, 2 * cyLine, sProcName);

    DrawProcessor(painter, pDebugPU, cxChar * 6, 1 * cyLine, arrR, arrRChanged);

    // Draw stack
    DrawMemoryForRegister(painter, 6, pDebugPU, 35 * cxChar, 1 * cyLine);
}

void QDebugView::DrawProcessor(QPainter &painter, const CProcessor *pProc, int x, int y, WORD *arrR, BOOL *arrRChanged)
{
    QFontMetrics fontmetrics(painter.font());
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    QColor colorText = painter.pen().color();

    painter.drawRect(x - cxChar, y - cyLine/2, 28 * cxChar, cyLine * 13);

    // Registers
    for (int r = 0; r < 8; r++) {
        painter.setPen(QColor(arrRChanged[r] ? Qt::red : colorText));

        LPCTSTR strRegName = REGISTER_NAME[r];
        painter.drawText(x, y + (1 + r) * cyLine, strRegName);

        WORD value = arrR[r]; //pProc->GetReg(r);
        DrawOctalValue(painter, x + cxChar * 3, y + (1 + r) * cyLine, value);
        DrawBinaryValue(painter, x + cxChar * 10, y + (1 + r) * cyLine, value);
    }
    painter.setPen(colorText);

    // PSW value
    painter.setPen(QColor(arrRChanged[8] ? Qt::red : colorText));
    painter.drawText(x, y + 10 * cyLine, _T("PS"));
    WORD psw = arrR[8]; // pProc->GetPSW();
    DrawOctalValue(painter, x + cxChar * 3, y + 10 * cyLine, psw);
    painter.drawText(x + cxChar * 10, y + 9 * cyLine, _T("       HP  TNZVC"));
    DrawBinaryValue(painter, x + cxChar * 10, y + 10 * cyLine, psw);

    painter.setPen(colorText);

    // Processor mode - HALT or USER
    BOOL okHaltMode = pProc->IsHaltMode();
    painter.drawText(x, y + 12 * cyLine, okHaltMode ? _T("HALT") : _T("USER"));

    // "Stopped" flag
    BOOL okStopped = pProc->IsStopped();
    if (okStopped)
        painter.drawText(x + 6 * cxChar, y + 12 * cyLine, _T("STOP"));
}

void QDebugView::DrawMemoryForRegister(QPainter &painter, int reg, CProcessor *pProc, int x, int y)
{
    QFontMetrics fontmetrics(painter.font());
    int cxChar = fontmetrics.averageCharWidth();
    int cyLine = fontmetrics.height();
    QColor colorText = painter.pen().color();

    WORD current = pProc->GetReg(reg);
    BOOL okExec = (reg == 7);

    // ������ �� ������ ���������� � �����
    WORD memory[16];
    CMemoryController* pMemCtl = pProc->GetMemoryController();
    for (int idx = 0; idx < 16; idx++) {
        int addrtype;
        memory[idx] = pMemCtl->GetWordView(
                current + idx * 2 - 14, pProc->IsHaltMode(), okExec, &addrtype);
    }

    WORD address = current - 10;
    for (int index = 0; index < 14; index++) {  // ������ ������
        // �����
        DrawOctalValue(painter, x + 4 * cxChar, y, address);

        // �������� �� ������
        WORD value = memory[index];
//        WORD wChanged = Emulator_GetChangeRamStatus(address);
//        painter.setPen(wChanged != 0 ? Qt::red : colorText);
        DrawOctalValue(painter, x + 12 * cxChar, y, value);
//        painter.setPen(colorText);

        // ������� �������
        if (address == current) {
            painter.drawText(x + 2 * cxChar, y, _T(">>"));
            painter.setPen(m_okDebugCpuRChanged[reg] != 0 ? Qt::red : colorText);
            painter.drawText(x, y, REGISTER_NAME[reg]);
            painter.setPen(colorText);
        }

        address += 2;
        y += cyLine;
    }
}
