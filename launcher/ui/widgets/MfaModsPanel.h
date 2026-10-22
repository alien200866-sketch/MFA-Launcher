// SPDX-License-Identifier: GPL-3.0-only
/*
 *  MFA Launcher - a Prism Launcher fork for Many Fun Adventures
 *  Copyright (C) 2026 AndyMFA (Many Fun Adventures)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include "modplatform/ModIndex.h"
#include "tasks/Task.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class MinecraftInstance;
class ModFolderModel;

/* A narrow, scrollable column of AndyMFA's CurseForge mods shown beside the
 * instance Mods page. Hovering an icon shows the CurseForge summary; clicking
 * one installs the newest version that matches the instance's Minecraft
 * version and mod loader (plus any required dependencies). */
class MfaModsPanel : public QWidget {
    Q_OBJECT

   public:
    explicit MfaModsPanel(MinecraftInstance* instance, ModFolderModel* model, QWidget* parent = nullptr);
    ~MfaModsPanel() override;

   private slots:
    void onItemClicked(QListWidgetItem* item);
    void refreshInstalledMarks();

   private:
    void loadProjectList();
    void loadProjects(const QStringList& ids, const QString& author);
    void populate();
    void loadIcons();
    void setItemIcon(QListWidgetItem* item, const QString& addonId);
    QString tooltipFor(const ModPlatform::IndexedPack& pack, bool installed) const;
    bool isInstalled(const QString& addonId) const;
    void installProject(const ModPlatform::IndexedPack::Ptr& pack);
    void setStatus(const QString& text);

    MinecraftInstance* m_instance;
    ModFolderModel* m_model;

    QLabel* m_status;
    QListWidget* m_list;

    Task::Ptr m_listTask;
    Task::Ptr m_projectsTask;
    Task::Ptr m_iconsTask;

    // Shared between every Mods page opened during this run of the launcher,
    // so the list and icons are only downloaded once per session.
    static QList<ModPlatform::IndexedPack::Ptr> s_projects;
    static QHash<QString, QPixmap> s_icons;
};
