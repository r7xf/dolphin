/***************************************************************************
 *   Copyright (C) 2009 by Peter Penz <peter.penz19@gmail.com>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "informationpanelcontent.h"

#include <KIO/JobUiDelegate>
#include <KIO/PreviewJob>
#include <KIconEffect>
#include <KIconLoader>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KSeparator>
#include <KStringHandler>

#include <QIcon>
#include <QTextDocument>

#include <Baloo/FileMetaDataWidget>

#include <panels/places/placesitem.h>
#include <panels/places/placesitemmodel.h>

#include <Phonon/BackendCapabilities>
#include <Phonon/MediaObject>

#include <QLabel>
#include <QScrollArea>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QStyle>

#include "dolphin_informationpanelsettings.h"
#include "phononwidget.h"
#include "pixmapviewer.h"

InformationPanelContent::InformationPanelContent(QWidget* parent) :
    QWidget(parent),
    m_item(),
    m_previewJob(nullptr),
    m_outdatedPreviewTimer(nullptr),
    m_preview(nullptr),
    m_phononWidget(nullptr),
    m_nameLabel(nullptr),
    m_metaDataWidget(nullptr),
    m_metaDataArea(nullptr),
    m_placesItemModel(nullptr)
{
    parent->installEventFilter(this);

    // Initialize timer for disabling an outdated preview with a small
    // delay. This prevents flickering if the new preview can be generated
    // within a very small timeframe.
    m_outdatedPreviewTimer = new QTimer(this);
    m_outdatedPreviewTimer->setInterval(300);
    m_outdatedPreviewTimer->setSingleShot(true);
    connect(m_outdatedPreviewTimer, &QTimer::timeout,
            this, &InformationPanelContent::markOutdatedPreview);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // preview
    const int minPreviewWidth = KIconLoader::SizeEnormous + KIconLoader::SizeMedium;

    m_preview = new PixmapViewer(parent);
    m_preview->setMinimumWidth(minPreviewWidth);
    m_preview->setMinimumHeight(KIconLoader::SizeEnormous);

    m_phononWidget = new PhononWidget(parent);
    m_phononWidget->hide();
    m_phononWidget->setMinimumWidth(minPreviewWidth);
    connect(m_phononWidget, &PhononWidget::hasVideoChanged,
            this, &InformationPanelContent::slotHasVideoChanged);

    // name
    m_nameLabel = new QLabel(parent);
    QFont font = m_nameLabel->font();
    font.setBold(true);
    m_nameLabel->setFont(font);
    m_nameLabel->setTextFormat(Qt::PlainText);
    m_nameLabel->setAlignment(Qt::AlignHCenter);
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    const bool previewsShown = InformationPanelSettings::previewsShown();
    m_preview->setVisible(previewsShown);

    m_metaDataWidget = new Baloo::FileMetaDataWidget(parent);
    m_metaDataWidget->setDateFormat(static_cast<Baloo::DateFormats>(InformationPanelSettings::dateFormat()));
    connect(m_metaDataWidget, &Baloo::FileMetaDataWidget::urlActivated,
            this, &InformationPanelContent::urlActivated);
    m_metaDataWidget->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    m_metaDataWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Encapsulate the MetaDataWidget inside a container that has a dummy widget
    // at the bottom. This prevents that the meta data widget gets vertically stretched
    // in the case where the height of m_metaDataArea > m_metaDataWidget.
    QWidget* metaDataWidgetContainer = new QWidget(parent);
    QVBoxLayout* containerLayout = new QVBoxLayout(metaDataWidgetContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);
    containerLayout->addWidget(m_metaDataWidget);
    containerLayout->addStretch();

    m_metaDataArea = new QScrollArea(parent);
    m_metaDataArea->setWidget(metaDataWidgetContainer);
    m_metaDataArea->setWidgetResizable(true);
    m_metaDataArea->setFrameShape(QFrame::NoFrame);

    QWidget* viewport = m_metaDataArea->viewport();
    viewport->installEventFilter(this);

    layout->addWidget(m_preview);
    layout->addWidget(m_phononWidget);
    layout->addWidget(m_nameLabel);
    layout->addWidget(new KSeparator());
    layout->addWidget(m_metaDataArea);

    m_placesItemModel = new PlacesItemModel(this);
}

InformationPanelContent::~InformationPanelContent()
{
    InformationPanelSettings::self()->save();
}

void InformationPanelContent::showItem(const KFileItem& item)
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    const QUrl itemUrl = item.url();
    const bool isSearchUrl = itemUrl.scheme().contains(QStringLiteral("search")) && item.localPath().isEmpty();
    setNameLabelText(item.text());
    if (isSearchUrl) {
        // in the case of a search-URL the URL is not readable for humans
        // (at least not useful to show in the Information Panel)
        m_preview->setPixmap(
            QIcon::fromTheme(QStringLiteral("nepomuk")).pixmap(KIconLoader::SizeEnormous, KIconLoader::SizeEnormous)
        );
    } else {
        // try to get a preview pixmap from the item...

        // Mark the currently shown preview as outdated. This is done
        // with a small delay to prevent a flickering when the next preview
        // can be shown within a short timeframe. This timer is not started
        // for directories, as directory previews might fail and return the
        // same icon.
        if (!item.isDir()) {
            m_outdatedPreviewTimer->start();
        }

        QStringList plugins = KIO::PreviewJob::availablePlugins();
        m_previewJob = new KIO::PreviewJob(KFileItemList() << item,
                                           QSize(m_preview->width(), m_preview->height()),
                                           &plugins);
        m_previewJob->setScaleType(KIO::PreviewJob::Unscaled);
        m_previewJob->setIgnoreMaximumSize(item.isLocalFile());
        if (m_previewJob->uiDelegate()) {
            KJobWidgets::setWindow(m_previewJob, this);
        }

        connect(m_previewJob.data(), &KIO::PreviewJob::gotPreview,
                this, &InformationPanelContent::showPreview);
        connect(m_previewJob.data(), &KIO::PreviewJob::failed,
                this, &InformationPanelContent::showIcon);
    }

    if (m_metaDataWidget) {
        m_metaDataWidget->setDateFormat(static_cast<Baloo::DateFormats>(InformationPanelSettings::dateFormat()));
        m_metaDataWidget->show();
        m_metaDataWidget->setItems(KFileItemList() << item);
    }

    if (InformationPanelSettings::previewsShown()) {
        const QString mimeType = item.mimetype();
        const bool usePhonon = mimeType.startsWith(QLatin1String("audio/")) || mimeType.startsWith(QLatin1String("video/"));
        if (usePhonon) {
            m_phononWidget->show();
            m_phononWidget->setUrl(item.targetUrl());
            if (m_preview->isVisible()) {
                m_phononWidget->setVideoSize(m_preview->size());
            }
        } else {
            m_phononWidget->hide();
            m_preview->setVisible(true);
        }
    } else {
        m_phononWidget->hide();
    }

    m_item = item;
}

void InformationPanelContent::showItems(const KFileItemList& items)
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    m_preview->setPixmap(
        QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(KIconLoader::SizeEnormous, KIconLoader::SizeEnormous)
    );
    setNameLabelText(i18ncp("@label", "%1 item selected", "%1 items selected", items.count()));

    if (m_metaDataWidget) {
        m_metaDataWidget->setItems(items);
    }

    m_phononWidget->hide();

    m_item = KFileItem();
}

bool InformationPanelContent::eventFilter(QObject* obj, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Resize: {
        QResizeEvent* resizeEvent = static_cast<QResizeEvent*>(event);
        if (obj == m_metaDataArea->viewport()) {
            // The size of the meta text area has changed. Adjust the fixed
            // width in a way that no horizontal scrollbar needs to be shown.
            m_metaDataWidget->setFixedWidth(resizeEvent->size().width());
        } else if (obj == parent()) {
            adjustWidgetSizes(resizeEvent->size().width());
        }
        break;
    }

    case QEvent::Polish:
        adjustWidgetSizes(parentWidget()->width());
        break;

    case QEvent::FontChange:
        m_metaDataWidget->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
        break;

    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}

void InformationPanelContent::showIcon(const KFileItem& item)
{
    m_outdatedPreviewTimer->stop();
    QPixmap pixmap = QIcon::fromTheme(item.iconName()).pixmap(KIconLoader::SizeEnormous, KIconLoader::SizeEnormous);
    KIconLoader::global()->drawOverlays(item.overlays(), pixmap, KIconLoader::Desktop);
    m_preview->setPixmap(pixmap);
}

void InformationPanelContent::showPreview(const KFileItem& item,
                                          const QPixmap& pixmap)
{
    m_outdatedPreviewTimer->stop();
    Q_UNUSED(item);

    QPixmap p = pixmap;
    KIconLoader::global()->drawOverlays(item.overlays(), p, KIconLoader::Desktop);
    m_preview->setPixmap(p);
}

void InformationPanelContent::markOutdatedPreview()
{
    KIconEffect *iconEffect = KIconLoader::global()->iconEffect();
    QPixmap disabledPixmap = iconEffect->apply(m_preview->pixmap(),
                                               KIconLoader::Desktop,
                                               KIconLoader::DisabledState);
    m_preview->setPixmap(disabledPixmap);
}

void InformationPanelContent::setPreviewVisible(bool visible) {
    m_preview->setVisible(visible);
}

KFileItemList InformationPanelContent::items() {
    return m_metaDataWidget->items();
}

void InformationPanelContent::slotHasVideoChanged(bool hasVideo)
{
    m_preview->setVisible(InformationPanelSettings::previewsShown() && !hasVideo);
}

void InformationPanelContent::refreshMetaData()
{
    if (!m_item.isNull()) {
        showItem(m_item);
    }
}

void InformationPanelContent::setNameLabelText(const QString& text)
{
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    const QString processedText = Qt::mightBeRichText(text) ? text : KStringHandler::preProcessWrap(text);

    QTextLayout textLayout(processedText);
    textLayout.setFont(m_nameLabel->font());
    textLayout.setTextOption(textOption);

    QString wrappedText;
    wrappedText.reserve(processedText.length());

    // wrap the text to fit into the width of m_nameLabel
    textLayout.beginLayout();
    QTextLine line = textLayout.createLine();
    while (line.isValid()) {
        line.setLineWidth(m_nameLabel->width());
        wrappedText += processedText.midRef(line.textStart(), line.textLength());

        line = textLayout.createLine();
        if (line.isValid()) {
            wrappedText += QChar::LineSeparator;
        }
    }
    textLayout.endLayout();

    m_nameLabel->setText(wrappedText);
}

void InformationPanelContent::adjustWidgetSizes(int width)
{
    // If the text inside the name label or the info label cannot
    // get wrapped, then the maximum width of the label is increased
    // so that the width of the information panel gets increased.
    // To prevent this, the maximum width is adjusted to
    // the current width of the panel.
    const int maxWidth = width - style()->layoutSpacing(QSizePolicy::DefaultType, QSizePolicy::DefaultType, Qt::Horizontal) * 4;
    m_nameLabel->setMaximumWidth(maxWidth);

    // The metadata widget also contains a text widget which may return
    // a large preferred width.
    if (m_metaDataWidget) {
        m_metaDataWidget->setMaximumWidth(maxWidth);
    }

    // try to increase the preview as large as possible
    m_preview->setSizeHint(QSize(maxWidth, maxWidth));

    if (m_phononWidget->isVisible()) {
        // assure that the size of the video player is the same as the preview size
        m_phononWidget->setVideoSize(QSize(maxWidth, maxWidth));
    }
}

