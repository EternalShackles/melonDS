/*
    Copyright 2016-2026 melonDS team

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

#ifndef GAMELIST_H
#define GAMELIST_H

#include <QWidget>

class QTableWidget;
class QPushButton;
class EmuInstance;

class GameList : public QWidget
{
    Q_OBJECT

public:
    explicit GameList(EmuInstance* emuInstance, QWidget* parent = nullptr);

    void scanDirectory(const QString& path);

private slots:
    void onOpenFolder();

private:
    void addGame(const QString& filename);

    EmuInstance* emuInstance;

    QTableWidget* table;
    QPushButton* openFolderButton;
};

#endif // GAMELIST_H
