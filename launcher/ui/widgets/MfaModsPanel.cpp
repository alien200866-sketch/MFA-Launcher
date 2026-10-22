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

#include "MfaModsPanel.h"

#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "Application.h"
#include "QObjectPtr.h"
#include "ResourceDownloadTask.h"
#include "Version.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"

#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlamePackIndex.h"

#include "net/NetJob.h"
#include "net/Request.h"

#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"

#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {

// The list of projects lives in the MFA-Launcher GitHub repository, so new mods
// can be added there without anyone having to reinstall the launcher.
const QString s_projectListUrl =
    QStringLiteral("https://raw.githubusercontent.com/alien200866-sketch/MFA-Launcher/develop/mfa-projects.json");

// Only projects by this CurseForge author are shown. This guards against a
// mistyped project ID in the list showing somebody else's mod.
const QString s_defaultAuthor = QStringLiteral("AndyMFAMinecraft");

// Used when the online list can't be fetched (offline, GitHub down, ...).
const QStringList s_defaultProjectIds = {
    "1758509",  // Back to Level Zero
    "1757528",  // Extreme Explosions 2
    "1755713",  // Far Flung Carved Posts Add-on
    "1755050",  // Far Flung Flowers Add-on
    "1754236",  // Far Flung Woodworking Add-on
    "1750491",  // Far Flung Forests
    "1745605",  // Fruit Machines - The Grumpy Banker Add-on
    "1744925",  // Minecatraz Prison Island
    "1741846",  // Teleportals Are Here
    "1741069",  // Item Sticker Album
    "1739956",  // Pillager Outpost Radar
    "1739748",  // Keep the World Tidy
    "1739245",  // Neon Signs Unlimited
    "1739159",  // Must Have Taller Trees
    "1735156",  // Extreme Explosions
    "1735060",  // Dead Explorers
    "1734230",  // Enchantment Transference
    "1733257",  // Abandoned Tunnels
    "1732540",  // Farming the Land
    "1731692",  // Abandoned Well
    "1730982",  // Wanted For Kills
    "1730131",  // The Grumpy Banker
    "1726153",  // Pyrentium
    "1724874",  // Insane Pickaxe
    "1724310",  // Invisiblock
};

constexpr int s_iconSize = 52;
constexpr int s_panelWidth = 86;
constexpr int s_addonIdRole = Qt::UserRole;
constexpr int s_indexRole = Qt::UserRole + 1;

QPixmap placeholderPixmap()
{
    return QIcon::fromTheme("loadermods").pixmap(s_iconSize, s_iconSize);
}

// Draws a small green tick badge in the bottom-right corner of installed mods.
QPixmap withInstalledBadge(const QPixmap& base)
{
    QPixmap result = base.scaled(s_iconSize, s_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal size = s_iconSize * 0.42;
    const QRectF badge(result.width() - size - 1, result.height() - size - 1, size, size);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(QColor(46, 160, 67));
    painter.drawEllipse(badge);

    QPainterPath tick;
    tick.moveTo(badge.left() + size * 0.26, badge.top() + size * 0.52);
    tick.lineTo(badge.left() + size * 0.43, badge.top() + size * 0.70);
    tick.lineTo(badge.left() + size * 0.75, badge.top() + size * 0.32);
    painter.setPen(QPen(Qt::white, size * 0.14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(tick);
    return result;
}

}  // namespace

QList<ModPlatform::IndexedPack::Ptr> MfaModsPanel::s_projects;
QHash<QString, QPixmap> MfaModsPanel::s_icons;

MfaModsPanel::MfaModsPanel(MinecraftInstance* instance, ModFolderModel* model, QWidget* parent)
    : QWidget(parent), m_instance(instance), m_model(model)
{
    setFixedWidth(s_panelWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* title = new QLabel(tr("MFA Mods"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    title->setToolTip(tr("Mods by AndyMFA. Hover for details, click to install."));
    layout->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::TopToBottom);
    m_list->setWrapping(false);
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setIconSize(QSize(s_iconSize, s_iconSize));
    m_list->setGridSize(QSize(s_iconSize + 14, s_iconSize + 12));
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMouseTracking(true);
    m_list->viewport()->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_list, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignCenter);
    QFont statusFont = m_status->font();
    statusFont.setPointSizeF(statusFont.pointSizeF() * 0.85);
    m_status->setFont(statusFont);
    layout->addWidget(m_status);

    connect(m_list, &QListWidget::itemClicked, this, &MfaModsPanel::onItemClicked);

    // Keep the "installed" ticks up to date as mods are added or removed.
    connect(m_model, &QAbstractItemModel::modelReset, this, &MfaModsPanel::refreshInstalledMarks);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MfaModsPanel::refreshInstalledMarks);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MfaModsPanel::refreshInstalledMarks);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &MfaModsPanel::refreshInstalledMarks);

    if (s_projects.isEmpty()) {
        loadProjectList();
    } else {
        populate();
    }
}

