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

#include <QDialog>

namespace Ui { class ThreeDRenderingViewerDialog; }
class ThreeDRenderingViewerDialog;

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
    void on_ThreeDRenderingViewerDialog_reset();

private:
    Ui::ThreeDRenderingViewerDialog* ui;
    void update();
};

#endif // THREEDRENDERINGPIPELINEVIEW_H