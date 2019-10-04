#include "torrentcontextmenu.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>

#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QProcess>

#include "core/utils.hpp"
#include "torrenthandle.hpp"
#include "torrentstatus.hpp"
#include "translator.hpp"

namespace fs = std::filesystem;
namespace lt = libtorrent;

using pt::TorrentContextMenu;

struct TorrentMoveFileDialog : QFileDialog
{
    using QFileDialog::QFileDialog;

    QList<pt::TorrentHandle*> torrents;
};

TorrentContextMenu::TorrentContextMenu(QWidget* parent, QList<pt::TorrentHandle*> const& torrents)
    : QMenu(parent),
      m_parent(parent),
      m_torrents(torrents)
{
    m_pause              = new QAction(i18n("pause"));
    m_resume             = new QAction(i18n("resume"));
    m_resumeForce        = new QAction(i18n("resume_force"));
    m_move               = new QAction(i18n("move"));
    m_remove             = new QAction(i18n("remove_torrent"));
    m_removeFiles        = new QAction(i18n("remove_torrent_and_files"));
    m_queueUp            = new QAction(i18n("up"));
    m_queueDown          = new QAction(i18n("down"));
    m_queueTop           = new QAction(i18n("top"));
    m_queueBottom        = new QAction(i18n("bottom"));
    m_copyHash           = new QAction(i18n("copy_info_hash"));
    m_openExplorer       = new QAction(i18n("open_in_explorer"));
    m_forceRecheck       = new QAction(i18n("force_recheck"));
    m_forceReannounce    = new QAction(i18n("force_reannounce"));
    m_sequentialDownload = new QAction(i18n("sequential_download"));

    m_queueMenu = new QMenu(i18n("queuing"));
    m_queueMenu->addAction(m_queueUp);
    m_queueMenu->addAction(m_queueDown);
    m_queueMenu->addSeparator();
    m_queueMenu->addAction(m_queueTop);
    m_queueMenu->addAction(m_queueBottom);

    m_removeMenu = new QMenu(i18n("remove"));
    m_removeMenu->addAction(m_remove);
    m_removeMenu->addAction(m_removeFiles);

    bool allPaused = std::all_of(
        m_torrents.begin(),
        m_torrents.end(),
        [](TorrentHandle* th)
        {
            return th->status().paused;
        });

    bool allRunning = std::all_of(
        m_torrents.begin(),
        m_torrents.end(),
        [](TorrentHandle* th)
        {
            return !th->status().paused;
        });

    // If not all torrents in the selection are paused - add the ability
    // to pause them. Already paused torrents are unaffected.
    if (!allPaused)
    {
        this->addAction(m_pause);
    }

    // If not all torrents in the selection are running - add the ability
    // to resume them. Already running torrents are unaffected.
    if (!allRunning)
    {
        this->addAction(m_resume);
        this->addAction(m_resumeForce);
    }

    this->addSeparator();
    this->addAction(m_forceReannounce);
    this->addAction(m_forceRecheck);

    if (torrents.size() == 1)
    {
        this->addAction(m_sequentialDownload);
    }

    this->addSeparator();
    this->addAction(m_move);
    this->addSeparator();
    this->addMenu(m_removeMenu);
    this->addSeparator();
    this->addMenu(m_queueMenu);
    this->addSeparator();
    this->addAction(m_copyHash);
    this->addAction(m_openExplorer);

    for (TorrentHandle* torrent : torrents)
    {
        QObject::connect(m_pause,           &QAction::triggered,
                         torrent,           &TorrentHandle::pause);

        QObject::connect(m_resume,          &QAction::triggered,
                         torrent,           &TorrentHandle::resume);

        QObject::connect(m_resumeForce,     &QAction::triggered,
                         torrent,           &TorrentHandle::resumeForce);

        QObject::connect(m_forceReannounce, &QAction::triggered,
                         [torrent]() { torrent->forceReannounce(); });

        QObject::connect(m_forceRecheck,    &QAction::triggered,
                         torrent,           &TorrentHandle::forceRecheck);

        QObject::connect(m_remove,          &QAction::triggered,
                         torrent,           &TorrentHandle::remove);

        QObject::connect(m_removeFiles,     &QAction::triggered,
                         torrent,           &TorrentHandle::removeFiles);

        QObject::connect(m_queueUp,         &QAction::triggered,
                         torrent,           &TorrentHandle::queueUp);

        QObject::connect(m_queueDown,       &QAction::triggered,
                         torrent,           &TorrentHandle::queueDown);

        QObject::connect(m_queueTop,        &QAction::triggered,
                         torrent,           &TorrentHandle::queueTop);

        QObject::connect(m_queueBottom,     &QAction::triggered,
                         torrent,           &TorrentHandle::queueBottom);
    }

    QObject::connect(m_move,         &QAction::triggered,
                     this,           &TorrentContextMenu::move);

    QObject::connect(m_copyHash,     &QAction::triggered,
                     this,           &TorrentContextMenu::copyInfoHash);

    QObject::connect(m_openExplorer, &QAction::triggered,
                     this,           &TorrentContextMenu::openExplorer);
}

void TorrentContextMenu::copyInfoHash()
{
    std::stringstream hashes;

    for (TorrentHandle* torrent : m_torrents)
    {
        hashes << "," << torrent->infoHash();
    }

    QString hashList = QString::fromStdString(hashes.str().substr(1));

    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(hashList);
}

void TorrentContextMenu::move()
{
    auto dlg = new TorrentMoveFileDialog(m_parent);
    dlg->torrents = m_torrents;

    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly);
    dlg->open();

    QObject::connect(
        dlg, &QDialog::finished,
        [dlg](int result)
        {
            if (result)
            {
                for (TorrentHandle* torrent : dlg->torrents)
                {
                    torrent->moveStorage(dlg->selectedFiles().first());
                }
            }

            dlg->deleteLater();
        });
}

void TorrentContextMenu::openExplorer()
{
    TorrentHandle* th = m_torrents.at(0);
    TorrentStatus  ts = th->status();

    fs::path savePath = ts.savePath.toStdWString();
    fs::path path     = savePath / ts.name.toStdWString();

    QStringList param;

    if (!fs::is_directory(path))
    {
        param += QLatin1String("/select,");
    }

    param += QString::fromStdWString(fs::absolute(path).wstring());

    QProcess::startDetached("explorer.exe", param);
}

/*
void TorrentContextMenu::SequentialDownload(wxCommandEvent& WXUNUSED(event))
{
    for (lt::torrent_handle& th : m_state->selected_torrents)
    {
        bool isSequential = (th.flags() & lt::torrent_flags::sequential_download) == lt::torrent_flags::sequential_download;

        if (isSequential)
        {
            th.unset_flags(lt::torrent_flags::sequential_download);
        }
        else
        {
            th.set_flags(lt::torrent_flags::sequential_download);
        }
    }
}
*/
