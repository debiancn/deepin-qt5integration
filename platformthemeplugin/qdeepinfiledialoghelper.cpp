#include "qdeepinfiledialoghelper.h"
#include "qdeepintheme.h"

#include "filedialogmanager_interface.h"
#include "filedialog_interface.h"

#include <QDialog>
#include <QEvent>
#include <QWindow>
#include <QApplication>
#include <QDBusObjectPath>
#include <QFileDialog>
#include <QX11Info>
#include <QDebug>

#include <private/qwidgetwindow_p.h>
#include <private/qguiapplication_p.h>

#include <X11/Xlib.h>

QT_BEGIN_NAMESPACE
#define DIALOG_SERVICE "com.deepin.filemanager.filedialog"
#define DIALOG_CALL(Fun) ({if(nativeDialog) nativeDialog->Fun; else qtDialog->Fun;})

QList<QUrl> stringList2UrlList(const QStringList &list)
{
    QList<QUrl> urlList;

    for (const QString str : list)
        urlList << str;

    return urlList;
}

QStringList urlList2StringList(const QList<QUrl> &list)
{
    QStringList stringList;

    for (const QUrl &url : list)
        stringList << url.toString();

    return stringList;
}

DFileDialogManager *QDeepinFileDialogHelper::manager = Q_NULLPTR;

QDeepinFileDialogHelper::QDeepinFileDialogHelper()
{

}

QDeepinFileDialogHelper::~QDeepinFileDialogHelper()
{
    DIALOG_CALL(deleteLater());

    if (auxiliaryWindow)
        auxiliaryWindow->deleteLater();
}

bool QDeepinFileDialogHelper::show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent)
{
    ensureDialog();
    applyOptions();

    if (nativeDialog) {
        nativeDialog->setParent(parent);
        auxiliaryWindow->setParent(parent);
        auxiliaryWindow->setFlags(flags);
        auxiliaryWindow->setModality(modality);

        if (modality != Qt::NonModal) {
            QGuiApplicationPrivate::showModalWindow(auxiliaryWindow);
        }
    } else {
        qtDialog->setAttribute(Qt::WA_NativeWindow);

        if (parent) {
            if (parent->inherits("QWidgetWindow")) {
                qtDialog->setParent(static_cast<QWidgetWindow*>(parent)->widget());
            } else {
                qtDialog->windowHandle()->setParent(parent);
            }
        }

        qtDialog->setWindowModality(modality);
        qtDialog->setWindowFlags(flags | qtDialog->windowFlags());
    }

    DIALOG_CALL(show());

    if (nativeDialog && parent) {
        XSetTransientForHint(QX11Info::display(), nativeDialog->winId(), parent->winId());
    }

    return true;
}

void QDeepinFileDialogHelper::exec()
{
    qDebug() << "exec";

    ensureDialog();
    applyOptions();

    if (nativeDialog) {
        // block input to the window, allow input to other GTK dialogs
        QEventLoop loop;
        connect(this, SIGNAL(accept()), &loop, SLOT(quit()));
        connect(this, SIGNAL(reject()), &loop, SLOT(quit()));
        loop.exec();
    } else {
        QWindow *modalWindow = qApp->modalWindow();

        if (Q_LIKELY(modalWindow->inherits("QWidgetWindow")
                     && qobject_cast<QFileDialog*>(static_cast<QWidgetWindow*>(modalWindow)->widget()))) {
            QGuiApplicationPrivate::hideModalWindow(modalWindow);
        }

        qtDialog->exec();
    }
}

void QDeepinFileDialogHelper::hide()
{
    qDebug() << "hide";

    ensureDialog();

    DIALOG_CALL(hide());

    if (auxiliaryWindow) {
        QGuiApplicationPrivate::hideModalWindow(auxiliaryWindow);
    }
}

bool QDeepinFileDialogHelper::defaultNameFilterDisables() const
{
    qDebug() << __FUNCTION__;

    return true;
}

void QDeepinFileDialogHelper::setDirectory(const QUrl &directory)
{
    qDebug() << __FUNCTION__ << directory;

    ensureDialog();

    DIALOG_CALL(setDirectoryUrl(directory.toString()));
}

QUrl QDeepinFileDialogHelper::directory() const
{
    qDebug() << __FUNCTION__;

    ensureDialog();

    if (nativeDialog)
        return QUrl(nativeDialog->directoryUrl());

    return qtDialog->directoryUrl();
}

