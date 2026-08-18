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

#include "GameList.h"

#include "EmuInstance.h"
#include "NDSCart.h"
#include "NDS_Header.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using namespace melonDS;

static QString formatSize(qint64 size)
{
    if (size < 1024)
        return QString("%1 B").arg(size);

    if (size < 1024 * 1024)
        return QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);

    if (size < 1024LL * 1024LL * 1024LL)
        return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);

    return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

GameList::GameList(EmuInstance* instance, QWidget* parent)
    : QWidget(parent),
      emuInstance(instance)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* topLayout = new QHBoxLayout();

    QLabel* title = new QLabel("Games", this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);

    openFolderButton = new QPushButton("Open Folder...", this);
    connect(openFolderButton, &QPushButton::clicked,
            this, &GameList::onOpenFolder);

    topLayout->addWidget(title);
    topLayout->addStretch();
    topLayout->addWidget(openFolderButton);

    mainLayout->addLayout(topLayout);

    table = new QTableWidget(this);

    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(
        {
            "Icon",
            "Game Name",
            "Version",
            "NDS File Size",
            "ROM Size"
        }
    );

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);

    table->setIconSize(QSize(32, 32));
    table->verticalHeader()->setDefaultSectionSize(48);

    table->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents
    );

    table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch
    );

    table->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents
    );

    table->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents
    );

    table->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::ResizeToContents
    );

    mainLayout->addWidget(table);

    setLayout(mainLayout);
}

void GameList::onOpenFolder()
{
    QString directory = QFileDialog::getExistingDirectory(
        this,
        "Select Nintendo DS ROM folder"
    );

    if (directory.isEmpty())
        return;

    scanDirectory(directory);
}

void GameList::scanDirectory(const QString& path)
{
    table->setSortingEnabled(false);
    table->setRowCount(0);

    QDir directory(path);

    const QStringList filters =
    {
        "*.nds",
        "*.NDS"
    };

    const QFileInfoList files = directory.entryInfoList(
        filters,
        QDir::Files | QDir::Readable,
        QDir::Name
    );

    for (const QFileInfo& file : files)
        addGame(file.absoluteFilePath());

    table->setSortingEnabled(true);
}

void GameList::addGame(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return;

    const qint64 fileSize = file.size();

    /*
     * For this first implementation we use melonDS's existing ROM parser.
     *
     * ParseROM() creates a CartCommon containing the parsed NDS header and
     * banner, so we don't need to duplicate the NDS header parsing here.
     */
    if (fileSize <= 0 || fileSize > 0x40000000LL)
        return;

    std::unique_ptr<u8[]> romData(new u8[fileSize]);

    if (file.read(reinterpret_cast<char*>(romData.get()), fileSize) != fileSize)
        return;

    file.close();

    std::unique_ptr<NDSCart::CartCommon> cart =
        NDSCart::ParseROM(
            std::move(romData),
            static_cast<u32>(fileSize)
        );

    if (!cart)
        return;

    const NDSHeader& header = cart->GetHeader();
    const NDSBanner* banner = cart->Banner();

    QString gameName;

    if (banner)
    {
        gameName = QString::fromUtf16(
            banner->EnglishTitle
        ).trimmed();
    }

    /*
     * Some homebrew or unusual ROMs may not have a usable banner title.
     * Fall back to the header title.
     */
    if (gameName.isEmpty())
    {
        QString headerTitle = QString::fromLatin1(
            header.GameTitle,
            sizeof(header.GameTitle)
        );

        gameName = headerTitle.trimmed();
    }

    if (gameName.isEmpty())
    {
        gameName = QFileInfo(filename).completeBaseName();
    }

    const int row = table->rowCount();
    table->insertRow(row);

    /*
     * Icon
     */
    QTableWidgetItem* iconItem = new QTableWidgetItem();

    if (banner)
    {
        u32 iconData[32 * 32];

        emuInstance->romIcon(
            banner->Icon,
            banner->Palette,
            iconData
        );

        QImage iconImage(
            reinterpret_cast<u8*>(iconData),
            32,
            32,
            QImage::Format_RGBA8888
        );

        iconItem->setIcon(
            QIcon(QPixmap::fromImage(iconImage.copy()))
        );
    }

    iconItem->setTextAlignment(Qt::AlignCenter);

    /*
     * Game name
     */
    QTableWidgetItem* nameItem =
        new QTableWidgetItem(gameName);

    /*
     * Version
     */
    QTableWidgetItem* versionItem =
        new QTableWidgetItem(
            QString::number(header.ROMVersion)
        );

    /*
     * Actual .nds file size
     */
    QTableWidgetItem* fileSizeItem =
        new QTableWidgetItem(
            formatSize(fileSize)
        );

    fileSizeItem->setData(
        Qt::UserRole,
        fileSize
    );

    /*
     * ROM size from the NDS header.
     *
     * NDS CardSize is expressed as:
     *
     *     128 KB << CardSize
     *
     * melonDS's NDSHeader exposes the raw CardSize field.
     */
    const qint64 romSize =
        128LL * 1024LL * (1LL << header.CardSize);

    QTableWidgetItem* romSizeItem =
        new QTableWidgetItem(
            formatSize(romSize)
        );

    romSizeItem->setData(
        Qt::UserRole,
        romSize
    );

    table->setItem(row, 0, iconItem);
    table->setItem(row, 1, nameItem);
    table->setItem(row, 2, versionItem);
    table->setItem(row, 3, fileSizeItem);
    table->setItem(row, 4, romSizeItem);
}
