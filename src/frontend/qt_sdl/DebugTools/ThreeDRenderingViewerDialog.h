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

#ifndef THREEDRENDERINGPIPELINEVIEW_H
#define THREEDRENDERINGPIPELINEVIEW_H

#include <vector>
#include <QDialog>

#include "ThreeDRenderingWidgets.h"

namespace Ui { class ThreeDRenderingViewerDialog; }
class ThreeDRenderingViewerDialog;

struct TexParam
{
    u32 Vramaddr;
    s32 Width;
    s32 Height;
    bool WrapX;
    bool WrapY;
    bool FlipX;
    bool FlipY;
    s32 Format;
    u32 Alpha0;
    s32 TransformationMode;
};

struct CmdAggregatedEntry
{
    u32 Params[32];
    u8 Command;
    
    CmdAggregatedEntry(u8 command, u32* params, int paramsSize = 32)
    {
        Command = command;
        memcpy(Params, params, paramsSize * sizeof(u32));
    }
    ~CmdAggregatedEntry()
    {
        free(Params);
    }
};

class ThreeDRenderingViewerDialog : QDialog
{
    Q_OBJECT

public:
    explicit ThreeDRenderingViewerDialog(QWidget* parent, bool emuActive);
    ~ThreeDRenderingViewerDialog();

    static ThreeDRenderingViewerDialog* currentDlg;
    static ThreeDRenderingViewerDialog* openDlg(QWidget* parent, bool emuActive)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new ThreeDRenderingViewerDialog(parent, emuActive);
        currentDlg->show();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

signals:
    void highlightPolygon();

private slots:
    void on_ThreeDRenderingViewerDialog_rejected();
    void on_updateButton_clicked();
    void on_stepFrameButton_clicked();
    void on_unpauseButton_clicked();
    void on_pipelineCommandsTree_itemSelectionChanged();

private:
    u8 VRAMFlat_TextureCache[512*1024] = { };
    u8 VRAMFlat_TexPalCache[128*1024] = { };
    std::vector<CmdAggregatedEntry*> AggregatedFIFOCache = { };
    std::vector<s32*> AggregatedProjectionMatrixCache = { };
    std::vector<s32*> AggregatedPositionMatrixCache = { };
    std::vector<s32*> AggregatedVectorMatrixCache = { };
    std::vector<s32*> AggregatedTextureMatrixCache = { };

    Ui::ThreeDRenderingViewerDialog* ui;
    void updatePipeline();
    TexturePreviewer* getTexturePreviewer(TexParam* texParam, u32 texPalAddr);
    TexturePreviewer* addVertexGroupTexturePreview(int index);
    const char* findMtxMode(int index);
};

#endif // THREEDRENDERINGPIPELINEVIEW_H