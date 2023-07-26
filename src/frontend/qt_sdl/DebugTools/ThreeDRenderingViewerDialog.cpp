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
#include <string>
#include <vector>

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>

#include "GPU3D.h"

#include "ThreeDRenderingViewerDialog.h"
#include "ui_ThreeDRenderingViewerDialog.h"

struct CmdAggregatedEntry
{
    u32* Params;
    u8 Command;
    
    CmdAggregatedEntry(u8 command, u32* params)
    {
        Command = command;
        Params = params;
    }
};

static QRgb RGB15toQRgb(u16 colorData)
{
    return QRgb(((colorData & 0x1F) << 19) | (((colorData >> 5) & 0x1F) << 11) | (((colorData >> 10) & 0x1F) << 3) | 0xFF000000);
}

static void writeMultiParamString(std::stringstream &str, u32* execParams, int w, int h, bool newLine = false)
{
    for (int i = 0; i < w * h; i++)
    {
        str << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << execParams[i];
        if (i != (w * h - 1) && i % w != w - 1)
        {
            str << ", ";
        }
        else if (i != (w * h - 1) && i % w == w - 1)
        {
            if (newLine)
            {
                str << "\n";
            }
            else
            {
                str << " / ";
            }
        }
    }
}

static TexParam* parseTexImageParam(u32 texparam)
{
    TexParam* paramStruct = new TexParam();

    paramStruct->Vramaddr = (texparam & 0xFFFF) << 3;

    paramStruct->WrapX = (texparam >> 16) & 0x1;
    paramStruct->WrapY = (texparam >> 17) & 0x1;
    paramStruct->FlipX = (texparam >> 18) & 0x1;
    paramStruct->FlipY = (texparam >> 19) & 0x1;

    paramStruct->Width = 8 << ((texparam >> 20) & 0x7);
    paramStruct->Height = 8 << ((texparam >> 23) & 0x7);

    paramStruct->Format = (texparam >> 26) & 0x7;
    paramStruct->Alpha0 = ((texparam >> 29) & 0x1) ? 0 : 31;
    paramStruct->TransformationMode = (texparam >> 30) & 0x3;

    return paramStruct;
}

TexturePreviewer* ThreeDRenderingViewerDialog::getTexturePreviewer(TexParam* texParam, u32 texPalAddr)
{
    QVector<QRgb> palette = QVector<QRgb>();
    int numColors = 0;

    // case 7 is ignored because it has no palette
    switch (texParam->Format)
    {
        case 1:
            texPalAddr <<= 4;
            numColors = 32;
            break;

        case 2:
            texPalAddr <<= 3;
            numColors = 4;
            break;

        case 3:
            texPalAddr <<= 4;
            numColors = 16;
            break;

        case 4:
        case 5:
            texPalAddr <<= 4;
            numColors = 256;
            break;

        case 6:
            texPalAddr <<= 4;
            numColors = 8;
            break;
    }
    
    for (int i = 0; i < numColors; i++)
    {
        u16 colorData = this->VRAMFlat_TexPalCache[texPalAddr + i * 2] | (this->VRAMFlat_TexPalCache[texPalAddr + i * 2 + 1] << 8);
        palette.append(RGB15toQRgb(colorData));
    }
    
    QImage* texture = nullptr;

    switch (texParam->Format)
    {
        case 1: // A3I5
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_ARGB32);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x++)
                {
                    u8 pixel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++];
                    QColor color = QColor(palette[pixel & 0x1F]);
                    color.setAlpha((((pixel >> 3) & 0x1C) + (pixel >> 6)) << 3);
                    texture->setPixelColor(x, y, color);
                }
            }
            break;
        }

        case 2: // PAL4
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_Indexed8);
            texture->setColorTable(palette);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x += 4)
                {
                    u8 texel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++];
                    texture->setPixel(x, y, texel & 0x3);
                    texture->setPixel(x + 1, y, (texel >> 2) & 0x3);
                    texture->setPixel(x + 2, y, (texel >> 4) & 0x3);
                    texture->setPixel(x + 3, y, (texel >> 6) & 0x3);
                }
            }
            break;
        }

        case 3: // PAL16
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_Indexed8);
            texture->setColorTable(palette);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x += 2)
                {
                    u8 texel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++];
                    texture->setPixel(x, y, texel & 0x1F);
                    texture->setPixel(x + 1, y, (texel >> 4) & 0x1F);
                }
            }
            break;
        }

        case 4: // PAL256
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_Indexed8);
            texture->setColorTable(palette);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x++)
                {
                    texture->setPixel(x, y, this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++]);
                }
            }
            break;
        }

        case 5: // CMPR
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_ARGB32);
            break;
        }

        case 6: // A5I3
        {
            break;
        }

        case 7: // Direct
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_RGB555);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x++)
                {
                    u16 pixel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++] | (this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++] << 8);
                    texture->setPixel(x, y, pixel);
                }
            }
            break;
        }
    }

    if (texture == nullptr)
    {
        texture = new QImage(texParam->Width, texParam->Height, QImage::Format_ARGB32);
    }

    return new TexturePreviewer(this, texture);
}

