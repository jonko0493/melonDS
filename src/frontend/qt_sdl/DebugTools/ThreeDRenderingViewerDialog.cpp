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

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>

#include "GPU3D.h"
#include "main.h"

#include "ThreeDRenderingViewerDialog.h"
#include "ui_ThreeDRenderingViewerDialog.h"

extern EmuThread* emuThread;

static const char* getMatrixModeString(int mtxMode)
{
    switch (mtxMode)
    {
        case 0:
            return "Projection";

        case 1:
            return "Position";

        case 2:
            return "Position and Vector";

        case 3:
            return "Texture";

        default:
            return "Unknown Mode";
    }
}

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

static u16* unpackRawTexcoords(u32 packedTexcoords)
{
    u16* texcoords = new u16[2];
    texcoords[0] = packedTexcoords & 0xFFFF;
    texcoords[1] = packedTexcoords >> 16;
    return texcoords;
}

static u16* transformTexcoords(u16* rawTexCoords, s32* texMatrix)
{
    // u16* texcoords = new u16[2];
    // texcoords[0] = (rawTexCoords[0]*texMatrix[0] + rawTexCoords[1]*texMatrix[4] + texMatrix[8] + texMatrix[12]) >> 12;;
    // texcoords[1] = (rawTexCoords[0]*rawTexCoords[1] + rawTexCoords[1]*texMatrix[5] + texMatrix[9] + texMatrix[13]) >> 12;
    // return texcoords;
    return rawTexCoords;
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
            texPalAddr <<= 4;
            numColors = 256;
            break;

        case 5:
            texPalAddr <<= 4;
            // building the palette AoT for case 5 is useless with its large palette offset
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
                    texture->setPixel(x, y, texel & 0xF);
                    texture->setPixel(x + 1, y, (texel >> 4) & 0xF);
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
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                QColor color = QColor(Qt::black);
                for (int x = 0; x < texParam->Width; x++)
                {
                    u32 vramaddr = texParam->Vramaddr + ((y & 0x3FC) * (texParam->Width >> 2) + (x & 0x3FC)) + (y & 0x3);
                    u32 slot1addr = 0x20000 + ((vramaddr & 0x1FFFC) >> 1);
                    if (vramaddr >= 0x40000)
                        slot1addr += 0x10000;

                    u8 val = VRAMFlat_TextureCache[vramaddr] >> (2 * (x & 0x3));
                    u16 palInfo = VRAMFlat_TextureCache[slot1addr] | (VRAMFlat_TextureCache[slot1addr + 1] << 8);
                    u32 palOffset = (palInfo & 0x3FFF) << 2;

                    switch (val & 0x3)
                    {
                    case 0:
                        color = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 1] << 8))));
                        break;

                    case 1:
                        color = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 2] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 3] << 8))));
                        break;

                    case 2:
                        if (palInfo >> 14 == 1)
                        {
                            QColor color1 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 1] << 8))));
                            QColor color2 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 2] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 3] << 8))));

                            int r = (color1.red() + color2.red()) >> 1;
                            int g = (color1.green() + color2.green()) >> 1;
                            int b = (color1.blue() + color2.blue()) >> 1;

                            color = QColor(r, g, b);
                        }
                        else if (palInfo >> 14 == 3)
                        {
                            QColor color1 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 1] << 8))));
                            QColor color2 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 2] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 3] << 8))));

                            int r = (color1.red() * 5 + color2.red() * 3) >> 3;
                            int g = (color1.green() * 5 + color2.green() * 3) >> 3;
                            int b = (color1.blue() * 5 + color2.blue() * 3) >> 3;

                            color = QColor(r, g, b);
                        }
                        else
                        {
                            color = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 4] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 5] << 8))));
                        }
                        break;

                    case 3:
                        if (palInfo >> 14 == 2)
                        {
                            color = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 6] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 7] << 8))));
                        }
                        else if (palInfo >> 14 == 3)
                        {
                            QColor color1 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 1] << 8))));
                            QColor color2 = QColor(QRgb(RGB15toQRgb(VRAMFlat_TexPalCache[texPalAddr + palOffset + 2] | (VRAMFlat_TexPalCache[texPalAddr + palOffset + 3] << 8))));

                            int r = (color1.red() * 3 + color2.red() * 5) >> 3;
                            int g = (color1.green() * 3 + color2.green() * 5) >> 3;
                            int b = (color1.blue() * 3 + color2.blue() * 5) >> 3;

                            color = QColor(r, g, b);
                        }
                        else
                        {
                            color.setAlpha(0);
                        }
                        break;
                    }

                    texture->setPixelColor(x, y, color);
                }
            }
            break;
        }

        case 6: // A5I3
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_ARGB32);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x++)
                {
                    u8 pixel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++];
                    QColor color = QColor(palette[pixel & 0x7]);
                    color.setAlpha(pixel >> 3);
                    texture->setPixelColor(x, y, color);
                }
            }
            break;
        }

        case 7: // Direct
        {
            texture = new QImage(texParam->Width, texParam->Height, QImage::Format_ARGB32);
            int pixelIndex = 0;
            for (int y = 0; y < texParam->Height; y++)
            {
                for (int x = 0; x < texParam->Width; x++)
                {
                    u16 pixel = this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++] | (this->VRAMFlat_TextureCache[texParam->Vramaddr + pixelIndex++] << 8);
                    texture->setPixel(x, y, RGB15toQRgb(pixel));
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

QWidgetList previewWidgets = { };
QList<QGridLayout*> previewGridLayouts { };

ThreeDRenderingViewerDialog* ThreeDRenderingViewerDialog::currentDlg = nullptr;

const char* ThreeDRenderingViewerDialog::findMtxMode(int index)
{
    for (int i = index - 1; i >= 0; i--)
    {
        if (this->AggregatedFIFOCache[i]->Command == 0x10) // MTX_MODE
        {
            return getMatrixModeString(this->AggregatedFIFOCache[i]->Params[0]);
        }
    }
    return "Projection";
}

TexturePreviewer* ThreeDRenderingViewerDialog::addVertexGroupTexturePreview(int index)
{
    TexParam* texParam = nullptr;
    u32 basePalAddr = 0x2000;
    
    for (int i = index - 1; i >= 0; i--)
    {  
        if (this->AggregatedFIFOCache[i]->Command == 0x2A) // TEXIMAGE_PARAM
        {
            if (this->AggregatedFIFOCache[i]->Params[0] != 0)
            {
                texParam = parseTexImageParam(this->AggregatedFIFOCache[i]->Params[0]);
                if (texParam->Format == 7) // RGB15
                {
                    break;
                }
            }
        }
        if (this->AggregatedFIFOCache[i]->Command == 0x2B) // PLTT_BASE
        {
            basePalAddr = this->AggregatedFIFOCache[i]->Params[0] & 0x1FFF;
        }
        
        if (texParam != nullptr && basePalAddr < 0x2000)
        {
            break;
        }
    }
    if (texParam == nullptr)
    {
        texParam = parseTexImageParam(GPU3D::TexParamCache);
    }
    if (basePalAddr == 0x2000)
    {
        basePalAddr = GPU3D::TexPaletteCache;
    }

    if (texParam != nullptr && (basePalAddr < 0x2000 || texParam->Format == 7))
    {
        TexturePreviewer* previewer = this->getTexturePreviewer(texParam, basePalAddr);
        ui->previewLayout->addWidget(previewer);
        previewWidgets.push_back(previewer);
        return previewer;
    }
    return nullptr;
}

void ThreeDRenderingViewerDialog::updatePipeline()
{
    ui->pipelineCommandsTree->clear();
    this->AggregatedFIFOCache.clear();
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

    int matrixMode = GPU3D::MatrixModeCache;

    for (u32 i = 0; i < numEntries; i++)
    {
        GPU3D::CmdFIFOEntry entry = GPU3D::CmdFIFOReporter[i];
        std::string commandString = std::string("");
        std::stringstream paramString = std::stringstream("");
        bool createSubItem = false;
        bool endSubItem = false;

        if (i == 0)
        {
            AggregatedProjectionMatrixCache.push_back(GPU3D::ProjMatrixCache);
            AggregatedPositionMatrixCache.push_back(GPU3D::PosMatrixCache);
            AggregatedVectorMatrixCache.push_back(GPU3D::VecMatrixCache);
            AggregatedTextureMatrixCache.push_back(GPU3D::TexMatrixCache);
        }
        else
        {
            AggregatedProjectionMatrixCache.push_back(AggregatedProjectionMatrixCache[i - 1]);
            AggregatedPositionMatrixCache.push_back(AggregatedPositionMatrixCache[i - 1]);
            AggregatedVectorMatrixCache.push_back(AggregatedVectorMatrixCache[i - 1]);
            AggregatedTextureMatrixCache.push_back(AggregatedTextureMatrixCache[i - 1]);
        }

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
                matrixMode = entry.Param & 3;
                paramString << matrixMode;
                break;

            case 0x11:
                commandString = "MTX_PUSH";
                if (matrixMode == 0)
                {
                    memcpy(GPU3D::ProjMatrixStackCache, AggregatedProjectionMatrixCache[i], 16*4);
                    GPU3D::ProjMatrixStackPointerCache++;
                    GPU3D::ProjMatrixStackPointerCache &= 0x1;
                }
                else if (matrixMode == 3)
                {
                    memcpy(GPU3D::TexMatrixStackCache, AggregatedTextureMatrixCache[i], 16*4);
                    GPU3D::TexMatrixStackPointerCache++;
                    GPU3D::TexMatrixStackPointerCache &= 0x1;
                }
                else
                {
                    memcpy(GPU3D::PosMatrixStackCache[GPU3D::PosMatrixStackPointerCache & 0x1F], AggregatedPositionMatrixCache[i], 16*4);
                    memcpy(GPU3D::VecMatrixStackCache[GPU3D::PosMatrixStackPointerCache & 0x1F], AggregatedVectorMatrixCache[i], 16*4);
                    GPU3D::PosMatrixStackPointerCache++;
                    GPU3D::PosMatrixStackPointerCache &= 0x3F;
                }
                break;
            
            case 0x12:
                commandString = "MTX_POP";
                if (matrixMode == 0)
                {
                    GPU3D::ProjMatrixStackPointerCache--;
                    GPU3D::ProjMatrixStackPointerCache &= 0x1;
                    memcpy(AggregatedProjectionMatrixCache[i], GPU3D::ProjMatrixStackCache, 16*4);
                }
                else if (matrixMode == 3)
                {
                    GPU3D::TexMatrixStackPointerCache--;
                    GPU3D::TexMatrixStackPointerCache &= 0x1;
                    memcpy(AggregatedTextureMatrixCache[i], GPU3D::TexMatrixStackCache, 16*4);
                }
                else
                {
                    s32 offset = (s32)(entry.Param << 26) >> 26;
                    GPU3D::PosMatrixStackPointerCache -= offset;
                    GPU3D::PosMatrixStackPointerCache &= 0x3F;
                    memcpy(AggregatedPositionMatrixCache[i], GPU3D::PosMatrixStackCache[GPU3D::PosMatrixStackPointerCache & 0x1F], 16*4);
                    memcpy(AggregatedVectorMatrixCache[i], GPU3D::VecMatrixStackCache[GPU3D::PosMatrixStackPointerCache & 0x1F], 16*4);
                }
                break;

            case 0x13:
                commandString = "MTX_STORE";
                if (matrixMode == 0)
                {
                    memcpy(GPU3D::ProjMatrixStackCache, AggregatedProjectionMatrixCache[i], 16*4);
                }
                else if (matrixMode == 3)
                {
                    memcpy(GPU3D::TexMatrixStackCache, AggregatedTextureMatrixCache[i], 16*4);
                }
                else
                {
                    u32 addr = entry.Param & 0x1F;
                    memcpy(GPU3D::PosMatrixStackCache[addr], AggregatedPositionMatrixCache[i], 16*4);
                    memcpy(GPU3D::VecMatrixStackCache[addr], AggregatedVectorMatrixCache[i], 16*4);
                }
                break;

            case 0x14:
                commandString = "MTX_RESTORE";
                if (matrixMode == 0)
                {
                    memcpy(AggregatedProjectionMatrixCache[i], GPU3D::ProjMatrixStackCache, 16*4);
                }
                else if (matrixMode == 3)
                {
                    memcpy(AggregatedTextureMatrixCache[i], GPU3D::TexMatrixStackCache, 16*4);
                }
                else
                {
                    u32 addr = entry.Param & 0x1F;
                    memcpy(AggregatedPositionMatrixCache[i], GPU3D::PosMatrixStackCache[addr], 16*4);
                    memcpy(AggregatedVectorMatrixCache[i], GPU3D::VecMatrixStackCache[addr], 16*4);
                }
                break;

            case 0x15:
                commandString = "MTX_IDENTITY";
                if (matrixMode == 0)
                {
                    GPU3D::MatrixLoadIdentity(AggregatedProjectionMatrixCache[i]);
                }
                else if (matrixMode == 3)
                {
                    GPU3D::MatrixLoadIdentity(AggregatedTextureMatrixCache[i]);
                }
                else
                {
                    GPU3D::MatrixLoadIdentity(AggregatedPositionMatrixCache[i]);
                    if (matrixMode == 2)
                    {
                        GPU3D::MatrixLoadIdentity(AggregatedVectorMatrixCache[i]);
                    }
                }
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
                switch (entry.Param)
                {
                    case 0:
                        paramString << "Triangles";
                        break;
                    case 1:
                        paramString << "Quads";
                        break;
                    case 2:
                        paramString << "Tristrips";
                        break;
                    case 3:
                        paramString << "Quadstrips";
                        break;
                }
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
            this->AggregatedFIFOCache.push_back(new CmdAggregatedEntry(entry.Command, params, 1));
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
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixLoad4x4(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixLoad4x4(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixLoad4x4(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixLoad4x4(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
                    break;

                case 0x17:
                    commandString = "MTX_LOAD_4x3";
                    writeMultiParamString(paramString, execParams, 4, 3);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixLoad4x3(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixLoad4x3(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixLoad4x3(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixLoad4x3(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
                    break;
                    
                case 0x18:
                    commandString = "MTX_MULT_4x4";
                    writeMultiParamString(paramString, execParams, 4, 4);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixMult4x4(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixMult4x4(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixMult4x4(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixMult4x4(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
                    break;
                    
                case 0x19:
                    commandString = "MTX_MULT_4x3";
                    writeMultiParamString(paramString, execParams, 4, 3);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixMult4x3(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixMult4x3(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixMult4x3(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixMult4x3(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
                    break;
                    
                case 0x1A:
                    commandString = "MTX_MULT_3x3";
                    writeMultiParamString(paramString, execParams, 3, 3);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixMult3x3(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixMult3x3(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixMult3x3(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixMult3x3(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
                    break;
                    
                case 0x1B:
                    commandString = "MTX_SCALE";
                    writeMultiParamString(paramString, execParams, 3, 1);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixScale(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixScale(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixScale(AggregatedPositionMatrixCache[i], (s32*)execParams);
                    }
                    break;
                    
                case 0x1C:
                    commandString = "MTX_TRANS";
                    writeMultiParamString(paramString, execParams, 3, 1);
                    if (matrixMode == 0)
                    {
                        GPU3D::MatrixTranslate(AggregatedProjectionMatrixCache[i], (s32*)execParams);
                    }
                    else if (matrixMode == 3)
                    {
                        GPU3D::MatrixTranslate(AggregatedTextureMatrixCache[i], (s32*)execParams);
                    }
                    else
                    {
                        GPU3D::MatrixTranslate(AggregatedPositionMatrixCache[i], (s32*)execParams);
                        if (matrixMode == 2)
                        {
                            GPU3D::MatrixTranslate(AggregatedVectorMatrixCache[i], (s32*)execParams);
                        }
                    }
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

                u32 newExecParams[32] = { };
                memcpy(newExecParams, execParams, 32 * sizeof(u32));
                this->AggregatedFIFOCache.push_back(new CmdAggregatedEntry(entry.Command, newExecParams));
            }
            else
            {
                continue;
            }
        }

        QList<QString> strings = QList<QString>();
        strings.append(QString(std::to_string(this->AggregatedFIFOCache.size() - 1).c_str()));
        strings.append(QString(commandString.c_str()));
        strings.append(QString(paramString.str().c_str()));
        
        QTreeWidgetItem* commandItem = new QTreeWidgetItem(QStringList(strings));

        if (entry.Command == 0x40)
        {
            ui->pipelineCommandsTree->addTopLevelItem(commandItem);
            vertexListItem = nullptr;
        }
        else if (vertexListItem == nullptr)
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
}

ThreeDRenderingViewerDialog::~ThreeDRenderingViewerDialog()
{
    GPU3D::Report3DPipeline = false;
    emuThread->emuUnpause();
    previewWidgets.clear();
    delete ui;
}

void ThreeDRenderingViewerDialog::on_updateButton_clicked()
{
    emuThread->emuPause();
    updatePipeline();
    ui->updateButton->setDisabled(true);
    ui->stepFrameButton->setEnabled(true);
    ui->unpauseButton->setEnabled(true);
}

void ThreeDRenderingViewerDialog::on_stepFrameButton_clicked()
{
    emuThread->emuFrameStep();
    updatePipeline();
}

void ThreeDRenderingViewerDialog::on_unpauseButton_clicked()
{
    emuThread->emuUnpause();
    ui->updateButton->setEnabled(true);
    ui->stepFrameButton->setDisabled(true);
    ui->unpauseButton->setDisabled(true);
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

    TexturePreviewer* texturePreviwer = nullptr;

    if (selectedItems[0]->parent() != NULL) // if we're not a top-level node, we're in a poly and should draw the tex to the screen
    {
        texturePreviwer = addVertexGroupTexturePreview(index);
    }

    switch (this->AggregatedFIFOCache[index]->Command)
    {
    case 0x10: // MTX_MODE
    {
        QLabel* matrixModeLabel = new QLabel((std::string("Matrix Mode: ") + getMatrixModeString(this->AggregatedFIFOCache[index]->Params[0])).c_str());
        ui->previewLayout->addWidget(matrixModeLabel);
        previewWidgets.push_back(matrixModeLabel);
        break;
    }

    case 0x11: // MTX_PUSH
    case 0x12: // MTX_POP
    case 0x13: // MTX_STORE
    case 0x14: // MTX_RESTORE
    case 0x15: // MTX_IDENTITY
    {
        QLabel* matrixModeLabel = new QLabel(this->findMtxMode(index));
        ui->previewLayout->addWidget(matrixModeLabel);
        previewWidgets.push_back(matrixModeLabel);
        break;
    }

    case 0x16: // MTX_LOAD_4x4
    case 0x18: // MTX_MULT_4x4
    {
        QLabel* mtxLabel = new QLabel(this->findMtxMode(index));
        previewWidgets.push_back(mtxLabel);

        QGridLayout* mtxGrid = new QGridLayout();
        mtxGrid->setAlignment(Qt::AlignCenter);
        mtxGrid->addWidget(mtxLabel, 0, 0);

        int entryNum = 0;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                QLabel* mtxValLabel = new QLabel(std::to_string(((s32)this->AggregatedFIFOCache[index]->Params[entryNum++] / 4096.0f)).c_str());
                mtxValLabel->setStyleSheet("font-weight: bold;");
                mtxGrid->addWidget(mtxValLabel, y + 1, x);
                previewWidgets.push_back(mtxValLabel);
            }
        }

        ui->previewLayout->addLayout(mtxGrid);
        previewGridLayouts.push_back(mtxGrid);
        break;
    }

    case 0x17: // MTX_LOAD_4x3
    case 0x19: // MTX_MULT_4x3
    {
        QLabel* mtxLabel = new QLabel(this->findMtxMode(index));
        previewWidgets.push_back(mtxLabel);

        QGridLayout* mtxGrid = new QGridLayout();
        mtxGrid->setAlignment(Qt::AlignCenter);
        mtxGrid->addWidget(mtxLabel, 0, 0);
        
        int entryNum = 0;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                QLabel* mtxValLabel;
                if (x == 3 && y == 3)
                {
                    mtxValLabel = new QLabel("1.0");
                }
                else if (x == 3)
                {
                    mtxValLabel = new QLabel("0.0");
                }
                else
                {
                    mtxValLabel = new QLabel(std::to_string(((s32)this->AggregatedFIFOCache[index]->Params[entryNum++] / 4096.0f)).c_str());
                    mtxValLabel->setStyleSheet("font-weight: bold;");
                }
                mtxGrid->addWidget(mtxValLabel, y + 1, x);
                previewWidgets.push_back(mtxValLabel);
            }
        }

        ui->previewLayout->addLayout(mtxGrid);
        previewGridLayouts.push_back(mtxGrid);
        break;
    }

    case 0x1A: // MTX_MULT_3x3
    {
        QLabel* mtxLabel = new QLabel(this->findMtxMode(index));
        previewWidgets.push_back(mtxLabel);

        QGridLayout* mtxGrid = new QGridLayout();
        mtxGrid->setAlignment(Qt::AlignCenter);
        mtxGrid->addWidget(mtxLabel, 0, 0);
        
        int entryNum = 0;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                QLabel* mtxValLabel;
                if (x == 3 && y == 3)
                {
                    mtxValLabel = new QLabel("1.0");
                }
                else if (x == 3 || y == 3)
                {
                    mtxValLabel = new QLabel("0.0");
                }
                else
                {
                    mtxValLabel = new QLabel(std::to_string(((s32)this->AggregatedFIFOCache[index]->Params[entryNum++] / 4096.0f)).c_str());
                    mtxValLabel->setStyleSheet("font-weight: bold;");
                }
                mtxGrid->addWidget(mtxValLabel, y + 1, x);
                previewWidgets.push_back(mtxValLabel);
            }
        }

        ui->previewLayout->addLayout(mtxGrid);
        previewGridLayouts.push_back(mtxGrid);
        break;
    }
    
    case 0x1B: // MTX_SCALE
    {
        QLabel* mtxLabel = new QLabel(this->findMtxMode(index));
        previewWidgets.push_back(mtxLabel);

        QGridLayout* mtxGrid = new QGridLayout();
        mtxGrid->setAlignment(Qt::AlignCenter);
        mtxGrid->addWidget(mtxLabel, 0, 0);
        
        int entryNum = 0;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                QLabel* mtxValLabel;
                if (x != 3 && x == y)
                {
                    mtxValLabel = new QLabel(std::to_string(((s32)this->AggregatedFIFOCache[index]->Params[entryNum++] / 4096.0f)).c_str());
                    mtxValLabel->setStyleSheet("font-weight: bold;");
                }
                else if (x == 3 && x == y)
                {
                    mtxValLabel = new QLabel("1.0");
                }
                else
                {
                    mtxValLabel = new QLabel("0.0");
                }
                mtxGrid->addWidget(mtxValLabel, y + 1, x);
                previewWidgets.push_back(mtxValLabel);
            }
        }

        ui->previewLayout->addLayout(mtxGrid);
        previewGridLayouts.push_back(mtxGrid);
        break;
    }

    case 0x1C: // MTX_TRANS
    {
        QLabel* mtxLabel = new QLabel(this->findMtxMode(index));
        previewWidgets.push_back(mtxLabel);

        QGridLayout* mtxGrid = new QGridLayout();
        mtxGrid->setAlignment(Qt::AlignCenter);
        mtxGrid->addWidget(mtxLabel, 0, 0);
        
        int entryNum = 0;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                QLabel* mtxValLabel;
                if (y == 3 && x != 3)
                {
                    mtxValLabel = new QLabel(std::to_string(((s32)this->AggregatedFIFOCache[index]->Params[entryNum++] / 4096.0f)).c_str());
                    mtxValLabel->setStyleSheet("font-weight: bold;");
                }
                else if (y == 3 && x == 3)
                {
                    mtxValLabel = new QLabel("1.0");
                }
                else
                {
                    mtxValLabel = new QLabel("0.0");
                }
                mtxGrid->addWidget(mtxValLabel, y + 1, x);
                previewWidgets.push_back(mtxValLabel);
            }
        }

        ui->previewLayout->addLayout(mtxGrid);
        previewGridLayouts.push_back(mtxGrid);
        break;
    }
    
    case 0x20: // COLOR
    {
        u32 rgb15Color = this->AggregatedFIFOCache[index]->Params[0];
        uint colorRgb = ((rgb15Color & 0x1F) << 19) | (((rgb15Color >> 5) & 0x1F) << 11) | (((rgb15Color >> 10) & 0x1F) << 3) | 0xFF000000;
        QColor color = QColor(QRgb(colorRgb));

        ColorWidget* colorWidget = new ColorWidget(this, color);
        colorWidget->color = color;
        ui->previewLayout->addWidget(colorWidget);
        previewWidgets.push_back(colorWidget);
        break;   
    }

    case 0x22: // TEXCOORD
    {
        u16* texCoords = unpackRawTexcoords(this->AggregatedFIFOCache[index]->Params[0]);
        TexParam* texParam = nullptr;
        for (int i = index - 1; i >= 0; i--)
        {
            if (this->AggregatedFIFOCache[i]->Command == 0x2A) // TEXIMAGE_PARAM
            {
                texParam = parseTexImageParam(this->AggregatedFIFOCache[i]->Params[0]);
                break;
            }
        }
        if (texParam == nullptr)
        {
            texParam = parseTexImageParam(GPU3D::TexParamCache);
        }

        float s = texCoords[0] / 16.f;
        float t = texCoords[1] / 16.f;
        QLabel* texCoordLabel = new QLabel((std::to_string(s) + ", " + std::to_string(t)).c_str());
        ui->previewLayout->addWidget(texCoordLabel);
        previewWidgets.push_back(texCoordLabel);

        // should always be a texture previewer
        if (texturePreviwer != nullptr)
        {
            texturePreviwer->texCoord = new QPointF(s, t);
        }
        break;
    }

    case 0x2A: // TEXIMAGE_PARAM
    {
        std::stringstream str = std::stringstream("");
        if (this->AggregatedFIFOCache[index]->Params[0] == 0)
        {
            QLabel* texParamLabel = new QLabel("No texture.");
            ui->previewLayout->addWidget(texParamLabel);
            previewWidgets.push_back(texParamLabel);
        }
        else
        {
            TexParam* texParam = parseTexImageParam(this->AggregatedFIFOCache[index]->Params[0]);

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

            if (texParam->Format == 7) // RGB15 bitmap
            {
                TexturePreviewer* texturePreviewer = this->getTexturePreviewer(texParam, 0);
                ui->previewLayout->addWidget(texturePreviewer);
                previewWidgets.push_back(texturePreviewer);
                break;
            }
            else
            {
                for (int i = index - 1; i >= 0; i--)
                {
                    if (this->AggregatedFIFOCache[i]->Command == 0x2B) // PLTT_BASE
                    {
                        TexturePreviewer* texturePreviewer = this->getTexturePreviewer(texParam, this->AggregatedFIFOCache[i]->Params[0] & 0x1FFF);
                        ui->previewLayout->addWidget(texturePreviewer);
                        previewWidgets.push_back(texturePreviewer);
                        break;
                    }
                }
            }
        }
        break;
    }
    
    case 0x2B: // PLTT_BASE
    {
        for (int i = index - 1; i >= 0; i--)
        {  
            if (this->AggregatedFIFOCache[i]->Command == 0x2A) // TEXIMAGE_PARAM
            {
                if (this->AggregatedFIFOCache[i]->Params[0] != 0)
                {
                    TexParam* texParam = parseTexImageParam(this->AggregatedFIFOCache[i]->Params[0]);

                    u32 palAddr = this->AggregatedFIFOCache[index]->Params[0];
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

                    TexturePreviewer* texturePreviewer = this->getTexturePreviewer(texParam, this->AggregatedFIFOCache[index]->Params[0] & 0x1FFF);
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
        u32 rgb15Color = this->AggregatedFIFOCache[index]->Params[0];
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

    case 0x40: // BEGIN_VTXS
    {
        texturePreviwer = addVertexGroupTexturePreview(index);
        bool onePoly = this->AggregatedFIFOCache[index]->Params[0] >= 2;
        int numTexCoords = this->AggregatedFIFOCache[index]->Params[0] == 0 ? 3 : 4;
        
        QVector<QPolygonF>* texPolygonList = new QVector<QPolygonF>();
        for (int i = index + 1; i < this->AggregatedFIFOCache.size() && this->AggregatedFIFOCache[i]->Command != 0x41 && this->AggregatedFIFOCache[i]->Command != 0x40;)
        {
            QVector<QPointF>* texCoordsList = new QVector<QPointF>();
            for (int j = 0; (onePoly || j < numTexCoords) && i < this->AggregatedFIFOCache.size() && this->AggregatedFIFOCache[i]->Command != 0x41 && this->AggregatedFIFOCache[i]->Command != 0x40; i++)
            {
                if (this->AggregatedFIFOCache[i]->Command == 0x22) // TEXCOORD
                {
                    u16* texCoords = unpackRawTexcoords(this->AggregatedFIFOCache[i]->Params[0]);
                    TexParam* texParam = nullptr;
                    for (int j = index - 1; j >= 0; j--)
                    {
                        if (this->AggregatedFIFOCache[j]->Command == 0x2A) // TEXIMAGE_PARAM
                        {
                            texParam = parseTexImageParam(this->AggregatedFIFOCache[j]->Params[0]);
                            break;
                        }
                    }
                    if (texParam == nullptr)
                    {
                        texParam = parseTexImageParam(GPU3D::TexParamCache);
                    }

                    float s = (texCoords[0] /*% (16 * (texParam->Width + 1))*/) / 16.f;
                    float t = (texCoords[1] /*% (16 * (texParam->Height + 1))*/) / 16.f;
                    texCoordsList->push_back(QPointF(qreal(s), qreal(t)));
                    j++;
                }
            }
            if (texCoordsList->size() > 0)
            {
                QPointF* closingPoint = new QPointF(texCoordsList[0][0]);
                texCoordsList->push_back(*closingPoint);
                QPolygonF* texPoly = new QPolygonF(*texCoordsList);
                texPoly->translate(1.0, 1.0); // translate it to match the preview window being slightly larger for border effects
                texPolygonList->push_back(*texPoly);
            }
        }
        if (texturePreviwer != nullptr)
        {
            texturePreviwer->texPolys = texPolygonList;
        }
        break;
    }
    }
}

void ThreeDRenderingViewerDialog::on_ThreeDRenderingViewerDialog_rejected()
{
    closeDlg();
}   