void QDeepinFileDialogHelper::selectFile(const QUrl &fileUrl)
{
    qDebug() << __FUNCTION__ << fileUrl;

    ensureDialog();

    if (nativeDialog)
        nativeDialog->selectUrl(fileUrl.toString());
    else
        qtDialog->selectUrl(fileUrl);
}

QList<QUrl> QDeepinFileDialogHelper::selectedFiles() const
{
    qDebug() << __FUNCTION__;

    ensureDialog();

    if (nativeDialog)
        return stringList2UrlList(nativeDialog->selectedUrls());

    return qtDialog->selectedUrls();
}

void QDeepinFileDialogHelper::setFilter()
{
    qDebug() << __FUNCTION__;

    ensureDialog();

    DIALOG_CALL(setFilter(options()->filter()));
}

void QDeepinFileDialogHelper::selectNameFilter(const QString &filter)
{
    qDebug() << __FUNCTION__ << filter;

    ensureDialog();

    DIALOG_CALL(selectNameFilter(filter));
}

QString QDeepinFileDialogHelper::selectedNameFilter() const
{
    qDebug() << __FUNCTION__;

    ensureDialog();

    if (nativeDialog)
        return nativeDialog->selectedNameFilter();

    return qtDialog->selectedNameFilter();
}

void QDeepinFileDialogHelper::initDBusFileDialogManager()
{
    if (manager)
        return;

    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(DIALOG_SERVICE).value()
            || QFile::exists("/usr/bin/dde-file-manager")) {
        manager = new DFileDialogManager(DIALOG_SERVICE, "/com/deepin/filemanager/filedialogmanager", QDBusConnection::sessionBus());
    }
}

void QDeepinFileDialogHelper::ensureDialog() const
{
    if (nativeDialog || qtDialog)
        return;

    if (manager) {
        QDBusPendingReply<QDBusObjectPath> reply = manager->createDialog(QString());
        reply.waitForFinished();
        const QString &path = reply.value().path();

        if (path.isEmpty()) {
            qWarning("Can not create native dialog, Will be use QFileDialog");
        } else {
            nativeDialog = new DFileDialogHandle(DIALOG_SERVICE, path, QDBusConnection::sessionBus());
            auxiliaryWindow = new QWindow();

            connect(nativeDialog, &QObject::destroyed, auxiliaryWindow, &QWindow::deleteLater);
            connect(nativeDialog, &DFileDialogHandle::accepted, this, &QDeepinFileDialogHelper::accept);
            connect(nativeDialog, &DFileDialogHandle::rejected, this, &QDeepinFileDialogHelper::reject);
        }
    }

    if (!nativeDialog) {
        QDeepinTheme::m_usePlatformNativeDialog = false;
        qtDialog = new QFileDialog();
        QDeepinTheme::m_usePlatformNativeDialog = true;

        connect(qtDialog, &QFileDialog::accepted, this, &QDeepinFileDialogHelper::accept);
        connect(qtDialog, &QFileDialog::rejected, this, &QDeepinFileDialogHelper::reject);
    }
}

void QDeepinFileDialogHelper::applyOptions()
{
    QFileDialogOptions *options = this->options().data();

    for (int i = 0; i < QFileDialogOptions::DialogLabelCount; ++i) {
        if (options->isLabelExplicitlySet((QFileDialogOptions::DialogLabel)i)) {
            if (nativeDialog)
                nativeDialog->setLabelText(i, options->labelText((QFileDialogOptions::DialogLabel)i));
            else
                qtDialog->setLabelText((QFileDialog::DialogLabel)i, options->labelText((QFileDialogOptions::DialogLabel)i));
        }
    }

    DIALOG_CALL(setFilter(options->filter()));
    DIALOG_CALL(setWindowTitle(options->windowTitle()));
    DIALOG_CALL(setViewMode((QFileDialog::ViewMode)options->viewMode()));
    DIALOG_CALL(setFileMode((QFileDialog::FileMode)options->fileMode()));
    DIALOG_CALL(setAcceptMode((QFileDialog::AcceptMode)options->acceptMode()));
    DIALOG_CALL(setNameFilters(options->nameFilters()));
    DIALOG_CALL(setOptions((QFileDialog::Options)(int)options->options()));

    setDirectory(options->initialDirectory());

    foreach (const QUrl &filename, options->initiallySelectedFiles())
        selectFile(filename);

    selectNameFilter(options->initiallySelectedNameFilter());
}

QT_END_NAMESPACE