std::vector<CmdAggregatedEntry> AggregatedFIFOCache;

QWidgetList previewWidgets = { };

ThreeDRenderingViewerDialog* ThreeDRenderingViewerDialog::currentDlg = nullptr;

void ThreeDRenderingViewerDialog::updatePipeline()
{
    ui->pipelineCommandsTree->clear();
    AggregatedFIFOCache.clear();
    // Remove all items from the preview layout
    QLayoutItem* item;
    while ((item = ui->previewLayout->takeAt(0)) != nullptr)
    {
        ui->previewLayout->removeItem(item);
        delete item;
    }
    
    memcpy(this->VRAMFlat_TextureCache, GPU::VRAMFlat_Texture, 512*1024);
    memcpy(this->VRAMFlat_TexPalCache, GPU::VRAMFlat_TexPal, 128*1024);
    u32 numEntries = GPU3D::CmdFIFOReporter.size();
    QTreeWidgetItem* vertexListItem = nullptr;

    u32 execParamsCount = 0;
    u32 execParams[32];

    for (u32 i = 0; i < numEntries; i++)
    {
        GPU3D::CmdFIFOEntry entry = GPU3D::CmdFIFOReporter[i];
        std::string commandString = std::string("");
        std::stringstream paramString = std::stringstream("");
        bool createSubItem = false;
        bool endSubItem = false;

        u32 paramsRequiredCount = GPU3D::CmdNumParams[entry.Command];
        if (paramsRequiredCount <= 1)
        {
            switch (entry.Command)
            {
            case 0x00:
                commandString = "NOP";
                break;

            case 0x10:
                commandString = "MTX_MODE";
                paramString << (entry.Param & 3);
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
                
            case 0x20:
                commandString = "COLOR";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << entry.Param;
                break;
                
            case 0x21:
                commandString = "NORMAL";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << entry.Param;
                break;
                
            case 0x22:
                commandString = "TEXCOORD";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x24:
                commandString = "VTX_10";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x25:
                commandString = "VTX_XY";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x26:
                commandString = "VTX_XZ";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x27:
                commandString = "VTX_YZ";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x28:
                commandString = "VTX_DIFF";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x29:
                commandString = "POLYGON_ATTR";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x2A:
                commandString = "TEXIMAGE_PARAM";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x2B:
                commandString = "PLTT_BASE";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x30:
                commandString = "DIF_AMB";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x31:
                commandString = "SPE_EMI";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x32:
                commandString = "LIGHT_VECTOR";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x33:
                commandString = "LIGHT_COLOR";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
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
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;
                
            case 0x72:
                commandString = "VEC_TEST";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << entry.Param;
                break;

            default:
                commandString = "Unknown Command!";
                paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << entry.Command;
                break;
            }

            u32* params = new u32[1];
            params[0] = entry.Param;
            AggregatedFIFOCache.push_back(CmdAggregatedEntry(entry.Command, params));
        }
        else
        {
            execParams[execParamsCount++] = entry.Param;
            
            if (execParamsCount >= paramsRequiredCount)
            {
                execParamsCount = 0;

                switch (entry.Command)
                {
                case 0x16:
                    commandString = "MTX_LOAD_4x4";
                    writeMultiParamString(paramString, execParams, 4, 4);
                    break;

                case 0x17:
                    commandString = "MTX_LOAD_4x3";
                    writeMultiParamString(paramString, execParams, 4, 3);                    
                    break;
                    
                case 0x18:
                    commandString = "MTX_MULT_4x4";
                    writeMultiParamString(paramString, execParams, 4, 4);
                    break;
                    
                case 0x19:
                    commandString = "MTX_MULT_4x3";
                    writeMultiParamString(paramString, execParams, 4, 3);
                    break;
                    
                case 0x1A:
                    commandString = "MTX_MULT_3x3";
                    writeMultiParamString(paramString, execParams, 3, 3);
                    break;
                    
                case 0x1B:
                    commandString = "MTX_SCALE";
                    writeMultiParamString(paramString, execParams, 3, 1);
                    break;
                    
                case 0x1C:
                    commandString = "MTX_TRANS";
                    writeMultiParamString(paramString, execParams, 3, 1);
                    break;

                case 0x23:
                    commandString = "VTX_16";
                    writeMultiParamString(paramString, execParams, 2, 1);
                    break;
                
                case 0x34:
                    commandString = "SHININESS";
                    writeMultiParamString(paramString, execParams, 2, 1);
                    break;
                    
                case 0x70:
                    commandString = "BOX_TEST";
                    writeMultiParamString(paramString, execParams, 3, 1);
                    break;
                    
                case 0x71:
                    commandString = "POS_TEST";
                    writeMultiParamString(paramString, execParams, 2, 1);
                    break;
                    
                default:
                    commandString = "Unknown Command!";
                    paramString << "0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << entry.Command;
                    break;
                }

                AggregatedFIFOCache.push_back(CmdAggregatedEntry(entry.Command, execParams));
            }
            else
            {
                continue;
            }
        }

        QList<QString> strings = QList<QString>();
        strings.append(QString(std::to_string(AggregatedFIFOCache.size() - 1).c_str()));
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

ThreeDRenderingViewerDialog::ThreeDRenderingViewerDialog(QWidget* parent, bool emuActive) : QDialog(parent), ui(new Ui::ThreeDRenderingViewerDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    GPU3D::Report3DPipeline = true;
    updatePipeline();
}

ThreeDRenderingViewerDialog::~ThreeDRenderingViewerDialog()
{
    GPU3D::Report3DPipeline = false;
    delete ui;
}

void ThreeDRenderingViewerDialog::on_updateButton_clicked()
{
    updatePipeline();
}

void ThreeDRenderingViewerDialog::on_pipelineCommandsTree_itemSelectionChanged()
{
    // Remove all items from the layout
    while (previewWidgets.size() > 0)
    {
        ui->previewLayout->removeWidget(previewWidgets[0]);
        delete previewWidgets[0];
        previewWidgets.pop_front();
    }

    QList<QTreeWidgetItem*> selectedItems = ui->pipelineCommandsTree->selectedItems();
    if (selectedItems.count() == 0)
    {
        return;
    }

    int index = std::stoi(selectedItems[0]->text(0).toStdString());
    switch (AggregatedFIFOCache[index].Command)
    {
    case 0x10: // MTX_MODE
    {
        QLabel* matrixModeLabel = new QLabel((std::string("Matrix Mode ") + std::to_string(AggregatedFIFOCache[index].Params[0])).c_str());
        ui->previewLayout->addWidget(matrixModeLabel);
        previewWidgets.push_back(matrixModeLabel);
        break;
    }

    case 0x16: // MTX_LOAD_4x4
    case 0x18: // MTX_MULT_4x4
    {
        std::stringstream str = std::stringstream("");
        writeMultiParamString(str, AggregatedFIFOCache[index].Params, 4, 4, true);
        QLabel* mtxLabel = new QLabel(str.str().c_str());
        ui->previewLayout->addWidget(mtxLabel);
        previewWidgets.push_back(mtxLabel);
        break;
    }

    case 0x17: // MTX_LOAD_4x3
    case 0x19: // MTX_MULT_4x3
    {
        std::stringstream str = std::stringstream("");
        writeMultiParamString(str, AggregatedFIFOCache[index].Params, 4, 3, true);
        QLabel* mtxLabel = new QLabel(str.str().c_str());
        ui->previewLayout->addWidget(mtxLabel);
        previewWidgets.push_back(mtxLabel);
        break;
    }

    case 0x1A: // MTX_MULT_3x3
    {
        std::stringstream str = std::stringstream("");
        writeMultiParamString(str, AggregatedFIFOCache[index].Params, 3, 3, true);
        QLabel* mtxLabel = new QLabel(str.str().c_str());
        ui->previewLayout->addWidget(mtxLabel);
        previewWidgets.push_back(mtxLabel);
        break;
    }
    
    case 0x1B: // MTX_SCALE
    case 0x1C: // MTX_TRANS
    {
        std::stringstream str = std::stringstream("");
        writeMultiParamString(str, AggregatedFIFOCache[index].Params, 3, 1, true);
        QLabel* mtxLabel = new QLabel(str.str().c_str());
        ui->previewLayout->addWidget(mtxLabel);
        previewWidgets.push_back(mtxLabel);
        break;
    }
    
    case 0x20: // COLOR
    {
        u32 rgb15Color = AggregatedFIFOCache[index].Params[0];
        uint colorRgb = ((rgb15Color & 0x1F) << 3) | (((rgb15Color >> 5) & 0x1F) << 11) | (((rgb15Color >> 10) & 0x1F) << 19) | 0xFF000000;
        QColor color = QColor(QRgb(colorRgb));

        ColorWidget* colorWidget = new ColorWidget(this, color);
        colorWidget->color = color;
        ui->previewLayout->addWidget(colorWidget);
        previewWidgets.push_back(colorWidget);
        break;   
    }

    case 0x2A: // TEXIMAGE_PARAM
    {
        std::stringstream str = std::stringstream("");
        if (AggregatedFIFOCache[index].Params[0] == 0)
        {
            QLabel* texParamLabel = new QLabel("No texture.");
            ui->previewLayout->addWidget(texParamLabel);
            previewWidgets.push_back(texParamLabel);
        }
        else
        {
            TexParam* texParam = parseTexImageParam(AggregatedFIFOCache[index].Params[0]);

            str << "VRAM Address: " << texParam->Vramaddr << "\nSize: " << texParam->Width << "x" << texParam->Height << "\nType: ";
            switch (texParam->Format)
            {
                case 1:
                    str << "A3I5";
                    break;

                case 2:
                    str << "PAL4";
                    if (texParam->Alpha0) str << ", first transparent";
                    break;

                case 3:
                    str << "PAL16";
                    if (texParam->Alpha0) str << ", first transparent";
                    break;

                case 4:
                    str << "PAL256";
                    if (texParam->Alpha0) str << ", first transparent";
                    break;

                case 5:
                    str << "CMPR";
                    if (texParam->Alpha0) str << ", first transparent";
                    break;

                case 6:
                    str << "A5I3";
                    break;

                case 7:
                    str << "RGB15";
                    break;
            }
            str << "\n";
            if (texParam->WrapX) str << "Wrap X\n";
            if (texParam->WrapY) str << "Wrap Y\n";
            if (texParam->FlipX) str << "Flip X\n";
            if (texParam->FlipY) str << "Flip Y\n";

            str << "Transformation Mode: ";
            switch (texParam->TransformationMode)
            {
                case 0:
                    str << "Do not transform";
                    break;
                
                case 1:
                    str << "Texcoord source";
                    break;
                
                case 2:
                    str << "Normal source";
                    break;

                case 3:
                    str << "Vertex source";
                    break;
            }
             
            QLabel* texParamLabel = new QLabel(str.str().c_str());
            ui->previewLayout->addWidget(texParamLabel);
            previewWidgets.push_back(texParamLabel);

            for (int i = index - 1; i >= 0; i--)
            {
                if (AggregatedFIFOCache[i].Command == 0x2B) // PLTT_BASE
                {
                    TexturePreviewer* texturePreviewer = this->getTexturePreviewer(texParam, AggregatedFIFOCache[i].Params[0] & 0x1FFF);
                    ui->previewLayout->addWidget(texturePreviewer);
                    previewWidgets.push_back(texturePreviewer);
                    break;
                }
            }
        }
        break;
    }
    
    case 0x2B: // PLTT_BASE
    {
        for (int i = index - 1; i >= 0; i--)
        {  
            if (AggregatedFIFOCache[i].Command == 0x2A) // TEXIMAGE_PARAM
            {
                if (AggregatedFIFOCache[i].Params[0] != 0)
                {
                    TexParam* texParam = parseTexImageParam(AggregatedFIFOCache[i].Params[0]);

                    u32 palAddr = AggregatedFIFOCache[index].Params[0];
                    if (texParam->Format == 2) // PAL4
                    {
                        palAddr <<= 3;
                    }
                    else
                    {
                        palAddr <<= 4;
                    }
                    std::stringstream str = std::stringstream("");
                    str << "Palette Address: 0x" << std::hex << std::uppercase << palAddr;
                    QLabel* pltBaseLabel = new QLabel(str.str().c_str());
                    ui->previewLayout->addWidget(pltBaseLabel);
                    previewWidgets.push_back(pltBaseLabel);

                    TexturePreviewer* texturePreviewer = this->getTexturePreviewer(texParam, AggregatedFIFOCache[index].Params[0] & 0x1FFF);
                    ui->previewLayout->addWidget(texturePreviewer);
                    previewWidgets.push_back(texturePreviewer);
                    break;
                }
            }
        }
        break;
    }

    case 0x33: // LIGHT_COLOR
    {
        u32 rgb15Color = AggregatedFIFOCache[index].Params[0];
        uint colorRgb = ((rgb15Color & 0x1F) << 3) | (((rgb15Color >> 5) & 0x1F) << 11) | (((rgb15Color >> 10) & 0x1F) << 19) | 0xFF000000;
        QColor color = QColor(QRgb(colorRgb));

        QLabel* lightLabel = new QLabel((std::string("Light ") + std::to_string(rgb15Color >> 30)).c_str());
        ui->previewLayout->addWidget(lightLabel);
        previewWidgets.push_back(lightLabel);

        ColorWidget* colorWidget = new ColorWidget(this, color);
        colorWidget->color = color;
        ui->previewLayout->addWidget(colorWidget);
        previewWidgets.push_back(colorWidget);
        break;
    }
    }
}

void ThreeDRenderingViewerDialog::on_ThreeDRenderingViewerDialog_rejected()
{
    closeDlg();
}