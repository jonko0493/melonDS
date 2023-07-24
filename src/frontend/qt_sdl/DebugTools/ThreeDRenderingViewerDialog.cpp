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

#include <iomanip>
#include <sstream>

#include "GPU3D.h"

#include "ThreeDRenderingViewerDialog.h"
#include "ui_ThreeDRenderingViewerDialog.h"

u32 MatrixMode;

ThreeDRenderingViewerDialog* ThreeDRenderingViewerDialog::currentDlg = nullptr;

ThreeDRenderingViewerDialog::ThreeDRenderingViewerDialog(QWidget* parent, bool emuActive) : QDialog(parent), ui(new Ui::ThreeDRenderingViewerDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    GPU3D::ReportFIFO = true;
    update();
}

void ThreeDRenderingViewerDialog::update()
{
    ui->pipelineCommandsTree->clear();

    u32 numEntries = GPU3D::CmdFIFOReporter.Level();
    QTreeWidgetItem* vertexListItem = nullptr;

    for (u32 i = 0; i < numEntries; i++)
    {
        GPU3D::CmdFIFOEntry entry = GPU3D::CmdFIFOReporter.Peek(i);
        std::string commandString = std::string("");
        std::stringstream paramString = std::stringstream("");
        bool createSubItem = false;
        bool endSubItem = false;

        u32 paramsRequiredCount = GPU3D::CmdNumParams[entry.Command];
        // if (paramsRequiredCount <= 1)
        // {
            switch (entry.Command)
            {
            case 0x10:
                MatrixMode = entry.Param & 3;
                commandString = "MTX_MODE";
                paramString << MatrixMode;
                break;

            case 0x11:
                commandString = "MTX_PUSH";
                break;
            
            case 0x12:
                commandString = "MTX_POP";
                break;

            case 0x13:
                commandString = "MTX_STORE";
                break;

            case 0x14:
                commandString = "MTX_RESTORE";
                break;

            case 0x15:
                commandString = "MTX_IDENTITY";
                break;

            case 0x16:
                commandString = "MTX_LOAD_4x4";
                break;
                
            case 0x17:
                commandString = "MTX_LOAD_4x3";
                break;
                
            case 0x18:
                commandString = "MTX_MULT_4x4";
                break;
                
            case 0x19:
                commandString = "MTX_MULT_4x3";
                break;
                
            case 0x1A:
                commandString = "MTX_MULT_3x3";
                break;
                
            case 0x1B:
                commandString = "MTX_SCALE";
                break;
                
            case 0x1C:
                commandString = "MTX_TRANS";
                break;
                
            case 0x20:
                commandString = "COLOR";
                break;
                
            case 0x21:
                commandString = "NORMAL";
                break;
                
            case 0x22:
                commandString = "TEXCOORD";
                break;
                
            case 0x23:
                commandString = "VTX_16";
                break;
                
            case 0x24:
                commandString = "VTX_10";
                break;
                
            case 0x25:
                commandString = "VTX_XY";
                break;
                
            case 0x26:
                commandString = "VTX_XZ";
                break;
                
            case 0x27:
                commandString = "VTX_YZ";
                break;
                
            case 0x28:
                commandString = "VTX_DIFF";
                break;
                
            case 0x29:
                commandString = "POLYGON_ATTR";
                break;
                
            case 0x2A:
                commandString = "TEXIMAGE_PARAM";
                break;
                
            case 0x2B:
                commandString = "PLLT_BASE";
                break;
                
            case 0x30:
                commandString = "DIF_AMB";
                break;
                
            case 0x31:
                commandString = "SPE_EMI";
                break;
                
            case 0x32:
                commandString = "LIGHT_VECTOR";
                break;
                
            case 0x33:
                commandString = "LIGHT_COLOR";
                break;
                
            case 0x34:
                commandString = "SHININESS";
                break;
                
            case 0x40:
                commandString = "BEGIN_VTXS";
                createSubItem = true;
                break;
                
            case 0x41:
                commandString = "END_VTXS";
                endSubItem = true;
                break;
                
            case 0x50:
                commandString = "SWAP_BUFFERS";
                break;
                
            case 0x60:
                commandString = "VIEWPORT";
                break;
                
            case 0x70:
                commandString = "BOX_TEST";
                break;
                
            case 0x71:
                commandString = "POS_TEST";
                break;
                
            case 0x72:
                commandString = "VEC_TEST";
                break;
            }
        // }
        // else
        // {
            
        // }

        QList<QString> strings = QList<QString>();
        strings.append(QString(commandString.c_str()));
        strings.append(QString(paramString.str().c_str()));

        QTreeWidgetItem* commandItem = new QTreeWidgetItem(QStringList(strings));
        if (vertexListItem == nullptr)
        {
            ui->pipelineCommandsTree->addTopLevelItem(commandItem);
        }
        else
        {
            vertexListItem->addChild(commandItem);
        }

        if (createSubItem && vertexListItem == nullptr)
        {
            vertexListItem = commandItem;
        }
        if (endSubItem && vertexListItem != nullptr)
        {
            vertexListItem = nullptr;
        }
    } 
}

void ThreeDRenderingViewerDialog::on_ThreeDRenderingViewerDialog_reset()
{
    update();
}

ThreeDRenderingViewerDialog::~ThreeDRenderingViewerDialog()
{
    GPU3D::ReportFIFO = false;
    delete ui;
}

void ThreeDRenderingViewerDialog::on_ThreeDRenderingViewerDialog_rejected()
{
    closeDlg();
}