MfaModsPanel::~MfaModsPanel()
{
    for (const auto& task : { m_listTask, m_projectsTask, m_iconsTask }) {
        if (task && task->isRunning()) {
            task->abort();
        }
    }
}

void MfaModsPanel::setStatus(const QString& text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void MfaModsPanel::loadProjectList()
{
    setStatus(tr("Loading..."));

    auto job = makeShared<NetJob>(QStringLiteral("MFA::ProjectList"), APPLICATION->network());
    auto [request, response] = Net::Request::makeByteArray(QUrl(s_projectListUrl));
    job->addNetAction(request);

    connect(job.get(), &Task::succeeded, this, [this, response] {
        QStringList ids;
        QString author = s_defaultAuthor;

        const auto doc = QJsonDocument::fromJson(*response);
        if (doc.isObject()) {
            const auto obj = doc.object();
            if (obj.contains("author")) {
                author = obj.value("author").toString(s_defaultAuthor);
            }
            for (const auto& value : obj.value("projects").toArray()) {
                const auto id = value.isString() ? value.toString() : QString::number(value.toInteger());
                if (!id.isEmpty() && id != "0") {
                    ids << id;
                }
            }
        }

        if (ids.isEmpty()) {
            qWarning() << "MFA mod list was empty or unreadable, using the built-in list";
            ids = s_defaultProjectIds;
        }
        loadProjects(ids, author);
    });
    connect(job.get(), &Task::failed, this, [this](const QString& reason) {
        qWarning() << "Could not fetch the MFA mod list, using the built-in list:" << reason;
        loadProjects(s_defaultProjectIds, s_defaultAuthor);
    });

    m_listTask = job;
    job->start();
}

void MfaModsPanel::loadProjects(const QStringList& ids, const QString& author)
{
    auto [task, response] = FlameAPI::get().getProjects(ids);

    connect(task.get(), &Task::succeeded, this, [this, response, ids, author] {
        QHash<QString, ModPlatform::IndexedPack::Ptr> byId;

        const auto data = QJsonDocument::fromJson(*response).object().value("data").toArray();
        for (const auto& entry : data) {
            auto pack = std::make_shared<ModPlatform::IndexedPack>();
            if (auto res = Flame::Parse::loadIndexedPack(*pack, entry.toObject()); !res) {
                qWarning() << "Could not read an MFA project from CurseForge:" << res.error();
                continue;
            }

            const bool byAuthor = std::any_of(pack->authors.cbegin(), pack->authors.cend(), [&author](const auto& a) {
                return a.name.compare(author, Qt::CaseInsensitive) == 0;
            });
            if (!byAuthor) {
                qWarning() << "Skipping CurseForge project" << pack->addonId << pack->name << "- not by" << author;
                continue;
            }
            byId.insert(pack->addonId.toString(), pack);
        }

        // Keep the order from the list file.
        s_projects.clear();
        for (const auto& id : ids) {
            if (auto it = byId.constFind(id); it != byId.cend()) {
                s_projects.append(it.value());
            }
        }
        populate();
    });
    connect(task.get(), &Task::failed, this, [this](const QString& reason) {
        qWarning() << "Could not load MFA projects from CurseForge:" << reason;
        setStatus(tr("Couldn't reach CurseForge."));
    });

    m_projectsTask = task;
    task->start();
}

void MfaModsPanel::populate()
{
    m_list->clear();

    if (s_projects.isEmpty()) {
        setStatus(tr("No mods found."));
        return;
    }
    setStatus({});

    for (int i = 0; i < s_projects.size(); ++i) {
        const auto& pack = s_projects.at(i);
        const auto addonId = pack->addonId.toString();

        auto* item = new QListWidgetItem(m_list);
        item->setData(s_addonIdRole, addonId);
        item->setData(s_indexRole, i);
        item->setSizeHint(QSize(s_iconSize + 14, s_iconSize + 12));
        item->setToolTip(tooltipFor(*pack, isInstalled(addonId)));
        setItemIcon(item, addonId);
    }

    loadIcons();
}

void MfaModsPanel::loadIcons()
{
    auto job = makeShared<NetJob>(QStringLiteral("MFA::Icons"), APPLICATION->network());
    bool anything = false;

    for (const auto& pack : s_projects) {
        const auto addonId = pack->addonId.toString();
        if (s_icons.contains(addonId) || pack->logoUrl.isEmpty()) {
            continue;
        }

        auto [request, response] = Net::Request::makeByteArray(QUrl(pack->logoUrl));
        connect(request.get(), &Task::succeeded, this, [this, response, addonId] {
            QPixmap pixmap;
            if (!pixmap.loadFromData(*response)) {
                return;
            }
            s_icons.insert(addonId, pixmap.scaled(s_iconSize, s_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            for (int row = 0; row < m_list->count(); ++row) {
                auto* item = m_list->item(row);
                if (item->data(s_addonIdRole).toString() == addonId) {
                    setItemIcon(item, addonId);
                }
            }
        });
        job->addNetAction(request);
        anything = true;
    }

    if (!anything) {
        return;
    }
    m_iconsTask = job;
    job->start();
}

void MfaModsPanel::setItemIcon(QListWidgetItem* item, const QString& addonId)
{
    QPixmap pixmap = s_icons.value(addonId);
    if (pixmap.isNull()) {
        pixmap = placeholderPixmap();
    }
    if (isInstalled(addonId)) {
        pixmap = withInstalledBadge(pixmap);
    }
    item->setIcon(QIcon(pixmap));
}

QString MfaModsPanel::tooltipFor(const ModPlatform::IndexedPack& pack, bool installed) const
{
    QString text = QStringLiteral("<div style='max-width: 320px;'><p><b>%1</b></p>").arg(pack.name.toHtmlEscaped());
    if (!pack.description.isEmpty()) {
        text += QStringLiteral("<p>%1</p>").arg(pack.description.toHtmlEscaped());
    }
    if (installed) {
        text += QStringLiteral("<p><i>%1</i></p>").arg(tr("Installed in this instance. Click to reinstall or update."));
    } else {
        text += QStringLiteral("<p><i>%1</i></p>").arg(tr("Click to install."));
    }
    text += QStringLiteral("</div>");
    return text;
}

bool MfaModsPanel::isInstalled(const QString& addonId) const
{
    if (m_model == nullptr) {
        return false;
    }
    for (const auto* mod : m_model->allMods()) {
        const auto meta = mod->metadata();
        if (meta && meta->provider == ModPlatform::ResourceProvider::FLAME && meta->projectId.toString() == addonId) {
            return true;
        }
    }
    return false;
}

void MfaModsPanel::refreshInstalledMarks()
{
    for (int row = 0; row < m_list->count(); ++row) {
        auto* item = m_list->item(row);
        const auto addonId = item->data(s_addonIdRole).toString();
        const int index = item->data(s_indexRole).toInt();
        setItemIcon(item, addonId);
        if (index >= 0 && index < s_projects.size()) {
            item->setToolTip(tooltipFor(*s_projects.at(index), isInstalled(addonId)));
        }
    }
}

void MfaModsPanel::onItemClicked(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const int index = item->data(s_indexRole).toInt();
    if (index < 0 || index >= s_projects.size()) {
        return;
    }
    installProject(s_projects.at(index));
}

void MfaModsPanel::installProject(const ModPlatform::IndexedPack::Ptr& pack)
{
    if (m_instance == nullptr || m_model == nullptr) {
        return;
    }

    if (m_instance->isRunning()) {
        auto response = CustomMessageBox::selectable(this, tr("Game is running"),
                                                     tr("Installing mods while the game is running may cause problems.\n"
                                                        "Do you want to carry on anyway?"),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();
        if (response != QMessageBox::Yes) {
            return;
        }
    }

    auto* profile = m_instance->getPackProfile();
    const auto loaders = profile->getModLoaders();
    if (!loaders.has_value()) {
        CustomMessageBox::selectable(this, tr("No mod loader"),
                                     tr("This instance doesn't have a mod loader (such as Forge, NeoForge or Fabric) yet.\n"
                                        "Add one under Version, then try again."),
                                     QMessageBox::Information)
            ->exec();
        return;
    }
    const QString mcVersion = profile->getComponentVersion("net.minecraft");

    // 1. Find the files CurseForge has for this Minecraft version and loader.
    struct VersionState {
        QVector<ModPlatform::IndexedVersion> versions;
        bool failed = false;
        QString reason;
    };
    auto state = std::make_shared<VersionState>();

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
    callbacks.onSucceed = [state](QVector<ModPlatform::IndexedVersion>& versions) { state->versions = versions; };
    callbacks.onFail = [state](const QString& reason, int) {
        state->failed = true;
        state->reason = reason;
    };
    callbacks.onAbort = [state] { state->failed = true; };

    ResourceAPI::VersionSearchArgs args{
        .pack = pack,
        .mcVersions = std::vector<Version>{ Version(mcVersion) },
        .loaders = loaders,
        .resourceType = ModPlatform::ResourceType::Mod,
    };

    auto versionsTask = FlameAPI::get().getProjectVersions(args, callbacks);
    if (!versionsTask) {
        return;
    }
    {
        ProgressDialog progress(this);
        progress.setWindowTitle(tr("Looking up %1...").arg(pack->name));
        progress.execWithTask(versionsTask.get());
    }

    if (state->failed) {
        if (!state->reason.isEmpty()) {
            CustomMessageBox::selectable(this, tr("Couldn't reach CurseForge"), state->reason, QMessageBox::Warning)->exec();
        }
        return;
    }

    const auto latest =
        FlameAPI::getLatestVersion(state->versions, profile->getModLoadersList(), ModPlatform::ModLoaderTypes(), true);
    if (!latest.has_value() || !latest->addonId.isValid()) {
        QStringList loaderNames;
        for (auto loader : profile->getModLoadersList()) {
            loaderNames << ModPlatform::getModLoaderAsString(loader);
        }
        CustomMessageBox::selectable(this, tr("No matching version"),
                                     tr("%1 doesn't have a version for this instance yet (Minecraft %2, %3).")
                                         .arg(pack->name, mcVersion, loaderNames.join(" / ")),
                                     QMessageBox::Information)
            ->exec();
        return;
    }

    // 2. Work out any required mods that aren't installed yet.
    QList<shared_qobject_ptr<ResourceDownloadTask>> downloads;
    downloads.append(makeShared<ResourceDownloadTask>(pack, latest.value(), m_model, true, QStringLiteral("standalone")));

    QStringList dependencyNames;
    if (!APPLICATION->settings()->get("ModDependenciesDisabled").toBool()) {
        QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> selected{
            std::make_shared<GetModDependenciesTask::PackDependency>(pack, latest.value())
        };
        auto depTask = makeShared<GetModDependenciesTask>(m_instance, m_model, selected);

        ProgressDialog progress(this);
        progress.setWindowTitle(tr("Checking for required mods..."));
        if (progress.execWithTask(depTask.get()) == QDialog::Accepted) {
            const auto extraInfo = depTask->getExtraInfo();
            for (const auto& dep : depTask->getDependecies()) {
                const auto info = extraInfo.value(dep->pack->addonId.toString());
                if (info.maybeInstalled) {
                    continue;
                }
                downloads.append(makeShared<ResourceDownloadTask>(dep->pack, dep->version, m_model, true, QStringLiteral("dependency"),
                                                                  info.requiredByIds.value(0, pack->addonId.toString())));
                dependencyNames << dep->pack->name;
            }
        }
    }

    // 3. Confirm.
    QString message = tr("Install %1 into this instance?\n\nVersion: %2\nFile: %3")
                          .arg(pack->name, latest->version.isEmpty() ? latest->fileName : latest->version, latest->fileName);
    if (!dependencyNames.isEmpty()) {
        message += tr("\n\nThese required mods will also be installed:\n- %1").arg(dependencyNames.join("\n- "));
    }
    auto answer = CustomMessageBox::selectable(this, tr("Install mod"), message, QMessageBox::Question,
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
                      ->exec();
    if (answer != QMessageBox::Yes) {
        return;
    }

    // 4. Download and install.
    ConcurrentTask tasks(tr("Installing %1").arg(pack->name), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    connect(&tasks, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    for (const auto& download : downloads) {
        tasks.addTask(download);
    }

    ProgressDialog progress(this);
    progress.execWithTask(&tasks);

    m_model->update();
    refreshInstalledMarks();
}
